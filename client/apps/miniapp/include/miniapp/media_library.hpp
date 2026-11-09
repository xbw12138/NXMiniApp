//
// miniapp: 原生媒体库索引（SMB 目录树增量扫描 + 落盘）。
//
// 索引按数据源存成一份 JSON（每个目录记录修改时间与文件列表），
// 目录 mtime 未变化时直接复用旧结果，避免每次都整棵重扫。
//

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace miniapp {

struct MediaEntry {
    std::string path;
    uint64_t size = 0;
    /// 同目录下与视频同名的外挂字幕（.srt/.ass/.ssa/.vtt），找不到则为空。
    std::string subtitle;
};

class MediaLibrary {
public:
    static MediaLibrary& instance();

    /// 增量扫描 SMB 数据源并写盘索引，返回索引内文件总数。
    int scanSmb(const std::string& sourceId, const std::string& smbId, const std::string& root,
                int maxDepth, int maxFiles, std::string& error);

    /// 读取一页文件（按路径排序），total 返回索引内文件总数。
    std::vector<MediaEntry> page(const std::string& sourceId, int offset, int limit, int& total,
                                 std::string& error);

private:
    MediaLibrary() = default;
    std::string indexPath(const std::string& sourceId) const;

    // 写盘互斥：先写临时文件再改名，读库请求永远不会读到半截 JSON；
    // 只锁写盘这一小段，扫描期间不阻塞读库。
    std::mutex mutex_;
};

}  // namespace miniapp
