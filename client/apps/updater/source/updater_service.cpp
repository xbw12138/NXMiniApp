//
// updater: 拉取容器 NRO、替换本地容器、上传容器日志。
// 网络与文件操作全部是阻塞调用，由视图层放工作线程执行。
//

#include "updater/updater_service.hpp"

#include <borealis/core/logger.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <nxkit/config.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

namespace updater {

namespace {

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

bool installRelease(const ReleaseInfo& release,
                    const std::function<void(long long, long long)>& progress,
                    std::string& error) {
    std::remove(kNewNroPath);

    std::FILE* file = std::fopen(kNewNroPath, "wb");
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
        std::remove(kNewNroPath);
        error = "初始化网络失败";
        return false;
    }
    applyCommonOptions(curl);
    curl_easy_setopt(curl, CURLOPT_URL, release.downloadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    // 大文件下载给足超时。
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    std::fflush(file);
    std::fclose(file);

    if (code != CURLE_OK || ctx.writeError) {
        std::remove(kNewNroPath);
        error = ctx.writeError ? "写入 SD 卡失败" : std::string("下载失败：") + curl_easy_strerror(code);
        return false;
    }
    if (status != 200) {
        std::remove(kNewNroPath);
        error = "服务器返回 " + std::to_string(status);
        return false;
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

}  // namespace updater
