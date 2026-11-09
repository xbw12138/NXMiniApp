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

}  // namespace updater
