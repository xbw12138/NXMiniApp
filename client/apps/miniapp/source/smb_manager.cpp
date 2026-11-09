//
// miniapp: SMB 数据源实现。
//

#include "miniapp/smb_manager.hpp"

#include <borealis/core/logger.hpp>

// libsmb2 公共头不自带 <stdint.h>/<stddef.h>，需先引入保证全局类型可用。
// 且 libsmb2.h 依赖 <smb2/smb2.h> 先声明 SMB2_GUID_SIZE / smb2_lease_key。
#include <stdint.h>
#include <stddef.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#include <mongoose.h>

#include <fcntl.h>

#ifdef __SWITCH__
#include <sys/iosupport.h>
#endif

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace miniapp {

namespace {

// 单次 HTTP 代理读取的最大字节数。MPV 的 Range 请求通常远小于此值；
// 限制它避免一次性把整段视频读进内存（Switch 内存有限）。
constexpr uint64_t kChunkSize = 4 * 1024 * 1024;

// POSIX 风格路径（"/" 分隔，前导 "/" 表示共享根）转 SMB 风格（"\" 分隔）。
std::string toSmbPath(const std::string& path) {
    std::string p = path;
    for (char& c : p) {
        if (c == '/') c = '\\';
    }
    size_t start = 0;
    while (start < p.size() && p[start] == '\\') ++start;
    p = p.substr(start);
    return p.empty() ? "\\" : p;
}

// 把文件名拼到当前 POSIX 目录路径后，得到完整 POSIX 路径。
std::string joinPosixPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/") return "/" + name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

#ifdef __SWITCH__
// 诊断：打印 soc 设备索引、devoptab 表内容，以及指定 fd 的 handle 指向。
void dumpSocketState(const char* tag, int fd = -1) {
    int soc = FindDevice("soc:");
    std::string table;
    for (int i = 3; i < STD_MAX; ++i) {
        if (!devoptab_list[i]) continue;
        if (!table.empty()) table += ",";
        table += std::to_string(i) + "=" + devoptab_list[i]->name;
    }
    __handle* handle = fd >= 0 ? __get_handle(fd) : nullptr;
    int dev = handle ? static_cast<int>(handle->device) : -1;
    const char* name =
        (dev >= 0 && dev < STD_MAX && devoptab_list[dev]) ? devoptab_list[dev]->name : "(null)";
    std::string fds;
    for (int i = 0; i < STD_MAX; ++i) {
        __handle* h = __get_handle(i);
        if (!h || h->refcount == 0) continue;
        if (!fds.empty()) fds += ",";
        fds += std::to_string(i) + ":" + std::to_string(h->device);
    }
    brls::Logger::info("smb diag {}: soc={} fd={} handle={} dev={} name={} devs=[{}] fds=[{}]", tag,
                       soc, fd, static_cast<void*>(handle), dev, name, table, fds);
}
#endif

uint64_t parseU64(const std::string& s) {
    if (s.empty()) return 0;
    return strtoull(s.c_str(), nullptr, 10);
}

double steadyMilliseconds() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(
               steady_clock::now().time_since_epoch())
        .count();
}

std::string trimText(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

}  // namespace

SmbManager& SmbManager::instance() {
    static SmbManager manager;
    return manager;
}

SmbManager::~SmbManager() {
    shutdown();
}

void SmbManager::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (proxyRunning_.load()) {
        // 已启动，但仍可能停在「线程已起、端口未绑定」的窗口期。
        std::unique_lock<std::mutex> proxyLock(proxyMutex_);
        proxyCv_.wait_for(proxyLock, std::chrono::milliseconds(800),
                          [this]() { return proxyReady_; });
        return;
    }
    proxyRunning_.store(true);
    proxyStop_.store(false);
    port_.store(0);
    {
        std::lock_guard<std::mutex> proxyLock(proxyMutex_);
        proxyReady_ = false;
    }

    std::thread t([this]() {
        struct mg_mgr mgr;
        mg_mgr_init(&mgr);

        struct mg_connection* listener = nullptr;
        for (int port = 18080; port < 18100 && !listener; ++port) {
            char url[64];
            std::snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);
            listener = mg_http_listen(&mgr, url, [](struct mg_connection* c, int ev,
                                                    void* ev_data, void*) {
                SmbManager::instance().handleHttp(c, ev, ev_data);
            }, nullptr);
            if (listener) {
                port_.store(port);
            }
        }
        auto signalReady = [this]() {
            {
                std::lock_guard<std::mutex> proxyLock(proxyMutex_);
                proxyReady_ = true;
            }
            proxyCv_.notify_all();
        };
        if (!listener) {
            brls::Logger::error("smb proxy: no port available in 18080-18099");
            proxyRunning_.store(false);
            port_.store(0);
            signalReady();
            mg_mgr_free(&mgr);
            return;
        }

        brls::Logger::info("smb proxy listening on 127.0.0.1:{}", port_.load());
#ifdef __SWITCH__
        brls::Logger::info("smb proxy diag: soc={} romfs={} sdmc={}", FindDevice("soc:"),
                           FindDevice("romfs:"), FindDevice("sdmc:"));
        for (int i = 0; i < 35; i++) {
            if (devoptab_list[i]) {
                brls::Logger::info("smb proxy diag: dev[{}]={}", i, devoptab_list[i]->name);
            }
        }
        for (struct mg_connection* mc = mgr.conns; mc != nullptr; mc = mc->next) {
            int fd = static_cast<int>(reinterpret_cast<size_t>(mc->fd));
            __handle* h = __get_handle(fd);
            int dev = h ? static_cast<int>(h->device) : -1;
            const char* nm =
                (dev >= 0 && dev < 35 && devoptab_list[dev]) ? devoptab_list[dev]->name : "(null)";
            brls::Logger::info("smb proxy diag: conn fd={} handle={} dev={} name={}", fd,
                               static_cast<void*>(h), dev, nm);
            if (h && dev >= 0 && dev < 35 && devoptab_list[dev] == nullptr) {
                brls::Logger::error("smb proxy diag: bad devoptab for fd {}, proxy disabled", fd);
                proxyRunning_.store(false);
                port_.store(0);
                signalReady();
                mg_mgr_free(&mgr);
                return;
            }
        }
#endif
        signalReady();
        while (!proxyStop_.load()) {
            mg_mgr_poll(&mgr, 50);
        }
        mg_mgr_free(&mgr);
    });
    t.detach();
    // 等端口绑定完成再返回，调用方随后立即 streamUrl() 才拿得到有效地址。
    std::unique_lock<std::mutex> proxyLock(proxyMutex_);
    proxyCv_.wait_for(proxyLock, std::chrono::milliseconds(800),
                      [this]() { return proxyReady_; });
}

void SmbManager::shutdown() {
    bool wasRunning = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wasRunning = proxyRunning_.load();
        proxyStop_.store(true);
        proxyRunning_.store(false);
        std::lock_guard<std::mutex> proxyLock(proxyMutex_);
        proxyReady_ = false;
    }
    // 即使代理从未成功启动，也要清理已建立的 SMB 会话与缓存句柄。
    if (wasRunning) std::this_thread::sleep_for(std::chrono::milliseconds(80));
    std::lock_guard<std::mutex> lock(mutex_);
    closeCachedFiles();
    for (auto& kv : sessions_) {
        if (kv.second.ctx) {
            smb2_disconnect_share(kv.second.ctx);
            smb2_destroy_context(kv.second.ctx);
            kv.second.ctx = nullptr;
        }
    }
    sessions_.clear();
}

void SmbManager::closeCachedFiles() {
    for (auto& kv : files_) {
        if (!kv.second.fh) continue;
        auto session = sessions_.find(kv.first.substr(0, kv.first.find('\n')));
        if (session != sessions_.end() && session->second.ctx) {
            smb2_close(session->second.ctx, kv.second.fh);
        }
        kv.second.fh = nullptr;
    }
    files_.clear();
}

SmbManager::Session* SmbManager::findSession(const std::string& id) {
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

bool SmbManager::ensureConnected(Session* session, std::string& error) {
    if (!session) {
        error = "SMB 会话不存在";
        return false;
    }
    if (session->connected && session->ctx) return true;

    // 断线重连：丢掉旧 ctx（可能已处于半关闭状态），按保存的凭据重建。
    if (session->ctx) {
        smb2_destroy_context(session->ctx);
        session->ctx = nullptr;
    }
    session->connected = false;

    smb2_context* ctx = smb2_init_context();
    if (!ctx) {
        error = "初始化 SMB 上下文失败";
        return false;
    }
    if (!session->user.empty()) smb2_set_user(ctx, session->user.c_str());
    if (!session->password.empty()) smb2_set_password(ctx, session->password.c_str());
    if (!session->domain.empty()) smb2_set_domain(ctx, session->domain.c_str());
    smb2_set_timeout(ctx, 10);

    int ret = smb2_connect_share(ctx, session->server.c_str(), session->share.c_str(),
                                 session->user.empty() ? nullptr : session->user.c_str());
    if (ret != 0) {
        const char* err = smb2_get_error(ctx);
        error = err ? err : "连接共享失败";
        brls::Logger::error("smb reconnect failed: server=[{}] share=[{}] ret={} err={}",
                            session->server, session->share, ret, error);
        smb2_destroy_context(ctx);
        return false;
    }
    session->ctx = ctx;
    session->connected = true;
    brls::Logger::info("smb session {} reconnected to {}/{}", session->id, session->server,
                       session->share);
    return true;
}

std::string SmbManager::connect(const std::string& server, const std::string& share,
                                const std::string& user, const std::string& password,
                                const std::string& domain, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 兼容用户把地址写成 smb://host/share 或带首尾斜杠的形式。
    std::string host = trimText(server);
    if (host.rfind("smb://", 0) == 0) host = host.substr(6);
    if (host.rfind("\\\\", 0) == 0) host = host.substr(2);
    while (!host.empty() && (host.back() == '/' || host.back() == '\\')) host.pop_back();
    std::string shareName = trimText(share);
    size_t slash = host.find_first_of("/\\");
    if (slash != std::string::npos) {
        if (shareName.empty()) shareName = host.substr(slash + 1);
        host = host.substr(0, slash);
    }
    while (!shareName.empty() && (shareName.back() == '/' || shareName.back() == '\\')) {
        shareName.pop_back();
    }
    if (host.empty()) {
        error = "服务器地址不能为空";
        brls::Logger::error("smb connect failed: empty server, share=[{}]", shareName);
        return "";
    }
    if (shareName.empty()) {
        error = "共享目录不能为空";
        brls::Logger::error("smb connect failed: server=[{}] empty share", host);
        return "";
    }

    const std::string id = "s" + std::to_string(nextId_++);
    Session& session = sessions_[id];
    session.id = id;
    session.server = host;
    session.share = shareName;
    session.user = user;
    session.password = password;
    session.domain = domain;

#ifdef __SWITCH__
    dumpSocketState("connect-before");
#endif
    const bool connected = ensureConnected(&session, error);
#ifdef __SWITCH__
    dumpSocketState("connect-after");
#endif
    if (!connected) {
        brls::Logger::error(
            "smb connect failed: server=[{}] share=[{}] user=[{}] domain=[{}] err={}",
            host, shareName, user, domain, error);
        sessions_.erase(id);
        return "";
    }
    return id;
}

bool SmbManager::listDirInternal(Session* s, const std::string& path, std::vector<SmbEntry>& out,
                                 bool verbose) {
    std::vector<SmbEntry> result;
    std::string smbPath = toSmbPath(path);
    std::string withSlash = "/" + smbPath;
    // libsmb2 对根目录/相对路径的写法不一致：优先用 "/xxx"（实测在多数服务器上直接可用），
    // 失败再退化到其余写法。每次失败的尝试都是一次网络往返，顺序直接影响扫描速度。
    struct smb2dir* dir = nullptr;
    const char* opened = nullptr;
    const char* candidates[] = {withSlash.c_str(), smbPath.c_str(), "/", ""};
    for (const char* candidate : candidates) {
        dir = smb2_opendir(s->ctx, candidate);
        if (dir) {
            opened = candidate;
            break;
        }
    }
    if (!dir) {
        if (verbose) {
            brls::Logger::error("smb listDir failed: path=[{}] err={}", path,
                               smb2_get_error(s->ctx));
        }
        return false;
    }

    struct smb2dirent* ent;
    while ((ent = smb2_readdir(s->ctx, dir)) != nullptr) {
        if (!ent->name) continue;
        if (std::strcmp(ent->name, ".") == 0 || std::strcmp(ent->name, "..") == 0) continue;
        SmbEntry e;
        e.name = joinPosixPath(path, ent->name);
        e.isDir = (ent->st.smb2_type == SMB2_TYPE_DIRECTORY);
        e.size = ent->st.smb2_size;
        result.push_back(e);
    }
    smb2_closedir(s->ctx, dir);
    if (verbose) {
        std::string sample;
        for (size_t i = 0; i < result.size() && i < 6; ++i) {
            if (!sample.empty()) sample += " | ";
            sample += result[i].isDir ? "D:" : "F:";
            sample += result[i].name;
        }
        brls::Logger::info("smb listDir path=[{}] opened=[{}] entries={} sample={}", path,
                           opened ? opened : "", result.size(), sample);
    }
    out = std::move(result);
    return true;
}

std::vector<SmbEntry> SmbManager::listDir(const std::string& id,
                                          const std::string& path,
                                          std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    Session* s = findSession(id);
    if (!ensureConnected(s, error)) return {};
    std::vector<SmbEntry> out;
    if (!listDirInternal(s, path, out, true) && error.empty()) error = "列目录失败";
    return out;
}

bool SmbManager::listDirMeta(const std::string& id, const std::string& path,
                             std::vector<SmbEntry>& out, uint32_t& mtime, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    Session* s = findSession(id);
    if (!ensureConnected(s, error)) return false;
    // 目录修改时间用于增量扫描：未变化的目录可以直接复用旧索引，不必再列一遍。
    std::string smbPath = toSmbPath(path);
    std::string withSlash = "/" + smbPath;
    struct smb2_stat_64 st{};
    mtime = 0;
    const char* candidates[] = {withSlash.c_str(), smbPath.c_str(), "/", ""};
    for (const char* candidate : candidates) {
        if (smb2_stat(s->ctx, candidate, &st) == 0) {
            mtime = static_cast<uint32_t>(st.smb2_mtime);
            break;
        }
    }
    return listDirInternal(s, path, out, false);
}

std::vector<SmbEntry> SmbManager::scan(const std::string& id, const std::string& root,
                                       int maxDepth, int maxEntries, std::string& error,
                                       const std::function<void(int, int)>& progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SmbEntry> files;
    Session* s = findSession(id);
    if (!ensureConnected(s, error)) return files;
    if (maxDepth < 1) maxDepth = 1;
    if (maxEntries < 1) maxEntries = 1;
    scanCancel_.store(false);

    // 深度优先遍历：一次原生调用完成整棵树的列目录，避免每个目录一次 JS↔原生往返。
    struct Node {
        std::string path;
        int depth;
    };
    std::vector<Node> stack;
    stack.push_back({root.empty() ? "/" : root, 0});
    int visitedDirs = 0;
    // 目录数也设上限，避免在没有视频的深层目录里无限遍历。
    int maxDirs = maxEntries * 3 + 10;
    while (!stack.empty() && static_cast<int>(files.size()) < maxEntries &&
           visitedDirs < maxDirs && !scanCancel_.load()) {
        Node node = stack.back();
        stack.pop_back();
        ++visitedDirs;
        std::vector<SmbEntry> entries;
        if (!listDirInternal(s, node.path, entries, false)) {
            // 中途掉线：重连后重试当前目录一次，避免整次扫描因一次瞬断而失败。
            std::string reconnectError;
            if (ensureConnected(s, reconnectError)) {
                listDirInternal(s, node.path, entries, false);
            }
        }
        for (auto& e : entries) {
            if (e.isDir) {
                if (node.depth + 1 <= maxDepth) stack.push_back({e.name, node.depth + 1});
            } else if (static_cast<int>(files.size()) < maxEntries) {
                files.push_back(e);
            }
        }
        // 每处理几个目录上报一次，避免逐目录触发 JS↔原生往返。
        if (progress && (visitedDirs % 4 == 0 || stack.empty())) {
            progress(visitedDirs, static_cast<int>(files.size()));
        }
    }
    brls::Logger::info("smb scan root=[{}] dirs={} files={} cancelled={}", root, visitedDirs,
                       files.size(), scanCancel_.load());
    return files;
}

void SmbManager::disconnect(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    // 先关掉该会话缓存的文件句柄，再销毁 ctx。
    const std::string prefix = id + "\n";
    for (auto f = files_.begin(); f != files_.end();) {
        if (f->first.compare(0, prefix.size(), prefix) == 0) {
            if (f->second.fh && it->second.ctx) {
                smb2_close(it->second.ctx, f->second.fh);
            }
            f = files_.erase(f);
        } else {
            ++f;
        }
    }
    if (it->second.ctx) {
        smb2_disconnect_share(it->second.ctx);
        smb2_destroy_context(it->second.ctx);
        it->second.ctx = nullptr;
    }
    sessions_.erase(it);
}

std::string SmbManager::streamUrl(const std::string& id, const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size() * 3);
    for (unsigned char c : path) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 0x0f]);
        }
    }
    const int port = port_.load();
    if (port <= 0) {
        brls::Logger::error("smb streamUrl: proxy is not listening (port=0)");
        return "";
    }
    return "http://127.0.0.1:" + std::to_string(port) + "/" + id + "/" + encoded;
}

bool SmbManager::acquireFile(const std::string& id, const std::string& path, smb2fh*& fh,
                             uint64_t& size, std::string& error) {
    // 调用方（handleHttp）已持有 mutex_，这里不再重复加锁。
    Session* s = findSession(id);
    if (!ensureConnected(s, error)) return false;

    const std::string key = id + "\n" + path;
    auto it = files_.find(key);
    if (it != files_.end() && it->second.fh) {
        it->second.lastUsed = steadyMilliseconds();
        fh = it->second.fh;
        size = it->second.size;
        return true;
    }

    constexpr size_t kMaxCachedFiles = 6;
    while (files_.size() >= kMaxCachedFiles) {
        auto oldest = files_.end();
        for (auto f = files_.begin(); f != files_.end(); ++f) {
            if (oldest == files_.end() || f->second.lastUsed < oldest->second.lastUsed) {
                oldest = f;
            }
        }
        if (oldest == files_.end()) break;
        auto owner = findSession(oldest->first.substr(0, oldest->first.find('\n')));
        if (oldest->second.fh && owner && owner->ctx) {
            smb2_close(owner->ctx, oldest->second.fh);
        }
        files_.erase(oldest);
    }

    std::string smbPath = toSmbPath(path);
    smb2fh* h = smb2_open(s->ctx, smbPath.c_str(), O_RDONLY);
    if (!h) {
        // 句柄失效常见于服务器重启/空闲断链：重连后重试一次。
        std::string reconnectError;
        if (ensureConnected(s, reconnectError)) {
            h = smb2_open(s->ctx, smbPath.c_str(), O_RDONLY);
        }
    }
    if (!h) {
        const char* err = s->ctx ? smb2_get_error(s->ctx) : nullptr;
        error = err ? err : "打开文件失败";
        return false;
    }
    struct smb2_stat_64 st;
    if (smb2_fstat(s->ctx, h, &st) == 0) {
        size = st.smb2_size;
    } else {
        size = 0;
    }
    CachedFile entry;
    entry.fh = h;
    entry.size = size;
    entry.lastUsed = steadyMilliseconds();
    files_[key] = entry;
    fh = h;
    return true;
}

int SmbManager::pread(const std::string& id, smb2fh* fh, uint8_t* buf, uint32_t count,
                      uint64_t offset) {
    // 调用方已持有 mutex_。
    Session* s = findSession(id);
    if (!s || !s->ctx) return -1;
    return smb2_pread(s->ctx, fh, buf, count, offset);
}

void SmbManager::releaseFile(const std::string& id, const std::string& path, smb2fh* fh) {
    // 句柄留在 LRU 里复用，只需刷新时间戳。
    auto it = files_.find(id + "\n" + path);
    if (it != files_.end() && it->second.fh == fh) {
        it->second.lastUsed = steadyMilliseconds();
    }
}

void SmbManager::handleHttp(mg_connection* c, int ev, void* ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);

    bool isGet = mg_vcasecmp(&hm->method, "GET") == 0;
    bool isHead = mg_vcasecmp(&hm->method, "HEAD") == 0;
    if (!isGet && !isHead) {
        mg_http_reply(c, 405, "", "Method Not Allowed");
        return;
    }

    // 解析 URI: /<smbId>/<urlencoded path>
    std::string uri(hm->uri.ptr, hm->uri.len);
    size_t second = uri.find('/', 1);
    if (uri.empty() || uri[0] != '/' || second == std::string::npos) {
        mg_http_reply(c, 400, "", "bad request");
        return;
    }
    std::string smbId = uri.substr(1, second - 1);
    std::string encodedPath = uri.substr(second + 1);

    std::string path;
    path.resize(encodedPath.size() * 3 + 1);
    int n = mg_url_decode(encodedPath.c_str(), encodedPath.size(), &path[0],
                          path.size(), 0);
    if (n < 0) {
        mg_http_reply(c, 400, "", "bad path");
        return;
    }
    path.resize(static_cast<size_t>(n));

    uint64_t size = 0;
    smb2fh* fh = nullptr;
    std::string error;
    // SMB 的所有文件操作都在同一把锁内完成（libsmb2 context 非线程安全）。
    std::unique_lock<std::mutex> lock(mutex_);
    if (!acquireFile(smbId, path, fh, size, error)) {
        lock.unlock();
        mg_http_reply(c, 404, "", "%s", error.c_str());
        return;
    }

    // HEAD：只返回元数据。
    if (isHead) {
        releaseFile(smbId, path, fh);
        lock.unlock();
        char hdr[128];
        std::snprintf(hdr, sizeof(hdr),
                      "Accept-Ranges: bytes\r\nContent-Type: application/octet-stream\r\n"
                      "Content-Length: %llu\r\n",
                      static_cast<unsigned long long>(size));
        mg_http_reply(c, 200, hdr, "");
        return;
    }

    // 解析 Range（MPV 总是用 Range 拉流）。
    // 支持三种形式：bytes=start-end / bytes=start- / bytes=-suffix。
    uint64_t start = 0;
    uint64_t end = size > 0 ? size - 1 : 0;
    struct mg_str* range = mg_http_get_header(hm, "Range");
    if (range && range->len > 0) {
        std::string r(range->ptr, range->len);
        if (r.compare(0, 6, "bytes=") == 0) {
            std::string spec = r.substr(6);
            size_t dash = spec.find('-');
            if (dash != std::string::npos) {
                std::string sstr = spec.substr(0, dash);
                std::string estr = spec.substr(dash + 1);
                if (!sstr.empty()) {
                    start = parseU64(sstr);
                    if (!estr.empty()) end = parseU64(estr);
                } else if (!estr.empty()) {
                    // 后缀 Range：最后 N 字节。之前误当成 [0, N]，会让 MPV
                    // 拉错位置（尤其是 moov 在尾部的 mp4）。
                    const uint64_t suffix = parseU64(estr);
                    start = suffix >= size ? 0 : size - suffix;
                    end = size > 0 ? size - 1 : 0;
                }
            }
        }
    }

    if (size == 0 || start >= size || start > end) {
        releaseFile(smbId, path, fh);
        lock.unlock();
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "Content-Range: bytes */%llu\r\n",
                      static_cast<unsigned long long>(size));
        mg_http_reply(c, 416, hdr, "");
        return;
    }

    // 限制单次读取大小，MPV 会继续发下一个 Range。
    if (end - start + 1 > kChunkSize) {
        end = start + kChunkSize - 1;
    }
    if (end >= size) end = size - 1;

    uint64_t len = end - start + 1;
    std::vector<uint8_t> buf(static_cast<size_t>(len));
    int rd = pread(smbId, fh, buf.data(), static_cast<uint32_t>(len), start);
    releaseFile(smbId, path, fh);
    lock.unlock();

    if (rd < 0) {
        mg_http_reply(c, 500, "", "read failed");
        return;
    }
    if (rd == 0) {
        mg_http_reply(c, 416, "", "");
        return;
    }

    // 手动构造二进制响应（mg_http_reply 的 printf 对二进制不安全）。
    mg_printf(c, "HTTP/1.1 206 Partial Content\r\n");
    mg_printf(c, "Accept-Ranges: bytes\r\n");
    mg_printf(c, "Content-Type: application/octet-stream\r\n");
    mg_printf(c, "Content-Range: bytes %llu-%llu/%llu\r\n",
              static_cast<unsigned long long>(start),
              static_cast<unsigned long long>(start + rd - 1),
              static_cast<unsigned long long>(size));
    mg_printf(c, "Content-Length: %d\r\n", rd);
    mg_printf(c, "Connection: close\r\n");
    mg_printf(c, "\r\n");
    mg_send(c, buf.data(), static_cast<size_t>(rd));
    c->is_draining = 1;
}

}  // namespace miniapp
