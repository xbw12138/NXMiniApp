//
// miniapp: SMB 数据源 —— libsmb2 封装 + 本地 HTTP 流代理。
//
// 小程序层通过 wx.smb.* 调用这里的能力。libsmb2 提供同步阻塞 API，
// 因此所有 SMB 调用都在工作线程执行（由 MiniappView 用 std::thread 包装），
// 并用一把全局互斥锁串行化（libsmb2 的 context 非线程安全）。
//
// 播放链路：小程序拿到的播放地址是本地 HTTP 代理 URL
//   http://127.0.0.1:<port>/<smbId>/<urlencoded path>
// video 组件把它当作普通远程 http 源交给 MPV 播放；MPV 的 Range 请求
// 由本模块的 mongoose 服务器解析后，用 smb2_pread 从 SMB 读取对应字节返回。
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// libsmb2 前向声明（避免把 C 头带进所有 TU）。
struct smb2_context;
struct smb2fh;
struct mg_connection;

namespace miniapp {

struct SmbEntry {
    std::string name;
    bool isDir = false;
    uint64_t size = 0;
};

/// SMB 管理器（单例）。管理若干 SMB 会话 + 一个本地 HTTP 流代理。
class SmbManager {
public:
    static SmbManager& instance();

    /// 启动 HTTP 代理监听线程。幂等。
    void init();
    /// 停止代理并断开所有会话。
    void shutdown();

    // ---- JS 层操作（同步阻塞；调用方放到 worker 线程执行）----
    std::string connect(const std::string& server, const std::string& share,
                        const std::string& user, const std::string& password,
                        const std::string& domain, std::string& error);
    std::vector<SmbEntry> listDir(const std::string& id, const std::string& path,
                                  std::string& error);
    /// 列目录并附带目录修改时间（用于媒体库增量扫描）。
    bool listDirMeta(const std::string& id, const std::string& path,
                     std::vector<SmbEntry>& out, uint32_t& mtime, std::string& error);
    /// 一次调用递归遍历目录树，只返回文件条目（不含目录）。
    /// 整个遍历在同一个线程里完成，避免逐目录建线程与 JS↔原生往返。
    /// progress 可为空，回调携带 (已遍历目录数, 已收集文件数)。
    /// cancelScan() 置位后，遍历会在当前目录处理完后尽快返回已收集结果。
    std::vector<SmbEntry> scan(const std::string& id, const std::string& root, int maxDepth,
                               int maxEntries, std::string& error,
                               const std::function<void(int, int)>& progress = {});
    /// 请求取消正在进行的 scan（异步、线程安全，不会阻塞）。
    void cancelScan() { scanCancel_.store(true); }
    void disconnect(const std::string& id);

    /// 生成本地 HTTP 代理播放地址。代理不可用时返回空串，调用方应据此提示错误。
    std::string streamUrl(const std::string& id, const std::string& path);

    /// 代理监听端口（init 完成且成功时非 0）。
    int proxyPort() const { return port_.load(); }

private:
    struct Session {
        std::string id;
        std::string server;
        std::string share;
        std::string user;
        std::string password;
        std::string domain;
        smb2_context* ctx = nullptr;
        bool connected = false;
    };

    SmbManager() = default;
    ~SmbManager();

    Session* findSession(const std::string& id);
    /// 会话断线时按保存的凭据重建 ctx；成功返回 true。
    bool ensureConnected(Session* session, std::string& error);
    bool listDirInternal(Session* session, const std::string& path, std::vector<SmbEntry>& out,
                         bool verbose, std::string* error = nullptr);

    // mongoose HTTP 事件回调。
    void handleHttp(mg_connection* c, int ev, void* ev_data);

    // 代理线程主体（由 init() 用带显式栈大小的 pthread 拉起）。
    void runProxyLoop();
    static void* proxyEntry(void* arg);

    // HTTP 代理内部使用：按 smbId + path 取/读/回收文件句柄。
    // 句柄做 LRU 缓存，避免 MPV 每个 Range 请求都重新 open+stat（一次网络往返）。
    struct CachedFile {
        smb2fh* fh = nullptr;
        uint64_t size = 0;
        double lastUsed = 0.0;
    };
    bool acquireFile(const std::string& id, const std::string& path, smb2fh*& fh,
                     uint64_t& size, std::string& error);
    int pread(const std::string& id, smb2fh* fh, uint8_t* buf, uint32_t count,
              uint64_t offset);
    /// 把句柄归还缓存（不是关闭），由 LRU/shutdown/disconnect 统一关闭。
    void releaseFile(const std::string& id, const std::string& path, smb2fh* fh);
    void closeCachedFiles();

    std::mutex mutex_;
    std::map<std::string, Session> sessions_;
    int nextId_ = 1;

    std::map<std::string, CachedFile> files_;

    std::atomic<int> port_{0};
    std::atomic<bool> proxyRunning_{false};
    std::atomic<bool> proxyStop_{false};
    std::atomic<bool> scanCancel_{false};

    // init() 等代理线程完成端口绑定后再返回，避免 streamUrl 拿到 0 端口。
    std::mutex proxyMutex_;
    std::condition_variable proxyCv_;
    bool proxyReady_ = false;
};

}  // namespace miniapp
