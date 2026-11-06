//
// miniapp: client for the mini program catalogue API.
//

#pragma once

#include "nxkit/http_client.hpp"

#include <functional>
#include <string>
#include <vector>

namespace miniapp {

struct MiniAppItem {
    std::string id;
    std::string title;
    std::string summary;
    std::string category;
    std::string developer;
    std::string iconUrl;
    std::string version;
    long long size = 0;
    long long updatedAt = 0;
};

/// 首页内容位：横幅或资讯。
struct HomeContentItem {
    int id = 0;
    std::string kind;
    std::string title;
    std::string summary;
    std::string body;
    std::string coverUrl;
    std::string linkType;
    std::string linkTarget;
    int sort = 0;
};

/// 首页描述：home 为空表示服务端未配置首页小程序。
struct HomeInfo {
    bool hasHome = false;
    MiniAppItem home;
    std::vector<HomeContentItem> banners;
    std::vector<HomeContentItem> news;
    std::vector<MiniAppItem> featured;
};

/// Fetches the catalogue and packages from the store backend.
class MiniAppApi {
public:
    using ListCallback =
        std::function<void(const std::vector<MiniAppItem>& items, const std::string& error)>;
    using DownloadCallback = std::function<void(const std::string& data, const std::string& error)>;
    using HomeCallback = std::function<void(const HomeInfo& info, const std::string& error)>;

    static MiniAppApi& instance();

    void configure(const std::string& serverUrl);
    const std::string& serverUrl() const { return base; }
    /// 本机设备标识（服务端灰度分流用），首次调用时生成并持久化。
    std::string deviceId() const;
    bool ready() const { return !base.empty(); }

    void fetchList(ListCallback callback);
    void search(const std::string& keyword, ListCallback callback);
    void fetchHome(HomeCallback callback);
    void download(const std::string& id, DownloadCallback callback);

private:
    std::string resolveUrl(const std::string& path) const;

    nxkit::HttpClient http;
    std::string base;
};

}  // namespace miniapp
