//
// miniapp: client for the mini program catalogue API.
//

#include "miniapp/api/miniapp_api.hpp"

#include <nlohmann/json.hpp>

namespace miniapp {

namespace {

std::string asString(const nlohmann::json& value, const char* key,
                     const std::string& fallback = "") {
    if (!value.is_object()) return fallback;
    auto it = value.find(key);
    if (it == value.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

long long asInt(const nlohmann::json& value, const char* key, long long fallback = 0) {
    if (!value.is_object()) return fallback;
    auto it = value.find(key);
    if (it == value.end() || !it->is_number()) return fallback;
    return it->get<long long>();
}

std::string absoluteUrl(const std::string& base, const std::string& path) {
    if (path.empty()) return "";
    if (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0) return path;
    if (path[0] == '/') return base + path;
    return base + "/" + path;
}

std::string errorOf(const nxkit::HttpResult& result) {
    nlohmann::json payload = result.json();
    if (payload.is_object() && payload.contains("detail") && payload["detail"].is_string()) {
        return payload["detail"].get<std::string>();
    }
    if (!result.error.empty()) return result.error;
    return "HTTP " + std::to_string(result.status);
}

}  // namespace

MiniAppApi& MiniAppApi::instance() {
    static MiniAppApi api;
    return api;
}

void MiniAppApi::configure(const std::string& serverUrl) {
    std::string url = serverUrl;
    while (!url.empty() && (url.back() == ' ' || url.back() == '/')) url.pop_back();
    if (url.find("://") == std::string::npos && !url.empty()) url = "http://" + url;
    base = url;
#ifdef __SWITCH__
    nxkit::HttpClient::setDefaultCaBundle("romfs:/cacert.pem");
#endif
    http.setBaseUrl(base);
    http.setTimeout(15000);
}

std::string MiniAppApi::resolveUrl(const std::string& path) const {
    return miniapp::absoluteUrl(base, path);
}

void MiniAppApi::fetchList(ListCallback callback) {
    if (!ready()) {
        if (callback) callback({}, "未配置小程序服务器");
        return;
    }

    const std::string baseUrl = this->base;
    http.getAsync("/api/v1/miniapps", {}, [callback, baseUrl](const nxkit::HttpResult& result) {
        if (!result.ok) {
            if (callback) callback({}, errorOf(result));
            return;
        }
        nlohmann::json payload = result.json();
        if (!payload.is_object() || !payload.contains("items") || !payload["items"].is_array()) {
            if (callback) callback({}, "服务器返回格式不正确");
            return;
        }

        std::vector<MiniAppItem> items;
        for (const auto& entry : payload["items"]) {
            if (!entry.is_object()) continue;
            MiniAppItem item;
            item.id = asString(entry, "id");
            if (item.id.empty()) continue;
            item.title = asString(entry, "title", item.id);
            item.summary = asString(entry, "summary");
            item.category = asString(entry, "category");
            item.developer = asString(entry, "developer");
            item.iconUrl = absoluteUrl(baseUrl, asString(entry, "icon_url"));
            item.version = asString(entry, "version");
            item.size = asInt(entry, "size");
            item.updatedAt = asInt(entry, "updated_at");
            items.push_back(std::move(item));
        }
        if (callback) callback(items, "");
    });
}

void MiniAppApi::download(const std::string& id, DownloadCallback callback) {
    if (!ready()) {
        if (callback) callback("", "未配置小程序服务器");
        return;
    }
    http.getAsync("/api/v1/miniapps/" + id + "/download", {},
                  [callback](const nxkit::HttpResult& result) {
                      if (!result.ok) {
                          if (callback) callback("", errorOf(result));
                          return;
                      }
                      if (result.body.empty()) {
                          if (callback) callback("", "安装包为空");
                          return;
                      }
                      if (callback) callback(result.body, "");
                  },
                  30000);
}

}  // namespace miniapp
