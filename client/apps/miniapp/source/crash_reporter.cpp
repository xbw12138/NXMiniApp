//
// crash_reporter: 崩溃日志留存与上报（实现）。
//

#include "miniapp/crash_reporter.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

#ifdef __SWITCH__
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include <borealis/core/thread.hpp>
#include <nxkit/http_client.hpp>
#endif

namespace miniapp::crash {

namespace {

// 内存里的日志环形缓冲：进程内始终可读，不依赖 SD 卡是否已落盘。
constexpr std::size_t kLogRingLines = 1200;
constexpr std::size_t kLogRingLineMax = 256;

std::mutex& logRingMutex() {
    static std::mutex mutex;
    return mutex;
}

std::deque<std::string>& logRing() {
    static std::deque<std::string> ring;
    return ring;
}

std::string snapshotLog() {
    std::lock_guard<std::mutex> lock(logRingMutex());
    std::string out;
    for (const auto& line : logRing()) {
        out += line;
        out += '\n';
    }
    return out;
}

#ifdef __SWITCH__

// Switch 上会话运行标记：存在即代表上次没有走正常退出流程。
constexpr const char* kRunningFlag = "sdmc:/switch/miniapp/session.running";
// 致命信号处理器只写这一行，进程随后按默认动作退出。
constexpr const char* kSignalFile = "sdmc:/switch/miniapp/session.crash";
// 上一轮日志与崩溃日志留档目录。
constexpr const char* kPendingDir = "sdmc:/switch/miniapp/pending";
constexpr const char* kLogFile = "sdmc:/switch/miniapp/miniapp.log";

constexpr long kMaxUploadBytes = 1L << 20;
constexpr std::size_t kMaxPending = 5;

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}

bool readTail(const std::string& path, long maxBytes, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size <= 0) return false;
    if (size > maxBytes) {
        file.seekg(size - maxBytes, std::ios::beg);
        out = "[... 日志过长，仅保留末尾部分 ...]\n";
    } else {
        file.seekg(0, std::ios::beg);
    }
    out.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();
    return true;
}

std::vector<std::filesystem::path> pendingFiles() {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(kPendingDir, error)) {
        if (error) break;
        if (!entry.is_regular_file(error)) continue;
        if (entry.path().extension() != ".log") continue;
        files.push_back(entry.path());
    }
    // 文件名带时间戳，字典序即时间序；只用最新的若干个，避免刷爆磁盘。
    std::sort(files.begin(), files.end());
    if (files.size() > kMaxPending) {
        for (std::size_t i = 0; i < files.size() - kMaxPending; ++i) {
            std::filesystem::remove(files[i], error);
        }
        files.erase(files.begin(), files.end() - static_cast<std::ptrdiff_t>(kMaxPending));
    }
    return files;
}

// 把上一轮的日志转存成待上传的崩溃日志。进程崩溃时来不及写的收尾信息
// （版本、信号、时间）在这里补齐。
void preserveCrashLog(const std::string& version) {
    std::string body;
    if (!readTail(kLogFile, kMaxUploadBytes, body)) body = "[日志为空]\n";

    std::string header = "[crash] 上次运行未正常退出，已转存本机日志\n";
    header += "version=" + version + "\n";
    header += "detected_at=" + timestamp() + "\n";

    std::string signalText;
    if (readTail(kSignalFile, 4096, signalText)) header += signalText;
    header += "applet_type=" + std::to_string(static_cast<int>(appletGetAppletType())) + "\n";
    header += "---------------- log tail ----------------\n";

    std::string path = std::string(kPendingDir) + "/crash-" + timestamp() + ".log";
    // 同一秒内二次崩溃时时间戳会重复，补一个序号避免覆盖。
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10; ++suffix) {
        path = std::string(kPendingDir) + "/crash-" + timestamp() + "-" +
               std::to_string(suffix) + ".log";
    }
    writeFile(path, header + body);
    unlink(kSignalFile);
}

// 只做异步信号安全的事：整数转字符串 + open/write/close。
void writeSignalMarker(int sig) {
    char buffer[32];
    char digits[12];
    int count = 0;
    int value = sig;
    if (value <= 0) {
        digits[count++] = '0';
    } else {
        while (value > 0 && count < static_cast<int>(sizeof(digits))) {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        }
    }
    const char prefix[] = "signal=";
    std::size_t length = 0;
    for (const char* p = prefix; *p != '\0'; ++p) buffer[length++] = *p;
    while (count > 0) buffer[length++] = digits[--count];
    buffer[length++] = '\n';

    int fd = open(kSignalFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        ssize_t written = write(fd, buffer, length);
        (void)written;
        close(fd);
    }
}

void handleFatalSignal(int sig) {
    writeSignalMarker(sig);
    // 交还默认处理，保证系统仍能生成真实的崩溃报告。
    signal(sig, SIG_DFL);
    raise(sig);
}

void installSignalHandlers() {
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (int sig : signals) signal(sig, handleFatalSignal);
}

std::string multipartBody(const std::string& boundary, const std::string& version,
                          const std::string& fileName, const std::string& content) {
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"version\"\r\n\r\n";
    body += version + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + fileName + "\"\r\n";
    body += "Content-Type: text/plain\r\n\r\n";
    body += content;
    body += "\r\n--" + boundary + "--\r\n";
    return body;
}

void uploadPending(const std::string& baseUrl, const std::string& version,
                   const std::string& deviceId) {
    auto files = pendingFiles();
    if (files.empty()) return;

    nxkit::HttpClient http;
    http.setBaseUrl(baseUrl);
    http.setHeader("X-Device-Id", deviceId);
    http.setTimeout(20000);

    for (const auto& path : files) {
        std::string content;
        if (!readTail(path.string(), kMaxUploadBytes, content)) continue;
        const std::string boundary = "----nxminiappcrash7f3a";
        std::string body = multipartBody(boundary, version, path.filename().string(), content);
        auto result = http.request("POST", "/api/v1/device/logs", body,
                                   "multipart/form-data; boundary=" + boundary, 20000);
        if (result.ok) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
}

struct UploadJob {
    std::string baseUrl;
    std::string version;
    std::string deviceId;
};

void* runUploader(void* raw) {
    auto* job = static_cast<UploadJob*>(raw);
    const std::string baseUrl = job->baseUrl;
    const std::string version = job->version;
    const std::string deviceId = job->deviceId;
    delete job;

    // 启动阶段网络栈/GL 都还在初始化，等界面稳定后再补传，最多试三轮。
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(attempt == 0 ? 5 : 10));
        uploadPending(baseUrl, version, deviceId);
    }
    return nullptr;
}

struct ReportJob {
    std::string baseUrl;
    std::string version;
    std::string deviceId;
    std::string reason;
    std::function<void(bool, const std::string&)> callback;
};

void* runReporter(void* raw) {
    std::unique_ptr<ReportJob> job(static_cast<ReportJob*>(raw));

    std::string body = snapshotLog();
    std::size_t lines = body.empty() ? 0 : static_cast<std::size_t>(std::count(body.begin(), body.end(), '\n'));
    if (body.empty() && !readTail(kLogFile, kMaxUploadBytes, body)) body = "[日志为空]\n";
    std::string header = "[manual] 用户主动上报日志\n";
    header += "version=" + job->version + "\n";
    header += "device=" + job->deviceId + "\n";
    header += "reason=" + job->reason + "\n";
    header += "uploaded_at=" + timestamp() + "\n";
    header += "memory_lines=" + std::to_string(lines) + "\n";
    header += "---------------- log tail ----------------\n";

    bool ok = false;
    std::string message;
    if (job->baseUrl.empty()) {
        message = "未配置服务器地址";
    } else {
        nxkit::HttpClient http;
        http.setBaseUrl(job->baseUrl);
        http.setHeader("X-Device-Id", job->deviceId);
        http.setTimeout(20000);

        const std::string boundary = "----nxminiappmanual7f3a";
        std::string payload =
            multipartBody(boundary, job->version, "session-" + timestamp() + ".log", header + body);
        auto result = http.request("POST", "/api/v1/device/logs", payload,
                                   "multipart/form-data; boundary=" + boundary, 20000);
        ok = result.ok;
        if (ok) {
            message = "日志已上传";
        } else if (result.status > 0) {
            message = "上传失败（HTTP " + std::to_string(result.status) + "）";
        } else {
            message = "上传失败：" + (result.error.empty() ? "网络错误" : result.error);
        }
    }

    if (job->callback) {
        auto callback = job->callback;
        brls::sync([callback, ok, message] { callback(ok, message); });
    }
    return nullptr;
}

#endif  // __SWITCH__

}  // namespace

void beginSession(const std::string& version) {
#ifdef __SWITCH__
    mkdir("sdmc:/switch/miniapp", 0777);
    mkdir(kPendingDir, 0777);

    struct stat info {};
    const bool crashed = stat(kRunningFlag, &info) == 0;
    if (crashed) preserveCrashLog(version);

    // 标记必须先于日志打开写入：setupFileLog() 会清空本轮日志文件。
    writeFile(kRunningFlag, "version=" + version + "\nstarted_at=" + timestamp() + "\n");
    installSignalHandlers();
#else
    (void)version;
#endif
}

void endSession() {
#ifdef __SWITCH__
    unlink(kRunningFlag);
    unlink(kSignalFile);
#endif
}

void startUploader(const std::string& baseUrl, const std::string& version,
                   const std::string& deviceId) {
#ifdef __SWITCH__
    auto* job = new (std::nothrow) UploadJob{baseUrl, version, deviceId};
    if (!job) return;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    pthread_t thread;
    if (pthread_create(&thread, &attr, runUploader, job) != 0) {
        delete job;
    }
    pthread_attr_destroy(&attr);
#else
    (void)baseUrl;
    (void)version;
    (void)deviceId;
#endif
}

void reportCurrentLog(const std::string& baseUrl, const std::string& version,
                      const std::string& deviceId, const std::string& reason,
                      std::function<void(bool ok, const std::string& message)> callback) {
#ifdef __SWITCH__
    auto* job = new (std::nothrow) ReportJob();
    if (!job) {
        if (callback) callback(false, "内存不足，无法上报");
        return;
    }
    job->baseUrl = baseUrl;
    job->version = version;
    job->deviceId = deviceId;
    job->reason = reason;
    job->callback = std::move(callback);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    pthread_t thread;
    if (pthread_create(&thread, &attr, runReporter, job) != 0) {
        pthread_attr_destroy(&attr);
        auto failed = job->callback;
        delete job;
        if (failed) failed(false, "无法创建上报线程");
        return;
    }
    pthread_attr_destroy(&attr);
#else
    (void)baseUrl;
    (void)version;
    (void)deviceId;
    (void)reason;
    if (callback) callback(false, "仅 Switch 支持日志上报");
#endif
}

void appendLogLine(const std::string& line) {
    if (line.empty()) return;
    std::lock_guard<std::mutex> lock(logRingMutex());
    auto& ring = logRing();
    ring.push_back(line.size() > kLogRingLineMax ? line.substr(0, kLogRingLineMax) : line);
    if (ring.size() > kLogRingLines) ring.pop_front();
}

}  // namespace miniapp::crash
