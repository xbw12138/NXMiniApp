//
// updater: 拉取容器 NRO、替换本地容器、上传容器日志。
// 网络与文件操作全部是阻塞调用，由视图层放工作线程执行。
//

#include "updater/updater_service.hpp"

#include <borealis/core/logger.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <nxkit/config.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>

#include <pthread.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>

namespace updater {

namespace {

void applyCommonOptions(CURL* curl);

constexpr const char* kNroPath = "sdmc:/switch/miniapp.nro";
constexpr const char* kNewNroPath = "sdmc:/switch/miniapp.nro.new";
constexpr const char* kOldNroPath = "sdmc:/switch/miniapp.nro.old";
constexpr const char* kLogPath = "sdmc:/switch/miniapp/miniapp.log";
constexpr const char* kDefaultServer = "http://ecfun.cc";
// 后台单次接收上限 8MB，超出时只截取末尾。
constexpr std::size_t kMaxLogBytes = 8 << 20;

struct WriteContext {
    std::FILE* file = nullptr;
    std::function<void(long long, long long)> progress;
    long long lastReported = 0;
    bool writeError = false;
};

// 多段并行下载时共享的进度统计。
struct ProgressState {
    std::atomic<long long> done{0};
    std::atomic<long long> lastReported{0};
    std::function<void(long long, long long)> progress;
    long long total = 0;
};

size_t writeToFile(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    const size_t bytes = size * nmemb;
    if (ctx->file && std::fwrite(data, 1, bytes, ctx->file) != bytes) {
        ctx->writeError = true;
        return 0;
    }
    return bytes;
}

int reportProgress(void* userdata, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    if (!ctx->progress) return 0;
    // 每 1MB 汇报一次，避免把 UI 线程刷爆。
    if (now - ctx->lastReported < (1 << 20) && now != total) return 0;
    ctx->lastReported = now;
    ctx->progress(static_cast<long long>(now), static_cast<long long>(total));
    return 0;
}

// 并行下载的一段。
struct PartTask {
    std::string url;
    std::string path;
    long long start = 0;
    long long end = 0;  // 含
    bool ok = false;
    bool writeError = false;
    long long bytes = 0;
    std::string error;
    std::FILE* file = nullptr;
    ProgressState* progress = nullptr;
};

size_t writePartTracked(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* task = static_cast<PartTask*>(userdata);
    const size_t bytes = size * nmemb;
    // 必须真正落盘，否则分段文件是空的，校验长度时必然失败。
    if (task->file && std::fwrite(data, 1, bytes, task->file) != bytes) {
        task->writeError = true;
        return 0;
    }
    if (!task->progress) return bytes;
    const long long total = task->progress->done.fetch_add(static_cast<long long>(bytes)) +
                            static_cast<long long>(bytes);
    long long last = task->progress->lastReported.load();
    if (total - last >= (1 << 20) &&
        task->progress->lastReported.compare_exchange_strong(last, total)) {
        task->progress->progress(total, task->progress->total);
    }
    return bytes;
}

bool partOnce(PartTask* task, std::string& error) {
    task->file = std::fopen(task->path.c_str(), "wb");
    if (!task->file) {
        error = "无法写入 SD 卡";
        return false;
    }
    const std::string range = std::to_string(task->start) + "-" + std::to_string(task->end);
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(task->file);
        task->file = nullptr;
        error = "初始化网络失败";
        return false;
    }
    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, task->url.c_str());
    curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writePartTracked);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, task);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    std::fflush(task->file);
    const long long written = std::ftell(task->file);
    std::fclose(task->file);
    task->file = nullptr;

    if (code != CURLE_OK) {
        error = std::string("下载失败：") + curl_easy_strerror(code);
        return false;
    }
    if (status != 206 || written != task->end - task->start + 1) {
        error = "服务器不支持分段下载";
        return false;
    }
    task->bytes = written;
    return true;
}

void* partEntry(void* raw) {
    auto* task = static_cast<PartTask*>(raw);
    // 单段失败重试一次，避免整包从头再来。
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0 && task->progress) {
            task->progress->done.fetch_sub(task->bytes);
            task->bytes = 0;
        }
        task->writeError = false;
        if (partOnce(task, task->error)) {
            task->ok = true;
            return nullptr;
        }
    }
    return nullptr;
}

bool concatParts(const std::vector<std::string>& parts, const std::string& out, std::string& error) {
    std::FILE* dst = std::fopen(out.c_str(), "wb");
    if (!dst) {
        error = "无法写入 SD 卡";
        return false;
    }
    std::vector<char> buffer(256 * 1024);
    for (const auto& part : parts) {
        std::FILE* src = std::fopen(part.c_str(), "rb");
        if (!src) {
            std::fclose(dst);
            error = "下载分片丢失";
            return false;
        }
        size_t read = 0;
        while ((read = std::fread(buffer.data(), 1, buffer.size(), src)) > 0) {
            if (std::fwrite(buffer.data(), 1, read, dst) != read) {
                std::fclose(src);
                std::fclose(dst);
                error = "写入 SD 卡失败";
                return false;
            }
        }
        std::fclose(src);
    }
    std::fflush(dst);
    std::fclose(dst);
    return true;
}

// 分段并行下载。ecfun.cc 单连接大约只有 45KB/s，但多连接可以叠加，
// 4 段并行实测能把 37MB 的包从十几分钟压到两分钟左右。
bool downloadParallel(const std::string& url, const std::string& out, long long size,
                      const std::function<void(long long, long long)>& progress,
                      std::string& error) {
    constexpr int kParts = 8;
    constexpr const char* kPartDir = "sdmc:/switch/miniapp/parts";
    mkdir(kPartDir, 0777);

    ProgressState state;
    state.progress = progress;
    state.total = size;

    std::vector<PartTask> tasks(kParts);
    long long chunk = (size + kParts - 1) / kParts;
    for (int i = 0; i < kParts; ++i) {
        PartTask& task = tasks[i];
        task.url = url;
        task.path = std::string(kPartDir) + "/part" + std::to_string(i);
        task.start = chunk * i;
        task.end = std::min(size - 1, chunk * (i + 1) - 1);
        task.progress = &state;
        std::remove(task.path.c_str());
    }

    std::vector<pthread_t> threads(kParts);
    for (int i = 0; i < kParts; ++i) {
        if (pthread_create(&threads[i], nullptr, partEntry, &tasks[i]) != 0) {
            for (int j = 0; j < i; ++j) pthread_join(threads[j], nullptr);
            error = "创建下载线程失败";
            return false;
        }
    }
    for (int i = 0; i < kParts; ++i) pthread_join(threads[i], nullptr);

    bool ok = true;
    for (const auto& task : tasks) {
        if (!task.ok) {
            ok = false;
            if (error.empty()) error = task.error;
        }
    }
    std::vector<std::string> paths;
    for (const auto& task : tasks) paths.push_back(task.path);
    if (!ok) {
        for (const auto& part : paths) std::remove(part.c_str());
        return false;
    }
    if (progress) progress(state.done.load(), size);
    const bool merged = concatParts(paths, out, error);
    for (const auto& part : paths) std::remove(part.c_str());
    return merged;
}

std::string trimTrailingSlash(std::string url) {
    while (!url.empty() && (url.back() == '/' || url.back() == ' ')) url.pop_back();
    if (!url.empty() && url.find("://") == std::string::npos) url = "http://" + url;
    return url;
}

void applyCommonOptions(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXMiniApp-Updater/1.0");
#ifdef __SWITCH__
    if (std::string(serverUrl()).rfind("https://", 0) == 0) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
    }
#endif
}

size_t appendToString(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

std::string absoluteUrl(const std::string& path) {
    if (path.empty()) return "";
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) return path;
    if (path[0] == '/') return serverUrl() + path;
    return serverUrl() + "/" + path;
}

std::string humanize(long long bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024 * 1024));
    return buffer;
}

}  // namespace

std::string serverUrl() {
    const char* env = std::getenv("NXKIT_MINIAPP_SERVER");
    if (env && *env) return trimTrailingSlash(env);
    nxkit::Config& config = nxkit::Config::instance();
    std::string value = config.getString("miniapp.serverUrl", "");
    if (value.empty()) value = config.getString("store.serverUrl", "");
    if (value.empty()) value = kDefaultServer;
    return trimTrailingSlash(value);
}

std::string deviceId() {
    nxkit::Config& config = nxkit::Config::instance();
    std::string id = config.getString("miniapp.device_id", "");
    if (id.empty()) {
        std::random_device seed;
        std::mt19937_64 engine(static_cast<std::mt19937_64::result_type>(seed()));
        char buffer[20];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(engine()));
        id = buffer;
        config.setString("miniapp.device_id", id);
        config.save();
    }
    return id;
}

bool fetchReleases(std::vector<ReleaseInfo>& out, std::string& error) {
    out.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "初始化网络失败";
        return false;
    }
    const std::string url = serverUrl() + "/api/v1/container/releases";
    std::string body;
    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        error = std::string("网络错误：") + curl_easy_strerror(code);
        return false;
    }
    if (status != 200) {
        error = "服务器返回 " + std::to_string(status);
        return false;
    }
    try {
        const auto json = nlohmann::json::parse(body);
        for (const auto& item : json.at("items")) {
            ReleaseInfo release;
            release.id = item.value("id", 0LL);
            release.version = item.value("version", "");
            release.notes = item.value("notes", "");
            release.size = item.value("size", 0LL);
            release.createdAt = item.value("created_at", 0LL);
            release.downloadUrl = absoluteUrl(item.value("download_url", ""));
            if (release.id > 0 && !release.downloadUrl.empty()) out.push_back(release);
        }
    } catch (const std::exception& e) {
        error = std::string("解析版本列表失败：") + e.what();
        return false;
    }
    if (out.empty()) {
        error = "后台还没有已发布的容器版本";
        return false;
    }
    return true;
}

long long installedNroSize() {
    std::FILE* file = std::fopen(kNroPath, "rb");
    if (!file) return -1;
    std::fseek(file, 0, SEEK_END);
    const long long size = std::ftell(file);
    std::fclose(file);
    return size;
}

bool downloadSingle(const std::string& url, const std::string& out,
                    const std::function<void(long long, long long)>& progress,
                    std::string& error) {
    std::FILE* file = std::fopen(out.c_str(), "wb");
    if (!file) {
        error = "无法写入 SD 卡（磁盘满或写保护）";
        return false;
    }
    WriteContext ctx;
    ctx.file = file;
    ctx.progress = progress;

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        error = "初始化网络失败";
        return false;
    }
    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    std::fflush(file);
    std::fclose(file);

    if (code != CURLE_OK || ctx.writeError) {
        error = ctx.writeError ? "写入 SD 卡失败" : std::string("下载失败：") + curl_easy_strerror(code);
        return false;
    }
    if (status != 200) {
        error = "服务器返回 " + std::to_string(status);
        return false;
    }
    return true;
}

bool installRelease(const ReleaseInfo& release,
                    const std::function<void(long long, long long)>& progress,
                    std::string& error) {
    std::remove(kNewNroPath);

    // 已知总大小时走多段并行；单连接在 ecfun.cc 上只有 ~45KB/s。
    bool parallelOk = false;
    if (release.size > 4 * 1024 * 1024) {
        parallelOk = downloadParallel(release.downloadUrl, kNewNroPath, release.size,
                                      progress, error);
    }
    if (!parallelOk) {
        std::remove(kNewNroPath);
        std::string singleError;
        if (!downloadSingle(release.downloadUrl, kNewNroPath, progress, singleError)) {
            std::remove(kNewNroPath);
            if (error.empty()) error = singleError;
            return false;
        }
        error.clear();
    }

    std::FILE* check = std::fopen(kNewNroPath, "rb");
    if (!check) {
        error = "下载文件不可读";
        return false;
    }
    std::fseek(check, 0, SEEK_END);
    const long long downloaded = std::ftell(check);
    std::fclose(check);
    if (release.size > 0 && downloaded != release.size) {
        std::remove(kNewNroPath);
        error = "文件大小不符（" + humanize(downloaded) + " / " + humanize(release.size) + "）";
        return false;
    }

    // 先备份旧容器，替换失败还能还原，避免出现“两边都没了”的情况。
    std::remove(kOldNroPath);
    const bool hadOld = std::rename(kNroPath, kOldNroPath) == 0;
    if (std::rename(kNewNroPath, kNroPath) != 0) {
        if (hadOld) std::rename(kOldNroPath, kNroPath);
        std::remove(kNewNroPath);
        error = "替换 miniapp.nro 失败（文件被占用？请先退出容器）";
        return false;
    }
    std::remove(kOldNroPath);
    // 记录版本，商店里才能准确判断是否可更新。
    std::ofstream versionFile("sdmc:/switch/miniapp.nro.version",
                              std::ios::binary | std::ios::trunc);
    versionFile << release.version;
    versionFile.close();
    brls::Logger::info("updater: installed version {} ({} bytes)", release.version, downloaded);
    return true;
}

bool uploadLog(std::string& error) {
    std::ifstream input(kLogPath, std::ios::binary);
    if (!input) {
        error = "没有找到 miniapp.log（容器还没运行过？）";
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    if (data.empty()) {
        error = "日志是空的";
        return false;
    }
    if (data.size() > kMaxLogBytes) {
        data = data.substr(data.size() - kMaxLogBytes);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "初始化网络失败";
        return false;
    }
    applyCommonOptions(curl);
    const std::string url = serverUrl() + "/api/v1/device/logs";
    const std::string device = deviceId();

    curl_mime* form = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(form);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "miniapp.log");
    curl_mime_type(part, "text/plain");
    curl_mime_data(part, data.data(), data.size());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-Device-Id: " + device).c_str());

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_mime_free(form);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        error = std::string("上传失败：") + curl_easy_strerror(code);
        return false;
    }
    if (status != 200) {
        error = "服务器返回 " + std::to_string(status);
        return false;
    }
    brls::Logger::info("updater: uploaded log {} bytes for device {}", data.size(), device);
    return true;
}

// ---------------- 原生 NRO 应用商店 ----------------

namespace {

constexpr const char* kSwitchDir = "sdmc:/switch";
constexpr const char* kStoreIconDir = "sdmc:/switch/miniapp/store-icons";

std::string safeName(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        out += ok ? c : '_';
    }
    return out.empty() ? "app" : out;
}

std::string nroPath(const std::string& id) {
    return std::string(kSwitchDir) + "/" + safeName(id) + ".nro";
}

std::string versionPath(const std::string& id) { return nroPath(id) + ".version"; }

bool fileSize(const std::string& path, long long& size) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    size = static_cast<long long>(info.st_size);
    return true;
}

void makeDirs(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); i++) {
        current += path[i];
        if (path[i] != '/' || i + 1 >= path.size()) continue;
        if (current.size() > 2 && current[current.size() - 2] == ':') continue;
        mkdir(current.c_str(), 0777);
    }
    mkdir(path.c_str(), 0777);
}

std::string iconCachePath(const std::string& id, const std::string& iconUrl) {
    std::string ext = ".png";
    const size_t dot = iconUrl.find_last_of('.');
    if (dot != std::string::npos) {
        std::string candidate = iconUrl.substr(dot);
        const size_t cut = candidate.find_first_of("?#");
        if (cut != std::string::npos) candidate = candidate.substr(0, cut);
        for (char& c : candidate) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (candidate == ".png" || candidate == ".jpg" || candidate == ".jpeg") ext = candidate;
    }
    return std::string(kStoreIconDir) + "/" + safeName(id) + ext;
}

}  // namespace

std::string installedNroPath(const std::string& id) {
    const std::string path = nroPath(id);
    long long size = 0;
    if (!fileSize(path, size) || size <= 0) return "";
    return path;
}

std::string installedNroVersion(const std::string& id) {
    if (installedNroPath(id).empty()) return "";
    std::ifstream versionFile(versionPath(id), std::ios::binary);
    std::string current;
    if (!versionFile || !std::getline(versionFile, current)) return "-";
    return current;
}

long long installedNroSize(const std::string& id) {
    long long size = 0;
    if (!fileSize(nroPath(id), size)) return -1;
    return size;
}

bool fetchStoreApps(std::vector<StoreAppInfo>& out, std::string& error) {
    out.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "初始化网络失败";
        return false;
    }
    const std::string url =
        serverUrl() + "/api/v1/apps?page_size=100&sort=updated_at";
    std::string body;
    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        error = std::string("网络错误：") + curl_easy_strerror(code);
        return false;
    }
    if (status != 200) {
        error = "服务器返回 " + std::to_string(status);
        return false;
    }
    try {
        const auto json = nlohmann::json::parse(body);
        for (const auto& item : json.at("items")) {
            StoreAppInfo app;
            app.id = item.value("id", "");
            app.title = item.value("title", "");
            app.summary = item.value("summary", "");
            app.description = item.value("description", "");
            app.category = item.value("category", "");
            app.developer = item.value("developer", "");
            app.iconUrl = absoluteUrl(item.value("icon_url", ""));
            app.featured = item.value("featured", false);
            app.downloads = item.value("downloads", 0LL);
            if (item.contains("latest_version") && item["latest_version"].is_object()) {
                const auto& version = item["latest_version"];
                app.version = version.value("version", "");
                app.size = version.value("size", 0LL);
            }
            if (!app.id.empty()) out.push_back(app);
        }
    } catch (const std::exception& e) {
        error = std::string("解析商店列表失败：") + e.what();
        return false;
    }
    return true;
}

bool installStoreApp(const StoreAppInfo& app,
                     const std::function<void(long long, long long)>& progress,
                     std::string& error) {
    makeDirs(kSwitchDir);
    const std::string target = nroPath(app.id);
    const std::string tmp = target + ".download";
    std::remove(tmp.c_str());

    const std::string url =
        serverUrl() + "/api/v1/apps/" + app.id + "/download?version=latest";
    // 大文件走多段并行，直连 ecfun.cc 单连接速度很差。
    bool ok = false;
    if (app.size > 4 * 1024 * 1024) {
        ok = downloadParallel(url, tmp, app.size, progress, error);
    }
    if (!ok) {
        std::remove(tmp.c_str());
        std::string singleError;
        if (!downloadSingle(url, tmp, progress, singleError)) {
            std::remove(tmp.c_str());
            if (error.empty()) error = singleError;
            return false;
        }
        error.clear();
    }

    long long downloaded = 0;
    if (!fileSize(tmp, downloaded) || downloaded <= 0) {
        std::remove(tmp.c_str());
        error = "下载文件为空";
        return false;
    }
    if (app.size > 0 && downloaded != app.size) {
        std::remove(tmp.c_str());
        error = "文件大小不符（" + humanize(downloaded) + " / " + humanize(app.size) + "）";
        return false;
    }

    // 先备份旧版本，替换失败还能还原。
    const std::string oldTarget = target + ".old";
    std::remove(oldTarget.c_str());
    const bool hadOld = std::rename(target.c_str(), oldTarget.c_str()) == 0;
    if (std::rename(tmp.c_str(), target.c_str()) != 0) {
        if (hadOld) std::rename(oldTarget.c_str(), target.c_str());
        std::remove(tmp.c_str());
        error = "写入 /switch 失败";
        return false;
    }
    std::remove(oldTarget.c_str());
    std::ofstream versionFile(versionPath(app.id), std::ios::binary | std::ios::trunc);
    versionFile << app.version;
    versionFile.close();
    brls::Logger::info("updater: installed nro {} {} ({} bytes)", app.id, app.version, downloaded);
    return true;
}

bool removeStoreApp(const std::string& id, std::string& error) {
    const std::string target = nroPath(id);
    long long size = 0;
    if (!fileSize(target, size)) {
        error = "未安装";
        return false;
    }
    if (std::remove(target.c_str()) != 0) {
        error = "删除失败（文件正在使用？请先退出该应用）";
        return false;
    }
    std::remove(versionPath(id).c_str());
    brls::Logger::info("updater: removed nro {}", id);
    return true;
}

std::string fetchIcon(const std::string& id, const std::string& iconUrl) {
    if (iconUrl.empty()) return "";
    makeDirs(kStoreIconDir);
    const std::string path = iconCachePath(id, iconUrl);
    long long size = 0;
    if (fileSize(path, size) && size > 0) return path;

    std::string error;
    if (!downloadSingle(iconUrl, path, nullptr, error)) {
        std::remove(path.c_str());
        brls::Logger::warning("updater: icon {} failed: {}", id, error);
        return "";
    }
    return path;
}

}  // namespace updater
