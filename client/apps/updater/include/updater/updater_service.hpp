#pragma once

#include <functional>
#include <string>
#include <vector>

namespace updater {

/// 容器版本（来自后台 /api/v1/container/releases）。
struct ReleaseInfo {
    long long id = 0;
    std::string version;
    std::string notes;
    std::string downloadUrl;
    long long size = 0;
    long long createdAt = 0;
};

/// 服务器地址：与容器保持一致（环境变量 > config.json > 线上默认值）。
std::string serverUrl();

/// 本机持久化设备标识，与容器共用同一份 config.json。
std::string deviceId();

/// 拉取已发布的容器版本，按发布时间倒序。
bool fetchReleases(std::vector<ReleaseInfo>& out, std::string& error);

/// 当前 SD 卡上 miniapp.nro 的大小（不存在时为 -1）。
long long installedNroSize();

/// 下载并替换 /switch/miniapp.nro。progress(downloaded, total) 可能为高频回调。
bool installRelease(const ReleaseInfo& release,
                    const std::function<void(long long, long long)>& progress,
                    std::string& error);

/// 上传 /switch/miniapp/miniapp.log 到后台设备日志。
bool uploadLog(std::string& error);

/// 商店里的 NRO 应用（来自后台 /api/v1/apps）。
struct StoreAppInfo {
    std::string id;
    std::string title;
    std::string summary;
    std::string description;
    std::string category;
    std::string developer;
    std::string iconUrl;
    std::string version;
    std::string iconPath;  // 本地缓存路径，由 fetchIcon 填充
    long long size = 0;
    long long downloads = 0;
    bool featured = false;
};

/// 拉取商店应用列表（已上架）。
bool fetchStoreApps(std::vector<StoreAppInfo>& out, std::string& error);

/// 已安装到 SD 卡的 NRO 路径；不存在返回空串。
std::string installedNroPath(const std::string& id);

/// 已安装版本号；未安装返回空串，安装记录缺失返回 "-"。
std::string installedNroVersion(const std::string& id);

/// 下载并安装（覆盖）NRO 到 /switch/<id>.nro。
bool installStoreApp(const StoreAppInfo& app,
                     const std::function<void(long long, long long)>& progress,
                     std::string& error);

/// 删除 /switch/<id>.nro。
bool removeStoreApp(const std::string& id, std::string& error);

/// 下载应用图标到本地缓存，返回本地路径（失败返回空串）。
std::string fetchIcon(const std::string& id, const std::string& iconUrl);


}  // namespace updater
