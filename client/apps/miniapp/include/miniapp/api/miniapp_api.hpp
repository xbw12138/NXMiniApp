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

/// Fetches the catalogue and packages from the store backend.
class MiniAppApi {
public:
    using ListCallback =
        std::function<void(const std::vector<MiniAppItem>& items, const std::string& error)>;
    using DownloadCallback = std::function<void(const std::string& data, const std::string& error)>;

    static MiniAppApi& instance();

    void configure(const std::string& serverUrl);
    const std::string& serverUrl() const { return base; }
    bool ready() const { return !base.empty(); }

    void fetchList(ListCallback callback);
    void download(const std::string& id, DownloadCallback callback);

private:
    std::string resolveUrl(const std::string& path) const;

    nxkit::HttpClient http;
    std::string base;
};

}  // namespace miniapp
