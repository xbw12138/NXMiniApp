#include "nxkit/http_client.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace nxkit {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

HttpResult fromResponse(const cpr::Response& response) {
    HttpResult result;
    result.status = response.status_code;
    result.body = response.text;

    for (const auto& header : response.header) {
        std::string name = lower(header.first);
        result.headers[name] = header.second;
        if (name == "set-cookie") {
            if (!result.setCookie.empty()) result.setCookie += "; ";
            result.setCookie += header.second;
        }
    }
    for (const auto& cookie : response.cookies) {
        result.cookies[cookie.GetName()] = cookie.GetValue();
    }

    if (response.error) {
        result.error = response.error.message;
        return result;
    }

    result.ok = response.status_code >= 200 && response.status_code < 300;
    if (!result.ok) {
        result.error = "HTTP " + std::to_string(response.status_code);
    }
    return result;
}

}  // namespace

std::string mergeCookies(const std::string& existing,
                         const std::map<std::string, std::string>& pairs) {
    std::map<std::string, std::string> merged;
    size_t pos = 0;
    while (pos < existing.size()) {
        size_t end = existing.find("; ", pos);
        if (end == std::string::npos) end = existing.size();
        std::string pair = existing.substr(pos, end - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && eq > 0) merged[pair.substr(0, eq)] = pair.substr(eq + 1);
        pos = end + 2;
    }
    for (const auto& pair : pairs) merged[pair.first] = pair.second;

    std::string out;
    for (const auto& pair : merged) {
        if (!out.empty()) out += "; ";
        out += pair.first + "=" + pair.second;
    }
    return out;
}

nlohmann::json HttpResult::json() const {
    nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    return parsed;
}

namespace {

std::string& defaultCaBundleStorage() {
#ifdef __SWITCH__
    // No system trust store on Switch; use the bundle baked into romfs.
    static std::string path = "romfs:/cacert.pem";
#else
    static std::string path;
#endif
    return path;
}

}  // namespace

const std::string& HttpClient::sharedCaBundle() { return defaultCaBundleStorage(); }

void HttpClient::setDefaultCaBundle(const std::string& path) { defaultCaBundleStorage() = path; }

cpr::SslOptions HttpClient::sslOptions() const {
    cpr::SslOptions options;
    options.verify_host = verifySsl;
    options.verify_peer = verifySsl;
    options.ca_info = caBundlePath();
    return options;
}

void HttpClient::applyCaBundle(cpr::Session& session) {
    std::string path = sharedCaBundle();
    if (path.empty()) return;
    cpr::SslOptions options;
    options.ca_info = path;
    session.SetOption(options);
}

void HttpClient::setBaseUrl(const std::string& url) {
    base = url;
    while (!base.empty() && base.back() == '/') base.pop_back();
}

void HttpClient::setHeader(const std::string& name, const std::string& value) {
    extraHeaders[name] = value;
}

void HttpClient::removeHeader(const std::string& name) { extraHeaders.erase(name); }

void HttpClient::clearHeaders() { extraHeaders.clear(); }

std::map<std::string, std::string>& defaultHeaderStorage() {
    static std::map<std::string, std::string> headers;
    return headers;
}

void HttpClient::setDefaultHeader(const std::string& name, const std::string& value) {
    defaultHeaderStorage()[name] = value;
}

std::string HttpClient::urlFor(const std::string& path) const {
    if (path.empty()) return base;
    if (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0) return path;
    if (path[0] == '/') return base + path;
    return base + "/" + path;
}

cpr::Header HttpClient::headers() const {
    cpr::Header header;
    for (const auto& entry : defaultHeaderStorage()) header[entry.first] = entry.second;
    for (const auto& entry : extraHeaders) header[entry.first] = entry.second;
    if (!cookieValue.empty()) header["Cookie"] = cookieValue;
    if (!bearer.empty()) header["Authorization"] = "Bearer " + bearer;
    return header;
}

int HttpClient::timeoutFor(int timeout) const { return timeout > 0 ? timeout : timeoutMs; }

cpr::Redirect HttpClient::redirect() const { return cpr::Redirect{followRedirects}; }

cpr::Parameters HttpClient::toParameters(const Query& query) {
    cpr::Parameters parameters;
    for (const auto& pair : query) parameters.Add(cpr::Parameter(pair.first, pair.second));
    return parameters;
}

cpr::Payload HttpClient::toPayload(const Query& form) {
    std::vector<cpr::Pair> pairs;
    pairs.reserve(form.size());
    for (const auto& pair : form) pairs.emplace_back(pair.first, pair.second);
    return cpr::Payload(pairs.begin(), pairs.end());
}

HttpResult HttpClient::get(const std::string& path, const Query& query, int timeout) const {
    cpr::Response response =
        cpr::Get(cpr::Url{urlFor(path)}, toParameters(query), headers(), redirect(), sslOptions(),
                 cpr::Timeout{timeoutFor(timeout)});
    return fromResponse(response);
}

HttpResult HttpClient::post(const std::string& path, const Query& query, const Query& form,
                            int timeout) const {
    cpr::Response response =
        cpr::Post(cpr::Url{urlFor(path)}, toParameters(query), headers(), toPayload(form),
                  redirect(), sslOptions(), cpr::Timeout{timeoutFor(timeout)});
    return fromResponse(response);
}

HttpResult HttpClient::postJson(const std::string& path, const Query& query,
                                const nlohmann::json& body, int timeout) const {
    cpr::Header header = headers();
    header["Content-Type"] = "application/json";

    cpr::Response response =
        cpr::Post(cpr::Url{urlFor(path)}, toParameters(query), header, cpr::Body{body.dump()},
                  redirect(), sslOptions(), cpr::Timeout{timeoutFor(timeout)});
    return fromResponse(response);
}

void HttpClient::getAsync(const std::string& path, const Query& query,
                          std::function<void(const HttpResult&)> callback, int timeout) const {
    cpr::GetCallback([callback](const cpr::Response& response) { callback(fromResponse(response)); },
                     cpr::Url{urlFor(path)}, toParameters(query), headers(), redirect(), sslOptions(),
                     cpr::Timeout{timeoutFor(timeout)});
}

void HttpClient::postAsync(const std::string& path, const Query& query, const Query& form,
                           std::function<void(const HttpResult&)> callback, int timeout) const {
    cpr::PostCallback([callback](const cpr::Response& response) { callback(fromResponse(response)); },
                      cpr::Url{urlFor(path)}, toParameters(query), headers(), toPayload(form),
                      redirect(), sslOptions(), cpr::Timeout{timeoutFor(timeout)});
}

void HttpClient::postJsonAsync(const std::string& path, const Query& query,
                               const nlohmann::json& body,
                               std::function<void(const HttpResult&)> callback, int timeout) const {
    cpr::Header header = headers();
    header["Content-Type"] = "application/json";

    cpr::PostCallback([callback](const cpr::Response& response) { callback(fromResponse(response)); },
                      cpr::Url{urlFor(path)}, toParameters(query), header, cpr::Body{body.dump()},
                      redirect(), sslOptions(), cpr::Timeout{timeoutFor(timeout)});
}

}  // namespace nxkit
