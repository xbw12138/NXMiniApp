#pragma once

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace nxkit {

using Query = std::vector<std::pair<std::string, std::string>>;

struct HttpResult {
    bool ok = false;
    long status = 0;
    std::string body;
    std::string error;
    /// Raw "Set-Cookie" header returned by the server (may be empty).
    std::string setCookie;
    /// Cookies parsed from all Set-Cookie headers of the final response.
    std::map<std::string, std::string> cookies;
    /// All response headers, keys lowercased (multi-value headers keep the
    /// last value; use `cookies` for Set-Cookie).
    std::map<std::string, std::string> headers;

    /// Parses body as JSON. Returns a discarded value on failure.
    nlohmann::json json() const;
};

/// Merges cookie pairs into an existing "k1=v1; k2=v2" cookie string.
/// New pairs override same-named entries. Returns the merged string.
std::string mergeCookies(const std::string& existing,
                         const std::map<std::string, std::string>& pairs);

/// Thin, reusable wrapper around cpr: base url, default query/headers,
/// cookies and JSON helpers. Safe to call from worker threads.
class HttpClient {
public:
    void setBaseUrl(const std::string& url);
    std::string baseUrl() const { return base; }

    void setHeader(const std::string& name, const std::string& value);
    void removeHeader(const std::string& name);
    void clearHeaders();
    /// Process-wide default header applied to every request of every client
    /// (used for the device id that drives server-side rollout bucketing).
    static void setDefaultHeader(const std::string& name, const std::string& value);

    void setCookie(const std::string& cookie) { cookieValue = cookie; }
    const std::string& cookie() const { return cookieValue; }

    void setBearerToken(const std::string& token) { bearer = token; }
    void setVerifySsl(bool verify) { verifySsl = verify; }
    /// CA bundle (PEM) used for certificate verification. Empty means "use
    /// the shared default" (set via setDefaultCaBundle, usually
    /// romfs:/cacert.pem on Switch) or the system trust store.
    void setCaBundle(const std::string& path) { caBundle = path; }
    static void setDefaultCaBundle(const std::string& path);
    /// Applies the resolved CA bundle (if any) to a raw cpr session, for code
    /// that performs its own streaming requests. Call before setting
    /// cpr::VerifySsl so an explicit verify flag still wins.
    static void applyCaBundle(cpr::Session& session);
    void setFollowRedirects(bool follow) { followRedirects = follow; }

    void setTimeout(int milliseconds) { timeoutMs = milliseconds; }
    int timeout() const { return timeoutMs; }

    HttpResult get(const std::string& path, const Query& query = {}, int timeout = -1) const;
    HttpResult post(const std::string& path, const Query& query = {}, const Query& form = {}, int timeout = -1) const;
    HttpResult postJson(const std::string& path, const Query& query, const nlohmann::json& body,
                        int timeout = -1) const;

    void getAsync(const std::string& path, const Query& query,
                  std::function<void(const HttpResult&)> callback, int timeout = -1) const;
    void postAsync(const std::string& path, const Query& query, const Query& form,
                   std::function<void(const HttpResult&)> callback, int timeout = -1) const;
    void postJsonAsync(const std::string& path, const Query& query, const nlohmann::json& body,
                       std::function<void(const HttpResult&)> callback, int timeout = -1) const;

private:
    std::string urlFor(const std::string& path) const;
    cpr::Header headers() const;
    int timeoutFor(int timeout) const;
    cpr::Redirect redirect() const;
    cpr::SslOptions sslOptions() const;
    std::string caBundlePath() const { return caBundle.empty() ? sharedCaBundle() : caBundle; }

    static const std::string& sharedCaBundle();

    static cpr::Parameters toParameters(const Query& query);
    static cpr::Payload toPayload(const Query& form);

    std::string base;
    std::map<std::string, std::string> extraHeaders;
    std::string cookieValue;
    std::string bearer;
    bool verifySsl = true;
    bool followRedirects = true;
    std::string caBundle;
    int timeoutMs = 10000;
};

}  // namespace nxkit
