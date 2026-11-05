//
// miniapp: client for the mini program catalogue API.
//

#include "miniapp/api/miniapp_api.hpp"

#include <cctype>

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

MiniAppItem parseMiniApp(const nlohmann::json& entry, const std::string& baseUrl) {
    MiniAppItem item;
    item.id = asString(entry, "id");
    if (item.id.empty()) return item;
    item.title = asString(entry, "title", item.id);
    item.summary = asString(entry, "summary");
    item.category = asString(entry, "category");
    item.developer = asString(entry, "developer");
    item.iconUrl = absoluteUrl(baseUrl, asString(entry, "icon_url"));
    item.version = asString(entry, "version");
    item.size = asInt(entry, "size");
    item.updatedAt = asInt(entry, "updated_at");
    return item;
}

std::vector<MiniAppItem> parseMiniAppList(const nlohmann::json& payload,
                                          const std::string& baseUrl) {
    std::vector<MiniAppItem> items;
    if (!payload.is_object() || !payload.contains("items") || !payload["items"].is_array()) {
        return items;
    }
    for (const auto& entry : payload["items"]) {
        if (!entry.is_object()) continue;
        MiniAppItem item = parseMiniApp(entry, baseUrl);
        if (item.id.empty()) continue;
        items.push_back(std::move(item));
    }
    return items;
}

HomeContentItem parseContent(const nlohmann::json& entry, const std::string& baseUrl) {
    HomeContentItem item;
    item.id = static_cast<int>(asInt(entry, "id"));
    item.kind = asString(entry, "kind");
    item.title = asString(entry, "title");
    item.summary = asString(entry, "summary");
    item.body = asString(entry, "body");
    item.coverUrl = absoluteUrl(baseUrl, asString(entry, "cover_url"));
    item.linkType = asString(entry, "link_type", "none");
    item.linkTarget = asString(entry, "link_target");
    item.sort = static_cast<int>(asInt(entry, "sort"));
    return item;
}

std::vector<HomeContentItem> parseContentList(const nlohmann::json& payload, const char* key,
                                              const std::string& baseUrl) {
    std::vector<HomeContentItem> items;
    if (!payload.is_object() || !payload.contains(key) || !payload[key].is_array()) {
        return items;
    }
    for (const auto& entry : payload[key]) {
        if (!entry.is_object()) continue;
        items.push_back(parseContent(entry, baseUrl));
    }
    return items;
}

std::string encodeQuery(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
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
        if (callback) callback(parseMiniAppList(payload, baseUrl), "");
    });
}

void MiniAppApi::search(const std::string& keyword, ListCallback callback) {
    if (!ready()) {
        if (callback) callback({}, "未配置小程序服务器");
        return;
    }

    const std::string baseUrl = this->base;
    std::string path = "/api/v1/miniapps";
    std::string trimmed = keyword;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
    if (!trimmed.empty()) path += "?q=" + encodeQuery(trimmed);

    http.getAsync(path, {}, [callback, baseUrl](const nxkit::HttpResult& result) {
        if (!result.ok) {
            if (callback) callback({}, errorOf(result));
            return;
        }
        nlohmann::json payload = result.json();
        if (!payload.is_object()) {
            if (callback) callback({}, "服务器返回格式不正确");
            return;
        }
        if (callback) callback(parseMiniAppList(payload, baseUrl), "");
    });
}

void MiniAppApi::fetchHome(HomeCallback callback) {
    if (!ready()) {
        if (callback) callback(HomeInfo{}, "未配置小程序服务器");
        return;
    }

    const std::string baseUrl = this->base;
    http.getAsync("/api/v1/miniapp-home", {}, [callback, baseUrl](const nxkit::HttpResult& result) {
        if (!result.ok) {
            if (callback) callback(HomeInfo{}, errorOf(result));
            return;
        }
        nlohmann::json payload = result.json();
        if (!payload.is_object()) {
            if (callback) callback(HomeInfo{}, "服务器返回格式不正确");
            return;
        }

        HomeInfo info;
        if (payload.contains("home") && payload["home"].is_object()) {
            info.home = parseMiniApp(payload["home"], baseUrl);
            info.hasHome = !info.home.id.empty();
        }
        info.banners = parseContentList(payload, "banners", baseUrl);
        info.news = parseContentList(payload, "news", baseUrl);

        if (payload.contains("featured") && payload["featured"].is_array()) {
            for (const auto& entry : payload["featured"]) {
                if (!entry.is_object()) continue;
                MiniAppItem item = parseMiniApp(entry, baseUrl);
                if (!item.id.empty()) info.featured.push_back(std::move(item));
            }
        }
        if (callback) callback(info, "");
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
