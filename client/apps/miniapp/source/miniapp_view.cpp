//
// miniapp: WXML → 渲染树 → Yoga 布局 → NanoVG 绘制，并驱动 JS 运行时。
//

#include "miniapp/miniapp_view.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/font.hpp>
#include <borealis/core/ime.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <nxkit/http_client.hpp>
#include <nxkit/paths.hpp>
#include <nxkit/media/video_view.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <new>
#include <pthread.h>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <stb_image.h>

#ifdef __SWITCH__
#include <malloc.h>
#endif

#include "miniapp/api/miniapp_api.hpp"
#include "miniapp/install/package_manager.hpp"
#include "miniapp/js_runtime.hpp"
#include "miniapp/layout.hpp"
#include "miniapp/media_library.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "miniapp/smb_manager.hpp"
#include "miniapp/wxml_parser.hpp"
#include "miniapp/wxss.hpp"
#include "nxkit/config.hpp"

namespace miniapp {

namespace {

// rpx 基准：750 设计宽映射到 640 逻辑宽。
constexpr float kRpxDesignWidth = 750.0f;
constexpr float kRpxBaseWidth = 640.0f;

const Rgba kAccent{0.059f, 0.863f, 0.471f, 1.0f};
const Rgba kTextPrimary{0.94f, 0.95f, 0.96f, 1.0f};
const Rgba kTextMuted{0.55f, 0.58f, 0.62f, 1.0f};

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string dirName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

/// rich-text 的 nodes 目前按纯文本降级：剥离标签并还原常见实体。
std::string stripTags(const std::string& html) {
    std::string out;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (c == '>' && inTag) {
            inTag = false;
            continue;
        }
        if (!inTag) out.push_back(c);
    }
    struct Entity {
        const char* text;
        const char* value;
    };
    static const Entity kEntities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&nbsp;", " "}, {"&ensp;", " "}, {"&emsp;", " "},
        {"<br>", "\n"}, {"<br/>", "\n"}};
    for (const auto& entity : kEntities) {
        size_t position = 0;
        while ((position = out.find(entity.text, position)) != std::string::npos) {
            out.replace(position, std::strlen(entity.text), entity.value);
            position += std::strlen(entity.value);
        }
    }
    return out;
}

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// 存储 key 可能包含 / 等文件系统敏感字符，落盘时做百分号转义。
std::string storageKeyFile(const std::string& key) {
    std::string out;
    out.reserve(key.size() + 5);
    for (unsigned char c : key) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
            out += buffer;
        }
    }
    out += ".json";
    return out;
}

std::string storageKeyFromFile(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%' && i + 2 < name.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int high = hex(name[i + 1]);
            const int low = hex(name[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>(high * 16 + low));
                i += 2;
                continue;
            }
        }
        out.push_back(name[i]);
    }
    return out;
}

bool isFullyWrappedExpression(const std::string& raw) {
    std::string value = trim(raw);
    return value.size() >= 4 && value.compare(0, 2, "{{") == 0 &&
           value.compare(value.size() - 2, 2, "}}") == 0;
}

bool isTruthy(const JsonValue& value) {
    switch (value.type()) {
        case JsonValue::Type::Null: return false;
        case JsonValue::Type::Bool: return value.asBool();
        case JsonValue::Type::Number: return value.asNumber() != 0.0;
        case JsonValue::Type::String: return !value.asString().empty();
        default: return true;
    }
}

NVGcolor toColor(const Rgba& rgba, float alpha = 1.0f) {
    return nvgRGBAf(rgba.r, rgba.g, rgba.b, rgba.a * alpha);
}

float clampRadius(float radius, float width, float height) {
    return std::max(0.0f, std::min(radius, std::min(width, height) * 0.5f));
}

float toFloat(const JsonValue& value, float fallback = 0.0f) {
    switch (value.type()) {
        case JsonValue::Type::Number: return static_cast<float>(value.asNumber());
        case JsonValue::Type::String: return static_cast<float>(std::atof(value.asString().c_str()));
        case JsonValue::Type::Bool: return value.asBool() ? 1.0f : 0.0f;
        default: return fallback;
    }
}

std::string stripQuery(const std::string& url) {
    size_t query = url.find('?');
    std::string path = query == std::string::npos ? url : url.substr(0, query);
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    return path;
}

JsonValue queryFromUrl(const std::string& url) {
    JsonValue query = JsonValue::object();
    size_t position = url.find('?');
    if (position == std::string::npos) return query;
    std::string text = url.substr(position + 1);
    std::istringstream stream(text);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t equal = pair.find('=');
        if (equal == std::string::npos) {
            query.setMember(pair, JsonValue::string(""));
        } else {
            query.setMember(pair.substr(0, equal), JsonValue::string(pair.substr(equal + 1)));
        }
    }
    return query;
}

std::string valueToTextImpl(const JsonValue& value) {
    switch (value.type()) {
        case JsonValue::Type::String: return value.asString();
        case JsonValue::Type::Bool: return value.asBool() ? "true" : "false";
        case JsonValue::Type::Number: {
            double number = value.asNumber();
            if (std::fabs(number - std::round(number)) < 1e-9 &&
                std::fabs(number) < 1e9) {
                return std::to_string(static_cast<long long>(number));
            }
            std::ostringstream stream;
            stream.precision(6);
            stream << number;
            return stream.str();
        }
        case JsonValue::Type::Array:
        case JsonValue::Type::Object:
            return toJsonString(value);
        default: return "";
    }
}

double steadyMilliseconds() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(
               steady_clock::now().time_since_epoch())
        .count();
}

std::string formatClock(double seconds) {
    if (!(seconds > 0.0)) seconds = 0.0;
    int total = static_cast<int>(seconds);
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

// ---------------------------------------------------------------------------
// 网络图片磁盘缓存：按 URL 哈希存原始字节，重启后命中直接解码，避免重复下载。
// ---------------------------------------------------------------------------
constexpr std::size_t kImageCacheLimit = 20u * 1024u * 1024u;

std::string imageCacheDir() { return nxkit::dataPath("miniapp/image_cache"); }

std::string imageCachePath(const std::string& url) {
    // FNV-1a 64bit，跨进程稳定，避免用到 std::hash 的实现差异。
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : url) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    char name[24];
    std::snprintf(name, sizeof(name), "%016llx.img",
                  static_cast<unsigned long long>(hash));
    return imageCacheDir() + "/" + name;
}

std::shared_ptr<std::string> readImageCache(const std::string& url) {
    std::ifstream file(imageCachePath(url), std::ios::binary);
    if (!file.good()) return nullptr;
    std::shared_ptr<std::string> bytes = std::make_shared<std::string>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (bytes->empty()) return nullptr;
    return bytes;
}

void trimImageCache() {
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(imageCacheDir(), error)) {
        if (error || !entry.is_regular_file(error)) continue;
        std::uintmax_t size = entry.file_size(error);
        if (error) continue;
        total += size;
        entries.emplace_back(entry.last_write_time(error), entry.path());
    }
    if (total <= kImageCacheLimit) return;
    std::sort(entries.begin(), entries.end());
    for (const auto& item : entries) {
        if (total <= kImageCacheLimit) break;
        std::uintmax_t size = std::filesystem::file_size(item.second, error);
        if (error) size = 0;
        std::filesystem::remove(item.second, error);
        total -= size;
    }
}

void writeImageCache(const std::string& url, const std::string& bytes) {
    // 多个图片可能同时完成下载，写盘与淘汰需要串行。
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    nxkit::ensureDirectory(imageCacheDir());
    std::ofstream file(imageCachePath(url), std::ios::binary | std::ios::trunc);
    if (!file.good()) return;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    trimImageCache();
}

// 图片解码线程：读盘 + stb 解码（CPU 密集）都在这里做，主线程只做 GPU 上传。
// 解码是数据加载时最容易卡住 UI 的环节，同步解码多张封面会直接掉帧。
class DecodeQueue {
public:
    static DecodeQueue& instance() {
        // 故意不析构：线程可能仍在跑，静态析构时销毁锁会导致退出阶段的数据竞争。
        static DecodeQueue* queue = new DecodeQueue();
        return *queue;
    }

    void submit(std::function<void()> task) {
        if (disabled) {
            // 线程起不来就退回同步解码：慢，但功能不缺失。
            try {
                task();
            } catch (...) {
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
        }
        ready.notify_one();
    }

private:
    DecodeQueue() {
        // 与日志线程一致：用 pthread 直接创建，避免 std::thread 在某些情况下
        // 抛异常把整个进程带走（解码线程是在绘制过程中懒启动的）。
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        int rc = pthread_create(&worker, &attr, &DecodeQueue::entry, this);
        pthread_attr_destroy(&attr);
        disabled = rc != 0;
    }

    static void* entry(void* raw) {
        static_cast<DecodeQueue*>(raw)->run();
        return nullptr;
    }

    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [this] { return !tasks.empty(); });
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            try {
                task();
            } catch (...) {
                // 单张图解码失败不能影响后续任务。
            }
        }
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> tasks;
    pthread_t worker{};
    bool disabled = false;
};

// 解码为 RGBA（调用方负责 stbi_image_free）。
unsigned char* decodeToRgba(const std::string& bytes, int& width, int& height) {
    if (bytes.empty()) return nullptr;
    return stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                 static_cast<int>(bytes.size()), &width, &height,
                                 nullptr, 4);
}

}  // namespace

MiniappView::MiniappView(std::string packagePath, std::string appTitleHint)
    : packagePathOverride(std::move(packagePath)), loadingTitle(std::move(appTitleHint)) {
    brls::Logger::info("MiniappView: ctor enter");
    rpxScale = kRpxBaseWidth / kRpxDesignWidth;
#ifdef __SWITCH__
    // Switch 平台必须用 zh-Hans 作为主字体：borealis 已为它挂好
    // 拉丁/扩展汉字/图标字体的回退链，FONT_REGULAR 不含中文字形。
    fontId = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
#else
    fontId = brls::Application::getFont(brls::FONT_REGULAR);
#endif
    if (fontId == brls::FONT_INVALID) {
        brls::Logger::warning("miniapp: primary font is not loaded, falling back to regular");
        fontId = brls::Application::getFont(brls::FONT_REGULAR);
    }

    this->setFocusable(true);
    // 小程序内部自己画绿色焦点框，隐藏 borealis 默认的蓝色高亮边框（贴着标题栏像进度条）。
    this->setHideHighlight(true);

    auto action = [this](brls::View*) {
        if (this->dispatchKeyToPage("a")) return true;
        this->handleActivate();
        return true;
    };
    this->registerAction("确认", brls::BUTTON_A, action);

    this->registerAction("返回", brls::BUTTON_B, [this](brls::View*) {
        if (this->dispatchKeyToPage("b")) return true;
        // 全屏视频优先退出全屏，不直接离开小程序。
        if (!fullscreenVideoPath.empty()) {
            exitVideoFullscreen();
            return true;
        }
        if (modal.active) {
            bool confirm = false;
            auto done = modal.done;
            modal = ModalState{};
            if (done) done(confirm);
            return true;
        }
        if (!navigateBack(1)) {
            // 根页面再按 B：内嵌模式下交给启动器弹「退出到桌面」确认，
            // 独立 Activity 模式下保持原行为直接退出本页。
            if (onRequestExit) {
                onRequestExit();
            } else {
                brls::Logger::info("MiniappView: B pressed, popActivity");
                brls::Application::popActivity();
            }
        }
        return true;
    });

    // 十字键：先派发页面按键事件，未被消费时才移动焦点。
    auto up = [this](brls::View*) {
        if (this->dispatchKeyToPage("up")) return true;
        return this->handleDirection(brls::FocusDirection::UP);
    };
    auto down = [this](brls::View*) {
        if (this->dispatchKeyToPage("down")) return true;
        return this->handleDirection(brls::FocusDirection::DOWN);
    };
    auto left = [this](brls::View*) {
        if (this->dispatchKeyToPage("left")) return true;
        return this->handleDirection(brls::FocusDirection::LEFT);
    };
    auto right = [this](brls::View*) {
        if (this->dispatchKeyToPage("right")) return true;
        return this->handleDirection(brls::FocusDirection::RIGHT);
    };
    // 摇杆（BUTTON_NAV_* 同时覆盖十字键与摇杆）：只移动焦点，
    // 不派发按键事件，避免摇杆误切台。十字键按下时交给上面的 action。
    auto navUp = [this](brls::View*) {
        if (this->isDpadHeld(brls::BUTTON_UP)) return true;
        return this->handleDirection(brls::FocusDirection::UP);
    };
    auto navDown = [this](brls::View*) {
        if (this->isDpadHeld(brls::BUTTON_DOWN)) return true;
        return this->handleDirection(brls::FocusDirection::DOWN);
    };
    auto navLeft = [this](brls::View*) {
        if (this->isDpadHeld(brls::BUTTON_LEFT)) return true;
        return this->handleDirection(brls::FocusDirection::LEFT);
    };
    auto navRight = [this](brls::View*) {
        if (this->isDpadHeld(brls::BUTTON_RIGHT)) return true;
        return this->handleDirection(brls::FocusDirection::RIGHT);
    };
    this->registerAction("上", brls::BUTTON_UP, up);
    this->registerAction("上", brls::BUTTON_NAV_UP, navUp);
    this->registerAction("下", brls::BUTTON_DOWN, down);
    this->registerAction("下", brls::BUTTON_NAV_DOWN, navDown);
    this->registerAction("左", brls::BUTTON_LEFT, left);
    this->registerAction("左", brls::BUTTON_NAV_LEFT, navLeft);
    this->registerAction("右", brls::BUTTON_RIGHT, right);
    this->registerAction("右", brls::BUTTON_NAV_RIGHT, navRight);

    // 鼠标左键 / 触摸点击：由手势识别器统一转发到渲染树命中测试。
    this->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END) {
                this->handlePointerTap(status.position.x, status.position.y);
            }
        }));

    // 包读取/解析（SD 卡 I/O + QuickJS 初始化）会阻塞主线程：先让加载页画出来，
    // 等下一帧之后再同步加载，避免打开小程序时整屏卡住没有任何反馈。
    this->pendingLoad = true;
    this->loadingStart = steadyMilliseconds();
    this->loadDelayFrames = 1;

    runLoopToken = brls::Application::getRunLoopEvent()->subscribe([this]() {
        this->tick(steadyMilliseconds());
    });
    runLoopActive = true;

    // 视频走 OpenGL 直出，只能在 NanoVG 帧结束后、交换缓冲前绘制。
    // 之后再用一帧 NanoVG 把 data-overlay 子树画到视频上层。
    postDrawToken = brls::Application::getPostDrawEvent()->subscribe([this]() {
        this->drawVideoSurface();
        this->drawVideoOverlay();
    });

    // 对接 wiliwili 风格播放器：用 MpvEvent 回调驱动状态，不轮询 mpv。
    nxkit::VideoView::instance().setStateCallback(
        [this](const nxkit::VideoState& vs) { this->onVideoState(vs); });

#ifndef __SWITCH__
    // 崩溃复现自动驾驶：NXKIT_AUTOPILOT=1 时进入后自动按 B 返回。
    static bool autopilot = std::getenv("NXKIT_AUTOPILOT") != nullptr;
    if (autopilot) {
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            brls::sync([]() { brls::Application::popActivity(); });
        }).detach();
    }
#endif
}

MiniappView::~MiniappView() {
    brls::Logger::info("MiniappView: dtor begin");
    if (aliveToken) *aliveToken = false;
    if (runLoopActive) {
        brls::Application::getRunLoopEvent()->unsubscribe(runLoopToken);
        runLoopActive = false;
    }
    brls::Application::getPostDrawEvent()->unsubscribe(postDrawToken);
    nxkit::VideoView::instance().setStateCallback(nullptr);
    brls::Logger::info("MiniappView: stopVideo");
    stopVideo();
    brls::Logger::info("MiniappView: cleanup {} page(s)", pages.size());
    for (auto& page : pages) {
        cleanupPage(*page);
    }
    pages.clear();
    brls::Logger::info("MiniappView: dtor done");
}

brls::View* MiniappView::getDefaultFocus() {
    return this;
}

void MiniappView::loadPackage() {
    const char* packageOverride = std::getenv("NXKIT_MINIAPP");
    std::string appJsonPath;
    if (!packagePathOverride.empty()) {
        appJsonPath = packagePathOverride + "/app.json";
    } else if (packageOverride && *packageOverride) {
        appJsonPath = std::string(packageOverride) + "/app.json";
    } else {
        appJsonPath = BRLS_ASSET("miniapps/demo/app.json");
    }
    packageDir = dirName(appJsonPath);

    brls::Logger::info("miniapp: loading package at {}", packageDir);

    if (!readTextFile(appJsonPath, appJsonText)) {
        errorMessage = "cannot open " + appJsonPath;
        return;
    }

    std::string parseError;
    JsonValue appJson = parseJson(appJsonText, &parseError);
    if (!parseError.empty()) {
        errorMessage = "app.json: " + parseError;
        return;
    }

    std::string firstPage = "pages/index/index";
    std::vector<std::string> declaredPages;
    if (const JsonValue* pagesValue = appJson.find("pages")) {
        if (pagesValue->isArray()) {
            for (const auto& entry : pagesValue->asArray()) {
                if (entry.isString() && !entry.asString().empty()) {
                    declaredPages.push_back(entry.asString());
                }
            }
        }
    }
    if (!declaredPages.empty()) firstPage = declaredPages.front();

    appTitle = "NX MiniApp";
    if (const JsonValue* window = appJson.find("window")) {
        if (const JsonValue* background = window->find("backgroundColor")) {
            if (background->isString()) appBackground = background->asString();
        }
        if (const JsonValue* title = window->find("navigationBarTitleText")) {
            if (title->isString()) appTitle = title->asString();
        }
    }

    std::string metaText;
    if (readTextFile(joinPath(packageDir, "meta.json"), metaText)) {
        JsonValue meta = parseJson(metaText);
        if (const JsonValue* name = meta.find("name")) {
            if (name->isString() && !name->asString().empty()) appTitle = name->asString();
        }
    }

    if (!readTextFile(joinPath(packageDir, "app.js"), appSource)) {
        appSource.clear();
    }
    if (!readTextFile(joinPath(packageDir, "app.wxss"), appWxss)) {
        appWxss.clear();
    }

    const std::size_t hash = std::hash<std::string>{}(packageDir);
    const std::string hashText = std::to_string(static_cast<unsigned long long>(hash));
    storageDir = nxkit::dataPath("miniapp/storage/" + hashText);
    storageFile = nxkit::dataPath("miniapp/storage/" + hashText + ".json");
    nxkit::ensureDirectory(storageDir);
    loadStorage();

    if (!openPage(firstPage, false)) {
        errorMessage = errorMessage.empty() ? "cannot open first page" : errorMessage;
        return;
    }
    brls::Logger::info("miniapp: package loaded, pages={}", pages.size());
}

bool MiniappView::openPage(const std::string& url, bool push) {
    std::string rawPath = stripQuery(url);
    if (rawPath.empty()) return false;
    if (rawPath.size() > 5 && rawPath.compare(rawPath.size() - 5, 5, ".wxml") == 0) {
        rawPath = rawPath.substr(0, rawPath.size() - 5);
    }

    if (push && current) {
        if (current->runtime) current->runtime->callLifecycle("onHide");
    }

    auto page = std::make_unique<PageState>();
    loadPageFiles(*page, rawPath, true);
    if (!page->loaded) {
        errorMessage = page->error;
        brls::Logger::error("miniapp: page '{}' failed: {}", rawPath, page->error);
        if (push && current && current->runtime) current->runtime->callLifecycle("onShow");
        return false;
    }

    JsonValue query = queryFromUrl(url);
    if (page->runtime) {
        page->runtime->callLifecycle("onLoad", &query);
        page->runtime->callLifecycle("onShow");
        page->data = page->runtime->data();
    }

    if (!push && current) {
        closeCurrentPage();
    }

    pages.push_back(std::move(page));
    current = pages.back().get();
    navigationTitle = current->title.empty() ? appTitle : current->title;
    current->dirty = true;
    layoutWidth = -1.0f;
    layoutHeight = -1.0f;
    focusPath.clear();
    focusInitialized = false;
    focusNodes.clear();
    lastAutoFocusPath.clear();
    restoreFocusPath.clear();
    return true;
}

void MiniappView::closeCurrentPage() {
    if (!current) return;
    stopVideo();
    if (current->runtime) {
        current->runtime->callLifecycle("onHide");
        current->runtime->callLifecycle("onUnload");
    }
    cleanupPage(*current);
    pages.pop_back();
    current = pages.empty() ? nullptr : pages.back().get();
    focusPath.clear();
    focusNodes.clear();
}

void MiniappView::loadPageFiles(PageState& page, const std::string& path, bool withRuntime) {
    page.path = path;
    page.dir = joinPath(packageDir, dirName(path));
    page.background = appBackground;
    page.title = appTitle;

    std::string wxmlPath = joinPath(packageDir, path + ".wxml");
    std::string wxssPath = joinPath(packageDir, path + ".wxss");
    std::string jsPath = joinPath(packageDir, path + ".js");

    if (!readTextFile(wxmlPath, page.wxmlText)) {
        page.error = "cannot open " + wxmlPath;
        return;
    }
    if (!readTextFile(wxssPath, page.wxssText)) {
        page.wxssText.clear();
    }
    if (!readTextFile(jsPath, page.jsText)) {
        page.error = "cannot open " + jsPath;
        return;
    }

    std::string parseError;
    page.wxmlRoot = parseWxml(page.wxmlText, &parseError);
    if (!parseError.empty()) {
        page.error = "WXML: " + parseError;
        return;
    }

    std::string styleText = appWxss;
    if (!styleText.empty() && !page.wxssText.empty()) styleText += "\n";
    styleText += page.wxssText;
    page.stylesheet = WxssStylesheet::parse(styleText, &parseError);
    if (!parseError.empty()) {
        page.error = "WXSS: " + parseError;
        return;
    }

    if (withRuntime) {
        page.runtime = std::make_unique<JsRuntime>(this, packageDir, path, appSource);
        std::string jsError;
        if (!page.runtime->initialize(page.jsText, path + ".js", jsError)) {
            page.error = "JS: " + jsError;
            return;
        }
        page.data = page.runtime->data();
    }

    page.loaded = true;
}

void MiniappView::cleanupPage(PageState& page) {
    if (page.root) {
        freeYogaTree(*page.root);
        page.root.reset();
    }
    page.runtime.reset();
    page.loaded = false;
}

void MiniappView::onDataChanged() {
    if (!current) return;
    if (current->runtime) current->data = current->runtime->data();
    current->dirty = true;
}

void MiniappView::rebuildTree(PageState& page) {
    if (page.root) {
        freeYogaTree(*page.root);
        page.root.reset();
    }
    page.error.clear();

    std::vector<std::unique_ptr<RenderNode>> roots;
    std::vector<SelectorNode> ancestors;
    std::vector<LocalValue> locals;
    buildChildren(page.wxmlRoot, inheritStyle(Style()), ancestors, locals, "", roots);

    if (roots.size() == 1) {
        page.root = std::move(roots[0]);
    } else if (!roots.empty()) {
        page.root = std::make_unique<RenderNode>();
        page.root->tag = "view";
        page.root->classes = {"page"};
        page.root->style = inheritStyle(Style());
        SelectorNode selector{"view", "", page.root->classes};
        page.stylesheet.apply(selector, ancestors, page.root->style, rpxScale);
        page.root->children = std::move(roots);
        page.root->fontSize = page.root->style.fontSize;
        page.root->lineHeight = effectiveLineHeight(page.root->style);
    }

    if (!page.root) {
        page.error = "WXML has no renderable root";
        page.dirty = false;
        return;
    }

    // app.json 的 window.backgroundColor 作为页面背景兜底。
    if (!page.root->style.hasBackground) {
        Rgba background;
        if (parseColor(page.background, background)) {
            page.root->style.hasBackground = true;
            page.root->style.background = background;
        }
    }

    // 链接父子指针并恢复滚动位置。
    std::vector<RenderNode*> stack{page.root.get()};
    while (!stack.empty()) {
        RenderNode* node = stack.back();
        stack.pop_back();
        if (node->isScrollView()) {
            auto it = page.scrollOffsets.find(node->path);
            node->scrollOffset = it == page.scrollOffsets.end() ? 0.0f : it->second;
        }
        for (auto& child : node->children) {
            child->parent = node;
            stack.push_back(child.get());
        }
    }

    page.dirty = false;
}

void MiniappView::rebuildLayout(PageState& page, float width, float height) {
    if (!page.root) return;
    if (page.root->yoga) freeYogaTree(*page.root);

    buildYogaTree(*page.root, width, height, rpxScale);
    calculateYogaTree(*page.root, width, height, brls::Application::getNVGContext(),
                      fontId);
    layoutWidth = width;
    layoutHeight = height;

    // 让首个可交互节点自动获得焦点。
    std::vector<RenderNode*> collect;
    std::vector<RenderNode*> stack{page.root.get()};
    while (!stack.empty()) {
        RenderNode* node = stack.back();
        stack.pop_back();
        if (node->style.displayNone) continue;
        if (node->isInteractive() && !node->disabled) collect.push_back(node);
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }
    focusNodes = std::move(collect);

    // autofocus：弹窗刚出现时把焦点交给它，避免摇杆还停在背后的列表上。
    // 只在 autofocus 节点发生变化时抢一次，用户随后在弹窗内移动焦点不会被拉回。
    RenderNode* autoFocus = nullptr;
    for (RenderNode* node : focusNodes) {
        if (node->autoFocus) {
            autoFocus = node;
            break;
        }
    }
    if (autoFocus) {
        // 焦点范围收敛到弹窗内部（最近的绝对定位祖先），摇杆不会跑到背后的列表。
        RenderNode* scope = nullptr;
        for (RenderNode* it = autoFocus; it; it = it->parent) {
            if (it->style.absolute) {
                scope = it;
                break;
            }
        }
        if (scope && scope->parent) {
            std::vector<RenderNode*> scoped;
            for (RenderNode* node : focusNodes) {
                for (RenderNode* it = node; it; it = it->parent) {
                    if (it == scope) {
                        scoped.push_back(node);
                        break;
                    }
                }
            }
            if (!scoped.empty()) focusNodes = std::move(scoped);
        }

        if (autoFocus->path != lastAutoFocusPath) {
            // 记住弹窗打开前的位置，关掉弹窗后还原。
            if (!focusPath.empty() && focusPath != autoFocus->path) {
                restoreFocusPath = focusPath;
            }
            lastAutoFocusPath = autoFocus->path;
            focusPath = autoFocus->path;
            focusInitialized = true;
            return;
        }
    } else {
        lastAutoFocusPath.clear();
        // 弹窗关闭：焦点回到打开前的位置（如果那一个节点还在）。
        if (!restoreFocusPath.empty()) {
            for (RenderNode* node : focusNodes) {
                if (node->path == restoreFocusPath) {
                    focusPath = restoreFocusPath;
                    break;
                }
            }
            restoreFocusPath.clear();
        }
    }

    if (!focusPath.empty()) {
        for (RenderNode* node : focusNodes) {
            if (node->path == focusPath) return;
        }
        focusPath.clear();
    }
    if (!focusNodes.empty()) {
        focusPath = focusNodes.front()->path;
        focusInitialized = true;
    }
}

void MiniappView::updateFocusList(PageState&) {
    // 焦点列表在 rebuildLayout 中生成；这里保留接口便于后续扩展。
}

// ---------------- WXML 构建 ----------------

JsonValue MiniappView::buildScope(const std::vector<LocalValue>& locals) const {
    JsonValue scope = current ? current->data : JsonValue::object();
    if (!scope.isObject()) scope = JsonValue::object();
    for (const auto& local : locals) scope.setMember(local.name, local.value);
    return scope;
}

bool MiniappView::evaluateExpression(const std::string& expression,
                                     const std::vector<LocalValue>& locals,
                                     JsonValue& out, bool* failed) {
    if (failed) *failed = false;
    if (!current || !current->runtime) {
        if (failed) *failed = true;
        return false;
    }
    std::string error;
    if (!current->runtime->evaluate(expression, buildScope(locals), out, error)) {
        brls::Logger::warning("miniapp expr '{}': {}", expression, error);
        if (failed) *failed = true;
        return false;
    }
    return true;
}

bool MiniappView::evaluateAttribute(const WxmlNode& node, const std::string& name,
                                    const std::vector<LocalValue>& locals,
                                    JsonValue& out) {
    const std::string* raw = node.attr(name);
    if (!raw) return false;
    std::string value = trim(*raw);
    if (isFullyWrappedExpression(value)) {
        return evaluateExpression(trim(value.substr(2, value.size() - 4)), locals, out);
    }
    out = JsonValue::string(interpolate(*raw, locals));
    return true;
}

bool MiniappView::conditionTruthy(const WxmlNode& node, const char* name,
                                  const std::vector<LocalValue>& locals, bool& ok) {
    ok = false;
    const std::string* raw = node.attr(name);
    if (!raw) return false;
    std::string text = trim(*raw);
    if (isFullyWrappedExpression(text)) {
        JsonValue value;
        if (!evaluateExpression(trim(text.substr(2, text.size() - 4)), locals, value)) {
            return false;
        }
        ok = true;
        return isTruthy(value);
    }
    ok = true;
    return !text.empty() && text != "false" && text != "0";
}

std::string MiniappView::interpolate(const std::string& text,
                                     const std::vector<LocalValue>& locals) {
    std::string result;
    size_t position = 0;
    while (position < text.size()) {
        size_t open = text.find("{{", position);
        if (open == std::string::npos) {
            result += text.substr(position);
            break;
        }
        result += text.substr(position, open - position);
        size_t close = text.find("}}", open + 2);
        if (close == std::string::npos) {
            result += text.substr(open);
            break;
        }
        std::string expression = trim(text.substr(open + 2, close - open - 2));
        JsonValue value;
        if (evaluateExpression(expression, locals, value)) {
            result += valueToTextImpl(value);
        }
        position = close + 2;
    }
    return result;
}

std::string MiniappView::valueToText(const JsonValue& value) const {
    return valueToTextImpl(value);
}

std::string MiniappView::attributeText(const WxmlNode& node, const std::string& name,
                                       const std::vector<LocalValue>& locals) {
    const std::string* raw = node.attr(name);
    return raw ? trim(interpolate(*raw, locals)) : std::string();
}

void MiniappView::buildChildren(const WxmlNode& node, const Style& parentStyle,
                                std::vector<SelectorNode>& ancestors,
                                const std::vector<LocalValue>& locals,
                                const std::string& path,
                                std::vector<std::unique_ptr<RenderNode>>& out) {
    bool chainActive = false;
    bool chainTaken = false;

    for (size_t index = 0; index < node.children.size(); ++index) {
        const WxmlNode& child = node.children[index];
        std::string childPath = path.empty() ? std::to_string(index)
                                            : path + "/" + std::to_string(index);

        if (child.isText()) {
            std::string text = interpolate(child.text, locals);
            text = trim(text);
            if (text.empty()) continue;

            auto render = std::make_unique<RenderNode>();
            render->tag = "#text";
            render->text = text;
            render->path = childPath;
            render->style = inheritStyle(parentStyle);
            render->fontSize = render->style.fontSize;
            render->lineHeight = effectiveLineHeight(render->style);
            out.push_back(std::move(render));
            continue;
        }

        const std::string* ifAttr = child.attr("wx:if");
        const std::string* elifAttr = child.attr("wx:elif");
        const std::string* elseAttr = child.attr("wx:else");

        bool visible = true;
        if (ifAttr) {
            bool ok = false;
            visible = conditionTruthy(child, "wx:if", locals, ok);
            chainActive = true;
            chainTaken = visible;
        } else if (elifAttr && chainActive) {
            if (chainTaken) {
                visible = false;
            } else {
                bool ok = false;
                visible = conditionTruthy(child, "wx:elif", locals, ok);
                chainTaken = chainTaken || visible;
            }
        } else if (elseAttr && chainActive) {
            visible = !chainTaken;
            chainTaken = true;
        } else {
            chainActive = false;
        }

        if (!visible) continue;

        size_t before = out.size();
        buildElement(child, parentStyle, ancestors, locals, childPath, out, false);
        for (size_t i = before; i < out.size(); ++i) out[i]->parent = nullptr;
    }
}

void MiniappView::buildElement(const WxmlNode& node, const Style& parentStyle,
                               std::vector<SelectorNode>& ancestors,
                               const std::vector<LocalValue>& locals,
                               const std::string& path,
                               std::vector<std::unique_ptr<RenderNode>>& out,
                               bool skipFor) {
    if (!skipFor) {
        if (const std::string* forAttr = node.attr("wx:for")) {
            JsonValue list;
            if (!evaluateAttribute(node, "wx:for", locals, list)) return;
            if (!list.isArray() && !list.isObject()) return;

            std::string itemName = "item";
            if (const std::string* attr = node.attr("wx:for-item")) {
                std::string value = trim(*attr);
                if (!value.empty()) itemName = value;
            }
            std::string indexName = "index";
            if (const std::string* attr = node.attr("wx:for-index")) {
                std::string value = trim(*attr);
                if (!value.empty()) indexName = value;
            }

            size_t count = list.isArray() ? list.size() : list.asObject().size();
            auto buildItem = [&](const JsonValue& item, size_t index,
                                 const std::string& key) {
                std::vector<LocalValue> scoped = locals;
                scoped.push_back(LocalValue{itemName, item});
                scoped.push_back(LocalValue{indexName,
                                            JsonValue::number(static_cast<double>(index))});
                if (!key.empty()) scoped.push_back(LocalValue{"key", JsonValue::string(key)});
                std::string itemPath = path + "#" + std::to_string(index);
                buildElement(node, parentStyle, ancestors, scoped, itemPath, out, true);
            };

            if (list.isArray()) {
                for (size_t i = 0; i < count; ++i) buildItem(list.at(i), i, std::string());
            } else {
                size_t i = 0;
                for (const auto& entry : list.asObject()) {
                    buildItem(entry.second, i, entry.first);
                    ++i;
                }
            }
            return;
        }
    }

    if (const std::string* ifAttr = node.attr("wx:if")) {
        std::string raw = trim(*ifAttr);
        bool condition = false;
        if (raw == "true") {
            condition = true;
        } else {
            bool ok = false;
            condition = conditionTruthy(node, "wx:if", locals, ok);
        }
        if (!condition) return;
    }

    std::string tag = node.tag;
    if (tag == "block") {
        std::vector<LocalValue> scoped = locals;
        buildChildren(node, parentStyle, ancestors, scoped, path, out);
        return;
    }

    static const char* kKnownTags[] = {
        "view", "text", "image", "button", "scroll-view", "swiper", "swiper-item",
        "input", "textarea", "switch", "checkbox", "checkbox-group", "radio",
        "radio-group", "slider", "navigator", "form", "progress", "rich-text",
        "label", "icon", "video"};
    bool known = false;
    for (const char* knownTag : kKnownTags) {
        if (tag == knownTag) {
            known = true;
            break;
        }
    }
    if (!known) tag = "view";

    std::string classText = attributeText(node, "class", locals);
    std::string idText = attributeText(node, "id", locals);

    auto render = std::make_unique<RenderNode>();
    render->tag = tag;
    render->id = idText;
    render->path = path;
    std::istringstream classStream(classText);
    std::string className;
    while (classStream >> className) render->classes.push_back(className);

    SelectorNode selector{tag, idText, render->classes};
    Style style = inheritStyle(parentStyle);
    if (tag == "button") {
        style.hasColor = true;
        style.color = Rgba{0.04f, 0.10f, 0.07f, 1.0f};
    }
    if (tag == "text" || tag == "#text") {
        // 文本默认继承颜色，不额外覆盖。
    }
    if (current) current->stylesheet.apply(selector, ancestors, style, rpxScale);
    if (const std::string* inlineStyle = node.attr("style")) {
        auto declarations = parseDeclarations(interpolate(*inlineStyle, locals));
        applyDeclarations(style, declarations, rpxScale);
    }

    // 默认组件样式。
    if (tag == "button") {
        if (!style.hasBackground) {
            style.hasBackground = true;
            style.background = kAccent;
        }
        if (style.borderRadius <= 0.0f) style.borderRadius = 44.0f * rpxScale;
    } else if (tag == "input" || tag == "textarea") {
        if (!style.hasBackground) {
            style.hasBackground = true;
            style.background = Rgba{0.12f, 0.13f, 0.15f, 1.0f};
            style.borderRadius = 12.0f * rpxScale;
        }
        if (style.padding.left <= 0.0f) {
            style.padding.left = style.padding.right = 20.0f * rpxScale;
        }
        if (!style.hasColor) {
            style.hasColor = true;
            style.color = kTextPrimary;
        }
    } else if (tag == "progress") {
        if (!style.hasBackground) {
            style.hasBackground = true;
            style.background = Rgba{0.16f, 0.17f, 0.20f, 1.0f};
            style.borderRadius = 12.0f * rpxScale;
        }
    } else if (tag == "navigator") {
        if (!style.hasBackground) {
            style.hasBackground = true;
            style.background = Rgba{0.12f, 0.13f, 0.15f, 1.0f};
            style.borderRadius = 12.0f * rpxScale;
        }
        style.padding.left = style.padding.right = 24.0f * rpxScale;
        if (!style.hasColor) {
            style.hasColor = true;
            style.color = kTextPrimary;
        }
    } else if (tag == "slider") {
        if (!style.hasBackground) style.hasBackground = false;
    } else if (tag == "video") {
        if (!style.hasBackground) {
            style.hasBackground = true;
            style.background = Rgba{0.05f, 0.06f, 0.07f, 1.0f};
            style.borderRadius = 12.0f * rpxScale;
        }
        style.overflowHidden = true;
    }

    // 事件与 dataset。
    const char* tapNames[] = {"bindtap", "bind:tap", "catchtap", "catch:tap",
                              "bindlongpress", "bind:longpress"};
    for (const char* name : tapNames) {
        if (const std::string* handler = node.attr(name)) {
            std::string text = trim(*handler);
            if (!text.empty()) {
                render->bindTap = text;
                render->catchTap = std::strcmp(name, "catchtap") == 0 ||
                                   std::strcmp(name, "catch:tap") == 0;
                break;
            }
        }
    }
    if (const std::string* handler = node.attr("bindchange")) {
        render->bindChange = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindinput")) {
        render->bindInput = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindconfirm")) {
        render->bindConfirm = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindplay")) {
        render->bindPlay = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindpause")) {
        render->bindPause = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindended")) {
        render->bindEnded = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindtimeupdate")) {
        render->bindTimeUpdate = trim(*handler);
    }
    if (const std::string* handler = node.attr("binderror")) {
        render->bindError = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindload")) {
        render->bindImageLoad = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindloadedmetadata")) {
        render->bindLoadedMetadata = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindwaiting")) {
        render->bindWaiting = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindprogress")) {
        render->bindProgress = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindseekcomplete")) {
        render->bindSeekComplete = trim(*handler);
    }
    if (const std::string* handler = node.attr("bindfullscreenchange")) {
        render->bindFullscreenChange = trim(*handler);
    }
    for (const auto& entry : node.attributes) {
        if (entry.first.size() > 5 && entry.first.compare(0, 5, "data-") == 0) {
            render->dataset[entry.first.substr(5)] = interpolate(entry.second, locals);
        }
    }
    if (const std::string* rawOverlay = node.attr("data-overlay")) {
        const std::string text = trim(interpolate(*rawOverlay, locals));
        render->overlay = !text.empty() && text != "false" && text != "0";
    }
    if (const std::string* rawAutoFocus = node.attr("autofocus")) {
        const std::string text = trim(interpolate(*rawAutoFocus, locals));
        render->autoFocus = !text.empty() && text != "false" && text != "0";
    }

    // 组件属性。
    if (tag == "image") {
        render->src = attributeText(node, "src", locals);
        render->imageMode = attributeText(node, "mode", locals);
        render->imageHeaders = attributeText(node, "http-headers", locals);
        JsonValue lazy;
        if (evaluateAttribute(node, "lazy-load", locals, lazy)) {
            render->imageLazyLoad = isTruthy(lazy);
        } else if (const std::string* rawLazy = node.attr("lazy-load")) {
            render->imageLazyLoad = trim(*rawLazy) == "true";
        }
        render->bindImageError = render->bindError;
        if (render->imageMode.empty()) render->imageMode = "scaleToFill";
    } else if (tag == "input" || tag == "textarea") {
        JsonValue boundValue;
        if (evaluateAttribute(node, "value", locals, boundValue)) {
            render->value = valueToTextImpl(boundValue);
        }
        auto it = current ? current->inputValues.find(path) : current->inputValues.end();
        if (current && it != current->inputValues.end()) render->value = it->second;
        render->placeholder = attributeText(node, "placeholder", locals);
    } else if (tag == "slider") {
        render->sliderMin = 0.0f;
        render->sliderMax = 100.0f;
        render->sliderStep = 1.0f;
        JsonValue attribute;
        if (evaluateAttribute(node, "min", locals, attribute)) render->sliderMin = toFloat(attribute, 0.0f);
        if (evaluateAttribute(node, "max", locals, attribute)) render->sliderMax = toFloat(attribute, 100.0f);
        if (evaluateAttribute(node, "step", locals, attribute)) render->sliderStep = std::max(0.01f, toFloat(attribute, 1.0f));
        if (evaluateAttribute(node, "value", locals, attribute)) render->value = valueToTextImpl(attribute);
        if (current) {
            auto it = current->sliderValues.find(path);
            if (it != current->sliderValues.end()) {
                render->value = valueToTextImpl(JsonValue::number(it->second));
            }
        }
    } else if (tag == "switch" || tag == "checkbox" || tag == "radio") {
        JsonValue attribute;
        bool checked = false;
        if (evaluateAttribute(node, "checked", locals, attribute)) checked = isTruthy(attribute);
        if (const std::string* checkedText = node.attr("checked")) {
            std::string text = trim(*checkedText);
            if (text == "true" || text == "checked") checked = true;
        }
        if (tag == "radio") {
            render->radioName = attributeText(node, "name", locals);
            if (current) {
                auto it = current->radioSelections.find(render->radioName);
                if (it != current->radioSelections.end()) {
                    checked = it->second == path;
                }
            }
        } else if (current) {
            auto it = current->toggles.find(path);
            if (it != current->toggles.end()) checked = it->second;
        }
        render->checked = checked;
        JsonValue disabled;
        if (evaluateAttribute(node, "disabled", locals, disabled)) {
            render->disabled = isTruthy(disabled);
        }
    } else if (tag == "navigator") {
        render->navUrl = attributeText(node, "url", locals);
        render->openType = attributeText(node, "open-type", locals);
        render->text = trim(collectText(node, locals));
    } else if (tag == "scroll-view") {
        JsonValue attr;
        if (evaluateAttribute(node, "scroll-x", locals, attr)) {
            render->horizontal = isTruthy(attr);
        }
        render->style.overflowHidden = true;
    } else if (tag == "swiper") {
        render->style.overflowHidden = true;
        JsonValue attr;
        if (evaluateAttribute(node, "autoplay", locals, attr)) render->autoplay = isTruthy(attr);
        if (evaluateAttribute(node, "interval", locals, attr)) {
            render->swiperInterval = std::max(500.0f, toFloat(attr, 3000.0f));
        }
        size_t count = 0;
        for (const auto& child : node.children) {
            if (!child.isText()) ++count;
        }
        render->swiperCount = static_cast<int>(count);
        int active = 0;
        if (current) {
            auto it = current->swiperIndex.find(path);
            if (it != current->swiperIndex.end()) active = it->second;
        }
        if (render->swiperCount > 0) active = std::max(0, std::min(active, render->swiperCount - 1));
        render->swiperIndex = active;
    } else if (tag == "text" || tag == "button") {
        render->text = trim(collectText(node, locals));
    } else if (tag == "progress") {
        JsonValue attribute;
        if (evaluateAttribute(node, "percent", locals, attribute)) {
            render->progressPercent = std::max(0.0f, std::min(100.0f, toFloat(attribute, 0.0f)));
        }
        std::string active = attributeText(node, "activeColor", locals);
        if (active.empty()) active = attributeText(node, "active-color", locals);
        Rgba parsed;
        if (!active.empty() && parseColor(active, parsed)) render->progressActive = parsed;
        std::string track = attributeText(node, "backgroundColor", locals);
        if (track.empty()) track = attributeText(node, "background-color", locals);
        if (!track.empty() && parseColor(track, parsed)) render->progressTrack = parsed;
    } else if (tag == "rich-text") {
        render->text = stripTags(attributeText(node, "nodes", locals));
    } else if (tag == "icon") {
        render->iconType = attributeText(node, "type", locals);
        if (render->iconType.empty()) render->iconType = "info";
        JsonValue attribute;
        if (evaluateAttribute(node, "size", locals, attribute)) {
            render->iconSize = std::max(16.0f, toFloat(attribute, 48.0f));
        } else {
            render->iconSize = 48.0f * rpxScale;
        }
    } else if (tag == "video") {
        render->videoSrc = attributeText(node, "src", locals);
        render->videoPoster = attributeText(node, "poster", locals);
        render->videoHeaders = attributeText(node, "http-headers", locals);
        render->videoObjectFit = attributeText(node, "object-fit", locals);
        if (render->videoObjectFit.empty()) {
            render->videoObjectFit = attributeText(node, "objectFit", locals);
        }
        if (render->videoObjectFit.empty()) render->videoObjectFit = "contain";
        JsonValue attribute;
        if (evaluateAttribute(node, "autoplay", locals, attribute)) {
            render->videoAutoplay = isTruthy(attribute);
        }
        if (evaluateAttribute(node, "loop", locals, attribute)) {
            render->videoLoop = isTruthy(attribute);
        }
        if (evaluateAttribute(node, "muted", locals, attribute)) {
            render->videoMuted = isTruthy(attribute);
        }
        if (evaluateAttribute(node, "controls", locals, attribute)) {
            render->videoControls = isTruthy(attribute);
        }
        if (evaluateAttribute(node, "initial-time", locals, attribute)) {
            render->videoInitialTime = std::max(0.0f, toFloat(attribute, 0.0f));
        }
        if (evaluateAttribute(node, "playback-rate", locals, attribute)) {
            render->videoPlaybackRate = toFloat(attribute, 0.0f);
        }
        if (evaluateAttribute(node, "volume", locals, attribute)) {
            render->videoVolume = static_cast<int>(toFloat(attribute, -1.0f));
        }
        render->videoSubtitle = attributeText(node, "subtitle", locals);
        if (current) {
            auto playing = current->videoPlaying.find(path);
            if (playing != current->videoPlaying.end()) {
                render->videoPlaying = playing->second;
            }
            auto time = current->videoTime.find(path);
            if (time != current->videoTime.end()) render->videoTime = time->second;
        }
    }

    render->style = style;
    render->fontSize = style.fontSize;
    render->lineHeight = effectiveLineHeight(style);

    bool hasChildren = tag == "view" || tag == "scroll-view" || tag == "radio-group" ||
                       tag == "checkbox-group" || tag == "form" || tag == "label" ||
                       tag == "swiper" || tag == "swiper-item" || tag == "navigator";
    if (hasChildren) {
        ancestors.push_back(selector);
        if (tag == "swiper") {
            // swiper 只构建当前展示的页面，切换时整体重建。
            size_t elementIndex = 0;
            bool chainActive = false;
            bool chainTaken = false;
            for (const auto& child : node.children) {
                if (child.isText()) continue;
                bool visible = true;
                if (child.attr("wx:if")) {
                    bool ok = false;
                    visible = conditionTruthy(child, "wx:if", locals, ok);
                    chainActive = true;
                    chainTaken = visible;
                } else if (child.attr("wx:elif") && chainActive) {
                    bool ok = false;
                    visible = !chainTaken && conditionTruthy(child, "wx:elif", locals, ok);
                    chainTaken = chainTaken || visible;
                } else if (child.attr("wx:else") && chainActive) {
                    visible = !chainTaken;
                    chainTaken = true;
                } else {
                    chainActive = false;
                }
                if (visible && static_cast<int>(elementIndex) == render->swiperIndex) {
                    buildElement(child, style, ancestors, locals,
                                 path + "/" + std::to_string(elementIndex), render->children,
                                 false);
                }
                ++elementIndex;
            }
        } else {
            buildChildren(node, style, ancestors, locals, path, render->children);
        }
        ancestors.pop_back();
    }

    out.push_back(std::move(render));
}

std::string MiniappView::collectText(const WxmlNode& node,
                                     const std::vector<LocalValue>& locals) {
    std::string result;
    for (const auto& child : node.children) {
        if (child.isText()) {
            result += interpolate(child.text, locals);
        } else {
            result += collectText(child, locals);
        }
    }
    if (result.empty() && !node.text.empty()) {
        result = interpolate(node.text, locals);
    }
    return result;
}

// ---------------- 绘制 ----------------

void MiniappView::draw(NVGcontext* vg, float x, float y, float width, float height,
                       brls::Style, brls::FrameContext*) {
    if (this->pendingLoad) {
        drawLoading(vg, x, y, width, height);
        return;
    }
    if (!current || !current->loaded) {
        drawError(vg, x, y, width, height,
                  errorMessage.empty() ? "miniapp failed to load" : errorMessage);
        return;
    }

    PageState& page = *current;
    if (page.dirty || !page.root) {
        rebuildTree(page);
    }
    if (!page.root) {
        drawError(vg, x, y, width, height, page.error);
        return;
    }
    if (!page.root->yoga || std::fabs(width - layoutWidth) > 0.5f ||
        std::fabs(height - layoutHeight) > 0.5f) {
        rebuildLayout(page, width, height);
    }

    updateGeometry(*page.root, x, y);

    // 保证焦点仍然有效。
    if (!focusNodes.empty()) {
        bool found = false;
        for (RenderNode* node : focusNodes) {
            if (node->path == focusPath) {
                found = true;
                break;
            }
        }
        if (!found) focusPath = focusNodes.front()->path;
    } else if (!focusPath.empty()) {
        focusPath.clear();
    }

    // 全屏视频：铺满内容区，只画黑底 + 一行退出提示，画面由 drawVideoSurface 直出。
    if (!fullscreenVideoPath.empty()) {
        RenderNode* node = findNodeByPath(*page.root, fullscreenVideoPath);
        if (!node || node->tag != "video") {
            fullscreenVideoPath.clear();
        } else {
            nvgBeginPath(vg);
            nvgRect(vg, x, y, width, height);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
            nvgFill(vg);
            nvgFontFaceId(vg, fontId);
            nvgFontSize(vg, 18.0f);
            nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, 0.75f));
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            nvgText(vg, x + width - 16.0f, y + 12.0f, "按 B 退出全屏", nullptr);
            fullscreenHasOverlay = hasOverlayNodes(*page.root);
            if (!page.readyCalled && page.runtime) {
                page.readyCalled = true;
                page.runtime->callLifecycle("onReady");
            }
            return;
        }
    }

    drawNode(vg, *page.root, 1.0f, nullptr);

    // 首次完成布局后触发 onReady，与微信时序保持一致。
    if (!page.readyCalled && page.runtime) {
        page.readyCalled = true;
        page.runtime->callLifecycle("onReady");
    }

    if (RenderNode* focused = findFocusByPath(focusPath)) {
        if (focused->isInteractive()) drawFocusRing(vg, *focused);
    }
    drawOverlays(vg, width, height);
}

void MiniappView::drawNode(NVGcontext* vg, RenderNode& node, float parentAlpha,
                           const RenderNode* clipParent) {
    if (node.style.displayNone) return;
    if (node.absWidth <= 0.0f || node.absHeight <= 0.0f) return;

    float alpha = parentAlpha * node.style.opacity;
    if (alpha <= 0.01f) return;

    float drawX = node.absLeft;
    float drawY = node.absTop;
    float width = node.absWidth;
    float height = node.absHeight;

    // box-shadow 近似：在背景下方绘制一层偏移的半透明圆角矩形。
    if (node.style.hasShadow) {
        nvgBeginPath(vg);
        float blur = node.style.shadowBlur;
        nvgRoundedRect(vg, drawX + node.style.shadowX - blur * 0.5f,
                       drawY + node.style.shadowY - blur * 0.5f, width + blur,
                       height + blur, clampRadius(node.style.borderRadius + blur, width, height));
        nvgFillColor(vg, toColor(node.style.shadowColor, alpha));
        nvgFill(vg);
    }

    drawBackground(vg, node, alpha);

    bool clip = node.isScrollView() || node.style.overflowHidden;
    if (clip) {
        nvgSave(vg);
        if (clipParent) {
            nvgIntersectScissor(vg, drawX, drawY, width, height);
        } else {
            nvgScissor(vg, drawX, drawY, width, height);
        }
    }

    if (node.isText()) {
        drawTextBlock(vg, node, drawX, drawY, width, alpha);
    } else if (node.isButton()) {
        if (!node.text.empty()) {
            nvgFontFaceId(vg, fontId);
            nvgFontSize(vg, node.fontSize);
            nvgFillColor(vg, toColor(node.style.color, alpha));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(vg, drawX + width * 0.5f, drawY + height * 0.5f,
                    node.text.c_str(), nullptr);
            if (node.style.bold) {
                nvgText(vg, drawX + width * 0.5f + 0.6f, drawY + height * 0.5f,
                        node.text.c_str(), nullptr);
            }
        }
    } else if (node.isImage()) {
        drawImage(vg, node, alpha);
    } else if (node.tag == "input" || node.tag == "textarea") {
        drawInput(vg, node, alpha);
    } else if (node.tag == "progress") {
        drawProgress(vg, node, alpha);
    } else if (node.tag == "rich-text") {
        drawTextBlock(vg, node, drawX, drawY, width, alpha);
    } else if (node.tag == "icon") {
        drawIcon(vg, node, alpha);
    } else if (node.tag == "video") {
        drawVideo(vg, node, alpha);
    } else if (node.tag == "switch") {
        drawSwitch(vg, node, alpha);
    } else if (node.tag == "checkbox") {
        drawCheckbox(vg, node, alpha);
    } else if (node.tag == "radio") {
        drawRadio(vg, node, alpha);
    } else if (node.tag == "slider") {
        drawSlider(vg, node, alpha);
    } else if (node.tag == "navigator") {
        drawNavigator(vg, node, alpha);
    } else if (node.tag == "swiper") {
        drawSwiper(vg, node, alpha, clip ? &node : nullptr);
    } else {
        for (auto& child : node.children) {
            drawNode(vg, *child, alpha, clip ? &node : clipParent);
        }
    }

    if (clip) nvgRestore(vg);
}

void MiniappView::drawBackground(NVGcontext* vg, RenderNode& node, float alpha) {
    if (node.absWidth <= 0.0f || node.absHeight <= 0.0f) return;
    float radius = clampRadius(node.style.borderRadius, node.absWidth, node.absHeight);
    if (node.style.hasBackground && node.style.background.a > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight, radius);
        nvgFillColor(vg, toColor(node.style.background, alpha));
        nvgFill(vg);
    }
    if (node.style.hasBorder && node.style.borderWidth > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight, radius);
        nvgStrokeColor(vg, toColor(node.style.borderColor, alpha));
        nvgStrokeWidth(vg, node.style.borderWidth);
        nvgStroke(vg);
    }
}

void MiniappView::drawTextBlock(NVGcontext* vg, const RenderNode& node, float x,
                                float y, float width, float alpha) {
    if (node.text.empty() || width <= 0.0f) return;

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, node.fontSize);
    nvgFillColor(vg, toColor(node.style.color, alpha));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    NVGtextRow rows[64];
    int count = nvgTextBreakLines(vg, node.text.c_str(), nullptr, width, rows, 64);
    for (int i = 0; i < count; ++i) {
        float rowX = x;
        if (node.style.textAlign == 1) {
            rowX = x + (width - rows[i].width) * 0.5f;
        } else if (node.style.textAlign == 2) {
            rowX = x + width - rows[i].width;
        }
        float rowY = y + node.lineHeight * static_cast<float>(i) + node.lineHeight * 0.5f;
        nvgText(vg, rowX, rowY, rows[i].start, rows[i].end);
        if (node.style.bold) {
            nvgText(vg, rowX + 0.6f, rowY, rows[i].start, rows[i].end);
        }
    }
}

int MiniappView::imageTexture(const std::string& src, const std::string& pageDir,
                              const std::string& headers) {
    if (src.empty()) return -1;
    constexpr int kMissingLocal = -2;

    const bool remote =
        src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0;
    std::string key;
    bool fetchRemote = remote;
    if (remote) {
        key = src;
    } else {
        std::string local = resolveLocalPath(src, pageDir);
        if (src[0] == '/') {
            // 服务端下发的资源路径（如 /media/covers/x.jpg）包内通常不存在。
            // 只探测一次并缓存结果，避免绘制时反复访问存储卡。
            auto localIt = imageTextures.find(local);
            bool missing = localIt != imageTextures.end() && localIt->second == kMissingLocal;
            if (localIt == imageTextures.end()) {
                std::ifstream probe(local, std::ios::binary);
                if (probe.good()) {
                    key = local;
                } else {
                    imageTextures[local] = kMissingLocal;
                    missing = true;
                }
            }
            if (missing) {
                if (MiniAppApi::instance().ready()) {
                    key = MiniAppApi::instance().serverUrl() + src;
                    fetchRemote = true;
                } else {
                    return -1;
                }
            } else if (key.empty()) {
                key = local;
            }
        } else {
            key = local;
        }
    }
    auto it = imageTextures.find(key);
    if (it != imageTextures.end()) return it->second;

    if (fetchRemote) {
        // 磁盘缓存命中时也交给解码线程：读盘 + 解码是首屏最容易卡住 UI 的环节。
        // 解码完成后再回主线程做 GPU 上传（nvgCreateImageRGBA）。
        if (pendingImages.insert(key).second) {
            std::weak_ptr<bool> weak = aliveToken;
            std::string requestHeaders = headers;
            DecodeQueue::instance().submit([this, key, weak, requestHeaders] {
                std::shared_ptr<std::string> cached = readImageCache(key);
                int width = 0;
                int height = 0;
                unsigned char* pixels =
                    cached ? decodeToRgba(*cached, width, height) : nullptr;
                brls::sync([this, key, weak, requestHeaders, pixels, width, height] {
                    auto token = weak.lock();
                    if (!token || !*token) {
                        if (pixels) stbi_image_free(pixels);
                        return;
                    }
                    pendingImages.erase(key);
                    if (!pixels) {
                        // 没有缓存（或解码失败）：保持下载中占位并走网络。
                        imageTextures[key] = -3;
                        this->requestRemoteImage(key, requestHeaders);
                        return;
                    }
                    NVGcontext* vg = brls::Application::getNVGContext();
                    int texture = nvgCreateImageRGBA(vg, width, height, 0, pixels);
                    stbi_image_free(pixels);
                    imageTextures[key] = texture > 0 ? texture : -1;
                    this->invalidate();
                });
            });
        }
        return -3;
    }

    NVGcontext* vg = brls::Application::getNVGContext();
    int texture = nvgCreateImage(vg, key.c_str(), 0);
    if (texture <= 0) {
        texture = -1;
        brls::Logger::warning("miniapp: cannot load image {}", key);
    }
    imageTextures[key] = texture;
    return texture;
}

void MiniappView::requestRemoteImage(const std::string& url, const std::string& headers) {
    // 走 nxkit 现成的异步请求路径（wx.request 也用它），避免自建线程。
    std::weak_ptr<bool> weak = aliveToken;
    nxkit::HttpClient client;
    client.setTimeout(15000);
    if (!headers.empty()) {
        // 约定格式："Cookie: a=b; c=d"，只取第一个冒号前的头名。
        size_t colon = headers.find(':');
        if (colon != std::string::npos) {
            std::string name = headers.substr(0, colon);
            std::string value = headers.substr(colon + 1);
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            if (!name.empty()) client.setHeader(name, value);
        }
    }
    client.getAsync(url, {}, [this, url, weak](const nxkit::HttpResult& result) {
        if (!result.ok || result.body.empty()) {
            brls::sync([this, url, weak] {
                auto token = weak.lock();
                if (!token || !*token) return;
                pendingImages.erase(url);
                imageTextures[url] = -1;
                this->invalidate();
            });
            return;
        }
        std::shared_ptr<std::string> bytes =
            std::make_shared<std::string>(result.body);
        // 在工作线程落盘 + 解码，主线程只做纹理上传。
        writeImageCache(url, *bytes);
        int width = 0;
        int height = 0;
        unsigned char* pixels = nullptr;
        try {
            pixels = decodeToRgba(*bytes, width, height);
        } catch (...) {
            pixels = nullptr;
        }
        brls::sync([this, url, weak, pixels, width, height] {
            auto token = weak.lock();
            if (!token || !*token) {
                if (pixels) stbi_image_free(pixels);
                return;
            }
            pendingImages.erase(url);
            if (!pixels) {
                brls::Logger::warning("miniapp: cannot decode remote image {}", url);
                imageTextures[url] = -1;
                this->invalidate();
                return;
            }
            NVGcontext* vg = brls::Application::getNVGContext();
            int texture = nvgCreateImageRGBA(vg, width, height, 0, pixels);
            stbi_image_free(pixels);
            imageTextures[url] = texture > 0 ? texture : -1;
            this->invalidate();
        });
    });
}


std::string MiniappView::resolveLocalPath(const std::string& src, const std::string& pageDir) {
    if (src.empty()) return src;

    // smb://<会话id>/<共享内路径>：转换成内置 HTTP 流代理地址交给 mpv。
    if (src.rfind("smb://", 0) == 0) {
        size_t slash = src.find('/', 6);
        if (slash == std::string::npos) return std::string();
        const std::string id = src.substr(6, slash - 6);
        const std::string path = "/" + src.substr(slash + 1);
        return smbStreamUrl(id, path);
    }

    // 平台绝对路径（sdmc:/、romfs:/、switch:/ 等）和 file:// 直接透传，
    // 让小程序可以播放 SD 卡上的本地视频/字幕，而不是被拼进包目录。
    if (src.rfind("file://", 0) == 0) return src.substr(7);
    size_t colon = src.find(':');
    size_t slash = src.find('/');
    if (colon != std::string::npos && colon > 0 &&
        (slash == std::string::npos || colon < slash)) {
        return src;
    }

    if (src[0] == '/' || src[0] == '\\') return packageDir + src;
    return joinPath(pageDir, src);
}

void MiniappView::drawImage(NVGcontext* vg, RenderNode& node, float alpha) {
    // lazy-load：离屏（含 200px 预取边距）时不触发下载，等滚动进视野再说。
    if (node.imageLazyLoad) {
        const float viewHeight = brls::Application::contentHeight;
        if (node.absTop + node.absHeight < -200.0f || node.absTop > viewHeight + 200.0f) {
            return;
        }
    }

    int texture = imageTexture(node.src, current ? current->dir : packageDir,
                               node.imageHeaders);
    if (texture > 0 && !node.imageLoadDispatched && current && current->runtime) {
        node.imageLoadDispatched = true;
        current->runtime->dispatchEvent(node.bindImageLoad, "load", node.dataset,
                                        JsonValue::object());
    } else if (texture == -1 && !node.imageErrorDispatched && current && current->runtime) {
        node.imageErrorDispatched = true;
        JsonValue detail = JsonValue::object();
        detail.setMember("errMsg", JsonValue::string("图片加载失败: " + node.src));
        current->runtime->dispatchEvent(node.bindImageError, "error", node.dataset, detail);
    }
    if (texture <= 0) {
        // 加载中/失败只保留底色占位块，不画文件名文字。
        if (!node.style.hasBackground) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight,
                           clampRadius(node.style.borderRadius, node.absWidth, node.absHeight));
            nvgFillColor(vg, nvgRGBAf(0.16f, 0.17f, 0.19f, alpha));
            nvgFill(vg);
        }
        return;
    }

    int imageWidth = 0;
    int imageHeight = 0;
    nvgImageSize(vg, texture, &imageWidth, &imageHeight);
    if (imageWidth <= 0 || imageHeight <= 0) return;

    float scaleX = node.absWidth / static_cast<float>(imageWidth);
    float scaleY = node.absHeight / static_cast<float>(imageHeight);
    float scale = std::max(scaleX, scaleY);
    float patternWidth = imageWidth * scaleX;
    float patternHeight = imageHeight * scaleY;
    float patternX = node.absLeft;
    float patternY = node.absTop;
    if (node.imageMode != "scaleToFill") {
        if (node.imageMode == "aspectFit") {
            scale = std::min(scaleX, scaleY);
        }
        patternWidth = imageWidth * scale;
        patternHeight = imageHeight * scale;
        patternX = node.absLeft + (node.absWidth - patternWidth) * 0.5f;
        patternY = node.absTop + (node.absHeight - patternHeight) * 0.5f;
    }

    nvgSave(vg);
    nvgIntersectScissor(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight);
    nvgBeginPath(vg);
    nvgRect(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight);
    NVGpaint paint = nvgImagePattern(vg, patternX, patternY, patternWidth,
                                     patternHeight, 0.0f, texture, alpha);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    nvgRestore(vg);
}

void MiniappView::drawInput(NVGcontext* vg, RenderNode& node, float alpha) {
    std::string text = node.value.empty() ? node.placeholder : node.value;
    bool placeholder = node.value.empty();
    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, node.fontSize);
    nvgFillColor(vg, placeholder ? nvgRGBAf(kTextMuted.r, kTextMuted.g, kTextMuted.b, alpha)
                                : toColor(node.style.color, alpha));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    float textX = node.absLeft + node.style.padding.left;
    float textY = node.absTop + node.absHeight * 0.5f;
    nvgText(vg, textX, textY, text.c_str(), nullptr);

    if (node.path == focusPath) {
        float bounds[4];
        nvgTextBounds(vg, textX, textY, text.c_str(), nullptr, bounds);
        float caretX = node.value.empty() ? textX : bounds[2] + 2.0f;
        nvgBeginPath(vg);
        nvgRect(vg, caretX, textY - node.fontSize * 0.6f, 2.0f, node.fontSize * 1.2f);
        nvgFillColor(vg, toColor(kAccent, alpha));
        nvgFill(vg);
    }
}

void MiniappView::drawProgress(NVGcontext* vg, RenderNode& node, float alpha) {
    float radius = clampRadius(node.style.borderRadius, node.absWidth, node.absHeight);
    float ratio = std::max(0.0f, std::min(1.0f, node.progressPercent / 100.0f));

    nvgBeginPath(vg);
    nvgRoundedRect(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight, radius);
    nvgFillColor(vg, toColor(node.progressTrack, alpha));
    nvgFill(vg);

    if (ratio > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, node.absLeft, node.absTop, node.absWidth * ratio, node.absHeight,
                       radius);
        nvgFillColor(vg, toColor(node.progressActive, alpha));
        nvgFill(vg);
    }
}

void MiniappView::drawIcon(NVGcontext* vg, RenderNode& node, float alpha) {
    float size = std::min(node.iconSize, std::min(node.absWidth, node.absHeight));
    if (size <= 0.0f) size = std::min(node.absWidth, node.absHeight);
    float centerX = node.absLeft + node.absWidth * 0.5f;
    float centerY = node.absTop + node.absHeight * 0.5f;
    float radius = size * 0.5f;

    Rgba color = kAccent;
    std::string glyph = "i";
    if (node.iconType == "warn" || node.iconType == "waiting") {
        color = Rgba{0.98f, 0.68f, 0.20f, 1.0f};
        glyph = "!";
    } else if (node.iconType == "cancel" || node.iconType == "clear") {
        color = Rgba{0.96f, 0.36f, 0.36f, 1.0f};
        glyph = "x";
    } else if (node.iconType == "success") {
        glyph = "v";
    } else if (node.iconType == "search") {
        color = Rgba{0.35f, 0.62f, 0.98f, 1.0f};
        glyph = "?";
    } else if (node.iconType == "download") {
        color = Rgba{0.35f, 0.62f, 0.98f, 1.0f};
        glyph = "↓";
    }

    nvgBeginPath(vg);
    nvgCircle(vg, centerX, centerY, radius);
    nvgFillColor(vg, toColor(color, alpha));
    nvgFill(vg);

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, size * 0.62f);
    nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, centerX, centerY, glyph.c_str(), nullptr);
}

void MiniappView::drawVideo(NVGcontext* vg, RenderNode& node, float alpha) {
    if (node.absWidth <= 0.0f || node.absHeight <= 0.0f) return;

    float barHeight =
        node.videoControls ? std::min(44.0f, node.absHeight * 0.35f) : 0.0f;
    float videoHeight = std::max(1.0f, node.absHeight - barHeight);

    nvgSave(vg);
    nvgIntersectScissor(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight);

    // 视频画面区域。
    nvgBeginPath(vg);
    nvgRect(vg, node.absLeft, node.absTop, node.absWidth, videoHeight);
    nvgFillColor(vg, nvgRGBAf(0.03f, 0.04f, 0.05f, alpha));
    nvgFill(vg);

    bool showPoster = !node.videoPlaying || node.videoError;
    if (showPoster && !node.videoPoster.empty()) {
        int texture = imageTexture(node.videoPoster, current ? current->dir : packageDir);
        int imageWidth = 0;
        int imageHeight = 0;
        if (texture > 0) nvgImageSize(vg, texture, &imageWidth, &imageHeight);
        if (texture > 0 && imageWidth > 0 && imageHeight > 0) {
            float scaleX = node.absWidth / static_cast<float>(imageWidth);
            float scaleY = videoHeight / static_cast<float>(imageHeight);
            float scale = std::min(scaleX, scaleY);
            if (node.videoObjectFit == "cover" || node.videoObjectFit == "fill") {
                scale = std::max(scaleX, scaleY);
            }
            float patternWidth = imageWidth * scale;
            float patternHeight = imageHeight * scale;
            float patternX = node.absLeft + (node.absWidth - patternWidth) * 0.5f;
            float patternY = node.absTop + (videoHeight - patternHeight) * 0.5f;
            nvgBeginPath(vg);
            nvgRect(vg, node.absLeft, node.absTop, node.absWidth, videoHeight);
            NVGpaint paint = nvgImagePattern(vg, patternX, patternY, patternWidth,
                                             patternHeight, 0.0f, texture, alpha);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }
    }

    // 未播放时在画面中央画播放按钮；播放中的画面由 OpenGL 直出覆盖。
    if (showPoster) {
        float centerX = node.absLeft + node.absWidth * 0.5f;
        float centerY = node.absTop + videoHeight * 0.5f;
        float radius = std::max(18.0f, std::min(44.0f, std::min(node.absWidth, videoHeight) * 0.14f));
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, radius);
        nvgFillColor(vg, nvgRGBAf(0.0f, 0.0f, 0.0f, 0.55f * alpha));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, centerX - radius * 0.28f, centerY - radius * 0.34f);
        nvgLineTo(vg, centerX + radius * 0.38f, centerY);
        nvgLineTo(vg, centerX - radius * 0.28f, centerY + radius * 0.34f);
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
        nvgFill(vg);
    }

    if (node.videoError && !node.videoErrorText.empty()) {
        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, std::min(node.fontSize, 22.0f));
        nvgFillColor(vg, nvgRGBAf(1.0f, 0.42f, 0.42f, alpha));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, node.absLeft + node.absWidth * 0.5f,
                node.absTop + videoHeight - 18.0f, node.videoErrorText.c_str(), nullptr);
    }

    if (barHeight > 0.0f) {
        float barTop = node.absTop + videoHeight;
        nvgBeginPath(vg);
        nvgRect(vg, node.absLeft, barTop, node.absWidth, barHeight);
        nvgFillColor(vg, nvgRGBAf(0.08f, 0.09f, 0.11f, 0.96f * alpha));
        nvgFill(vg);

        float progress = 0.0f;
        if (node.videoDuration > 0.0) {
            progress = static_cast<float>(node.videoTime / node.videoDuration);
        }
        progress = std::max(0.0f, std::min(1.0f, progress));
        nvgBeginPath(vg);
        nvgRect(vg, node.absLeft, barTop, node.absWidth * progress, 3.0f);
        nvgFillColor(vg, toColor(kAccent, alpha));
        nvgFill(vg);

        std::string clock = formatClock(node.videoTime) + " / " +
                            formatClock(node.videoDuration);
        std::string status;
        if (node.videoError) {
            status = "播放失败";
        } else {
            status = node.videoPlaying ? "播放中" : "已暂停";
        }

        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, 18.0f);
        nvgFillColor(vg, nvgRGBAf(0.82f, 0.84f, 0.88f, alpha));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, node.absLeft + 12.0f, barTop + barHeight * 0.62f, clock.c_str(),
                nullptr);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, node.absLeft + node.absWidth - 12.0f, barTop + barHeight * 0.62f,
                status.c_str(), nullptr);
    }

    nvgRestore(vg);
}

void MiniappView::drawVideoSurface() {
    if (activeVideoPath.empty() || !current || !current->root) return;
    RenderNode* node = findNodeByPath(*current->root, activeVideoPath);
    if (!node || node->videoError) return;
    if (node->tag != "video") return;

    // 全屏：忽略节点布局与裁剪，直接铺满内容区；暂停时也保留当前帧。
    if (!fullscreenVideoPath.empty() && fullscreenVideoPath == node->path) {
        float vw = brls::Application::contentWidth;
        float vh = brls::Application::contentHeight;
        nxkit::VideoView::instance().draw(brls::Rect(0.0f, 0.0f, vw, vh), 1.0f);
        return;
    }
    if (!node->videoPlaying) return;
    if (node->style.displayNone || node->absWidth <= 0.0f || node->absHeight <= 0.0f) {
        return;
    }

    float barHeight =
        node->videoControls ? std::min(44.0f, node->absHeight * 0.35f) : 0.0f;
    float viewWidth = brls::Application::contentWidth;
    float viewHeight = brls::Application::contentHeight;
    if (node->absLeft < 0.0f || node->absTop < 0.0f ||
        node->absLeft + node->absWidth > viewWidth ||
        node->absTop + node->absHeight > viewHeight) {
        return;
    }
    // 被 scroll-view 等裁剪时不直出，避免画到裁剪区域之外。
    for (RenderNode* parent = node->parent; parent; parent = parent->parent) {
        if (!parent->style.overflowHidden && !parent->isScrollView()) continue;
        if (node->absLeft < parent->absLeft || node->absTop < parent->absTop ||
            node->absLeft + node->absWidth > parent->absLeft + parent->absWidth ||
            node->absTop + node->absHeight > parent->absTop + parent->absHeight) {
            return;
        }
    }

    brls::Rect rect(node->absLeft, node->absTop, node->absWidth,
                    std::max(1.0f, node->absHeight - barHeight));
    nxkit::VideoView::instance().draw(rect, node->style.opacity);
}

bool MiniappView::hasOverlayNodes(const RenderNode& node) const {
    if (node.overlay) return true;
    for (const auto& child : node.children) {
        if (child && hasOverlayNodes(*child)) return true;
    }
    return false;
}

bool MiniappView::isOverlayNode(const RenderNode* node) const {
    for (const RenderNode* it = node; it; it = it->parent) {
        if (it->overlay) return true;
    }
    return false;
}

void MiniappView::drawOverlayNodes(NVGcontext* vg, RenderNode& node, float parentAlpha) {
    if (node.overlay) {
        drawNode(vg, node, parentAlpha, nullptr);
        return;
    }
    if (node.style.displayNone) return;
    for (auto& child : node.children) {
        if (child) drawOverlayNodes(vg, *child, parentAlpha);
    }
}

void MiniappView::drawVideoOverlay() {
    if (fullscreenVideoPath.empty() || !current || !current->root) return;
    const bool overlaysActive = modal.active || loadingVisible ||
                                (!toastText.empty() && overlayClock < toastUntil);
    if (!fullscreenHasOverlay && !overlaysActive) return;

    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg) return;
    const float width = brls::Application::contentWidth;
    const float height = brls::Application::contentHeight;
    const float ratio = static_cast<float>(brls::Application::windowWidth) /
                        static_cast<float>(brls::Application::windowHeight);
    nvgBeginFrame(vg, static_cast<float>(brls::Application::windowWidth),
                  static_cast<float>(brls::Application::windowHeight), ratio);
    nvgScale(vg, brls::Application::windowScale, brls::Application::windowScale);

    if (fullscreenHasOverlay) {
        drawOverlayNodes(vg, *current->root, 1.0f);
        RenderNode* focused = findFocusByPath(focusPath);
        if (focused && focused->isInteractive() && isOverlayNode(focused)) {
            drawFocusRing(vg, *focused);
        }
    }
    drawOverlays(vg, width, height);

    nvgResetTransform(vg);
    nvgEndFrame(vg);
}

float MiniappView::valueOf(RenderNode* node) const {
    if (!node) return 0.0f;
    if (current) {
        auto it = current->sliderValues.find(node->path);
        if (it != current->sliderValues.end()) {
            return std::max(node->sliderMin, std::min(node->sliderMax, it->second));
        }
    }
    float value = static_cast<float>(std::atof(node->value.c_str()));
    return std::max(node->sliderMin, std::min(node->sliderMax, value));
}

void MiniappView::drawSwitch(NVGcontext* vg, RenderNode& node, float alpha) {
    float height = node.absHeight;
    float width = node.absWidth;
    float radius = height * 0.5f;
    Rgba track = node.checked ? kAccent : Rgba{0.23f, 0.24f, 0.27f, 1.0f};
    if (node.checked && node.style.hasBackground) track = node.style.background;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, node.absLeft, node.absTop, width, height, radius);
    nvgFillColor(vg, toColor(track, alpha));
    nvgFill(vg);

    float knobRadius = radius - 4.0f * rpxScale;
    float knobX = node.checked ? node.absLeft + width - radius : node.absLeft + radius;
    nvgBeginPath(vg);
    nvgCircle(vg, knobX, node.absTop + radius, knobRadius);
    nvgFillColor(vg, nvgRGBAf(0.98f, 0.98f, 0.99f, alpha));
    nvgFill(vg);
}

void MiniappView::drawCheckbox(NVGcontext* vg, RenderNode& node, float alpha) {
    float size = std::min(node.absWidth, node.absHeight);
    float left = node.absLeft;
    float top = node.absTop + (node.absHeight - size) * 0.5f;
    float radius = size * 0.2f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, left, top, size, size, radius);
    nvgFillColor(vg, node.checked ? toColor(kAccent, alpha)
                                  : nvgRGBAf(0.16f, 0.17f, 0.19f, alpha));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBAf(0.35f, 0.37f, 0.40f, alpha));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    if (node.checked) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, left + size * 0.22f, top + size * 0.52f);
        nvgLineTo(vg, left + size * 0.43f, top + size * 0.72f);
        nvgLineTo(vg, left + size * 0.78f, top + size * 0.30f);
        nvgStrokeColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    }
}

void MiniappView::drawRadio(NVGcontext* vg, RenderNode& node, float alpha) {
    float size = std::min(node.absWidth, node.absHeight);
    float cx = node.absLeft + node.absWidth * 0.5f;
    float cy = node.absTop + node.absHeight * 0.5f;
    float radius = size * 0.42f;

    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, radius);
    nvgFillColor(vg, node.checked ? toColor(kAccent, alpha)
                                  : nvgRGBAf(0.16f, 0.17f, 0.19f, alpha));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBAf(0.35f, 0.37f, 0.40f, alpha));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    if (node.checked) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, radius * 0.48f);
        nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
        nvgFill(vg);
    }
}

void MiniappView::drawSlider(NVGcontext* vg, RenderNode& node, float alpha) {
    float value = valueOf(&node);
    float ratio = 0.0f;
    if (node.sliderMax > node.sliderMin) {
        ratio = (value - node.sliderMin) / (node.sliderMax - node.sliderMin);
    }
    ratio = std::max(0.0f, std::min(1.0f, ratio));

    float padding = node.style.padding.left;
    float trackX = node.absLeft + padding;
    float trackWidth = node.absWidth - padding - node.style.padding.right;
    float centerY = node.absTop + node.absHeight * 0.5f;
    float trackHeight = 8.0f * rpxScale;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, trackX, centerY - trackHeight * 0.5f, trackWidth, trackHeight,
                   trackHeight * 0.5f);
    nvgFillColor(vg, nvgRGBAf(0.24f, 0.25f, 0.28f, alpha));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, trackX, centerY - trackHeight * 0.5f, trackWidth * ratio,
                   trackHeight, trackHeight * 0.5f);
    nvgFillColor(vg, toColor(kAccent, alpha));
    nvgFill(vg);

    float knobX = trackX + trackWidth * ratio;
    float knobRadius = 16.0f * rpxScale;
    nvgBeginPath(vg);
    nvgCircle(vg, knobX, centerY, knobRadius);
    nvgFillColor(vg, nvgRGBAf(0.98f, 0.98f, 0.99f, alpha));
    nvgFill(vg);

    // 数值文字。
    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, node.fontSize * 0.85f);
    nvgFillColor(vg, nvgRGBAf(kTextMuted.r, kTextMuted.g, kTextMuted.b, alpha));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    std::string text = valueToTextImpl(JsonValue::number(static_cast<double>(value)));
    nvgText(vg, node.absLeft + node.absWidth, centerY, text.c_str(), nullptr);
}

void MiniappView::drawNavigator(NVGcontext* vg, RenderNode& node, float alpha) {
    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, node.fontSize);
    nvgFillColor(vg, toColor(node.style.color, alpha));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    std::string label = node.text.empty() ? node.navUrl : node.text;
    float left = node.absLeft + node.style.padding.left;
    nvgText(vg, left, node.absTop + node.absHeight * 0.5f, label.c_str(), nullptr);

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBAf(kTextMuted.r, kTextMuted.g, kTextMuted.b, alpha));
    nvgText(vg, node.absLeft + node.absWidth - node.style.padding.right,
            node.absTop + node.absHeight * 0.5f, ">", nullptr);
}

void MiniappView::drawSwiper(NVGcontext* vg, RenderNode& node, float alpha,
                             const RenderNode* clipParent) {
    for (auto& child : node.children) {
        drawNode(vg, *child, alpha, clipParent);
    }

    int count = node.swiperCount;
    if (count <= 1) return;
    float dotRadius = 5.0f * rpxScale;
    float gap = 14.0f * rpxScale;
    float totalWidth = count * dotRadius * 2 + (count - 1) * gap;
    float startX = node.absLeft + (node.absWidth - totalWidth) * 0.5f + dotRadius;
    float y = node.absTop + node.absHeight - 18.0f * rpxScale;
    for (int i = 0; i < count; ++i) {
        nvgBeginPath(vg);
        nvgCircle(vg, startX + i * (dotRadius * 2 + gap), y, dotRadius);
        nvgFillColor(vg, i == node.swiperIndex
                              ? toColor(kAccent, alpha)
                              : nvgRGBAf(1.0f, 1.0f, 1.0f, 0.35f * alpha));
        nvgFill(vg);
    }
}

void MiniappView::drawFocusRing(NVGcontext* vg, const RenderNode& node) {
    // 焦点框要跟随最近的滚动/裁剪祖先一起裁剪，否则列表滚动后卡片被裁掉，
    // 焦点框却会画到列表外的上方区域（表现为压在横幅或资讯上）。
    // 焦点框比卡片外扩 3px、描边 3px，最多伸出卡片 6px。剪裁区要按这个外扩量
    // 放大，否则贴着列表边缘的卡片（第一行/最左一列）选中框会被裁掉一圈，
    // 看起来像被别的东西挡住。
    constexpr float kRingOverhang = 7.0f;
    bool hasClip = false;
    float clipLeft = 0.0f;
    float clipTop = 0.0f;
    float clipWidth = 0.0f;
    float clipHeight = 0.0f;
    for (const RenderNode* parent = node.parent; parent; parent = parent->parent) {
        if (!parent->isScrollView() && !parent->style.overflowHidden) continue;
        float left = parent->absLeft - kRingOverhang;
        float top = parent->absTop - kRingOverhang;
        float right = parent->absLeft + parent->absWidth + kRingOverhang;
        float bottom = parent->absTop + parent->absHeight + kRingOverhang;
        if (!hasClip) {
            clipLeft = left;
            clipTop = top;
            clipWidth = right - left;
            clipHeight = bottom - top;
            hasClip = true;
        } else {
            float mergedLeft = std::max(clipLeft, left);
            float mergedTop = std::max(clipTop, top);
            float mergedRight = std::min(clipLeft + clipWidth, right);
            float mergedBottom = std::min(clipTop + clipHeight, bottom);
            clipLeft = mergedLeft;
            clipTop = mergedTop;
            clipWidth = std::max(0.0f, mergedRight - mergedLeft);
            clipHeight = std::max(0.0f, mergedBottom - mergedTop);
        }
    }
    if (hasClip && (clipWidth <= 0.0f || clipHeight <= 0.0f)) return;

    if (hasClip) {
        nvgSave(vg);
        nvgScissor(vg, clipLeft, clipTop, clipWidth, clipHeight);
    }

    float radius = clampRadius(node.style.borderRadius + 4.0f, node.absWidth, node.absHeight);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, node.absLeft - 3.0f, node.absTop - 3.0f,
                   node.absWidth + 6.0f, node.absHeight + 6.0f, radius);
    nvgStrokeColor(vg, nvgRGBAf(kAccent.r, kAccent.g, kAccent.b, 0.35f));
    nvgStrokeWidth(vg, 6.0f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, node.absLeft - 2.0f, node.absTop - 2.0f,
                   node.absWidth + 4.0f, node.absHeight + 4.0f, radius);
    nvgStrokeColor(vg, toColor(kAccent));
    nvgStrokeWidth(vg, 3.0f);
    nvgStroke(vg);

    if (hasClip) nvgRestore(vg);
}

void MiniappView::drawOverlays(NVGcontext* vg, float width, float height) {
    if (loadingVisible) {
        float boxWidth = 240.0f;
        float boxHeight = 130.0f;
        float left = (width - boxWidth) * 0.5f;
        float top = (height - boxHeight) * 0.5f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, left, top, boxWidth, boxHeight, 18.0f);
        nvgFillColor(vg, nvgRGBAf(0.05f, 0.05f, 0.06f, 0.92f));
        nvgFill(vg);

        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, 24.0f);
        nvgFillColor(vg, nvgRGBf(0.9f, 0.9f, 0.92f));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, left + boxWidth * 0.5f, top + boxHeight - 32.0f,
                loadingText.c_str(), nullptr);

        float spin = static_cast<float>(std::fmod(overlayClock * 0.006, 6.283185307179586));
        nvgBeginPath(vg);
        nvgArc(vg, left + boxWidth * 0.5f, top + 44.0f, 18.0f, spin, spin + 4.39822971502571,
                NVG_CW);
        nvgStrokeColor(vg, toColor(kAccent));
        nvgStrokeWidth(vg, 4.0f);
        nvgStroke(vg);
    }

    if (!toastText.empty() && overlayClock < toastUntil) {
        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, 26.0f);
        float bounds[4];
        nvgTextBounds(vg, 0, 0, toastText.c_str(), nullptr, bounds);
        float textWidth = bounds[2] - bounds[0];
        float boxWidth = std::min(width - 80.0f, textWidth + 80.0f);
        float boxHeight = 84.0f;
        float left = (width - boxWidth) * 0.5f;
        float top = height - 150.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, left, top, boxWidth, boxHeight, boxHeight * 0.5f);
        nvgFillColor(vg, nvgRGBAf(0.05f, 0.05f, 0.06f, 0.94f));
        nvgFill(vg);
        nvgFillColor(vg, nvgRGBf(0.94f, 0.95f, 0.96f));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, left + boxWidth * 0.5f, top + boxHeight * 0.5f,
                toastText.c_str(), nullptr);
    }

    if (!modal.active) return;

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, width, height);
    nvgFillColor(vg, nvgRGBAf(0.0f, 0.0f, 0.0f, 0.62f));
    nvgFill(vg);

    float panelWidth = std::min(760.0f, width * 0.68f);
    float panelHeight = 320.0f;
    float left = (width - panelWidth) * 0.5f;
    float top = (height - panelHeight) * 0.5f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, left, top, panelWidth, panelHeight, 24.0f);
    nvgFillColor(vg, nvgRGBf(0.13f, 0.14f, 0.16f));
    nvgFill(vg);

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, 32.0f);
    nvgFillColor(vg, nvgRGBf(0.96f, 0.97f, 0.98f));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgText(vg, left + panelWidth * 0.5f, top + 36.0f, modal.title.c_str(), nullptr);

    nvgFontSize(vg, 24.0f);
    nvgFillColor(vg, nvgRGBf(0.74f, 0.76f, 0.80f));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    NVGtextRow rows[8];
    int count = nvgTextBreakLines(vg, modal.content.c_str(), nullptr,
                                  panelWidth - 80.0f, rows, 8);
    for (int i = 0; i < count; ++i) {
        nvgText(vg, left + panelWidth * 0.5f, top + 100.0f + i * 34.0f,
                rows[i].start, rows[i].end);
    }

    // 按钮
    float buttonWidth = (panelWidth - 96.0f) * 0.5f;
    float buttonHeight = 64.0f;
    float buttonY = top + panelHeight - 100.0f;
    int buttons = modal.showCancel ? 2 : 1;
    for (int i = 0; i < buttons; ++i) {
        float buttonX = buttons == 2
                            ? left + 32.0f + i * (buttonWidth + 32.0f)
                            : left + (panelWidth - buttonWidth) * 0.5f;
        int index = modal.showCancel ? i : 1;
        bool selected = modal.focus == index;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, buttonX, buttonY, buttonWidth, buttonHeight, 14.0f);
        nvgFillColor(vg, selected ? toColor(kAccent) : nvgRGBf(0.19f, 0.20f, 0.23f));
        nvgFill(vg);
        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, 26.0f);
        nvgFillColor(vg, selected ? nvgRGBf(0.04f, 0.10f, 0.07f)
                                  : nvgRGBf(0.9f, 0.91f, 0.93f));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        const char* label = index == 0 ? "取消" : "确定";
        nvgText(vg, buttonX + buttonWidth * 0.5f, buttonY + buttonHeight * 0.5f,
                label, nullptr);
    }
}

void MiniappView::drawError(NVGcontext* vg, float x, float y, float width,
                            float height, const std::string& message) {
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(20, 21, 24));
    nvgFill(vg);

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, 24);
    nvgFillColor(vg, nvgRGB(255, 107, 107));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + width * 0.5f, y + height * 0.5f,
            message.empty() ? "miniapp failed to load" : message.c_str(), nullptr);
}

// ---------------- 输入 ----------------

void MiniappView::drawLoading(NVGcontext* vg, float x, float y, float width, float height) {
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(20, 21, 24));
    nvgFill(vg);

    const double elapsed = (steadyMilliseconds() - loadingStart) / 1000.0;
    const float cx = x + width * 0.5f;
    const float cy = y + height * 0.5f - 12.0f;
    const float radius = 22.0f;

    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, radius, static_cast<float>(elapsed * 4.0),
           static_cast<float>(elapsed * 4.0 + 1.9), NVG_CW);
    nvgStrokeWidth(vg, 4.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeColor(vg, nvgRGBAf(kAccent.r, kAccent.g, kAccent.b, 0.95f));
    nvgStroke(vg);

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, 20.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBAf(0.92f, 0.94f, 0.96f, 0.96f));
    const std::string title = loadingTitle.empty() ? std::string("小程序") : loadingTitle;
    nvgText(vg, cx, cy + radius + 24.0f, title.c_str(), nullptr);

    nvgFontSize(vg, 15.0f);
    nvgFillColor(vg, nvgRGBAf(0.55f, 0.58f, 0.62f, 0.9f));
    nvgText(vg, cx, cy + radius + 52.0f, "正在启动…", nullptr);
}

bool MiniappView::isDpadHeld(brls::ControllerButton pad) {
    auto* platform = brls::Application::getPlatform();
    auto* input = platform ? platform->getInputManager() : nullptr;
    if (!input) return false;
    brls::ControllerState state{};
    input->updateUnifiedControllerState(&state);
    return state.buttons[pad];
}

bool MiniappView::dispatchKeyToPage(const std::string& key) {
    // 容器弹窗打开时按键归弹窗所有；没有页面时也不转发。
    if (!current || !current->runtime || modal.active) return false;
    bool consumed = false;
    if (!current->runtime->dispatchKey(key, consumed)) return false;
    return consumed;
}

void MiniappView::handleActivate() {
    if (modal.active) {
        bool confirm = modal.showCancel ? (modal.focus != 0) : true;
        auto done = modal.done;
        modal = ModalState{};
        if (done) done(confirm);
        return;
    }

    RenderNode* node = findFocusByPath(focusPath);
    if (!node) return;
    activateNode(node);
}

void MiniappView::handlePointerTap(float x, float y) {
    if (!current) return;

    if (modal.active) {
        float panelWidth = std::min(760.0f, layoutWidth * 0.68f);
        float panelHeight = 320.0f;
        float left = (layoutWidth - panelWidth) * 0.5f;
        float top = (layoutHeight - panelHeight) * 0.5f;
        float buttonWidth = (panelWidth - 96.0f) * 0.5f;
        float buttonHeight = 64.0f;
        float buttonY = top + panelHeight - 100.0f;
        if (y >= buttonY && y <= buttonY + buttonHeight) {
            bool confirm = true;
            if (modal.showCancel) {
                confirm = x >= left + 32.0f + buttonWidth + 16.0f;
            }
            auto done = modal.done;
            modal = ModalState{};
            if (done) done(confirm);
        }
        return;
    }

    if (!current->root) return;
    RenderNode* hit = hitTestNode(*current->root, x, y);
    if (!hit) return;

    RenderNode* target = nullptr;
    for (RenderNode* node = hit; node; node = node->parent) {
        if (node->isInteractive()) {
            target = node;
            break;
        }
    }
    if (!target) return;

    focusPath = target->path;
    activateNode(target);
}

void MiniappView::activateNode(RenderNode* node) {
    if (!node || !current || node->disabled) return;

    if (node->tag == "input" || node->tag == "textarea") {
        activateInput(node);
        return;
    }

    if (node->tag == "switch" || node->tag == "checkbox" || node->tag == "radio") {
        bool nextChecked = !node->checked;
        if (node->tag == "radio") {
            nextChecked = true;
            if (!node->radioName.empty()) {
                current->radioSelections[node->radioName] = node->path;
            }
        } else {
            current->toggles[node->path] = nextChecked;
        }
        JsonValue detail = JsonValue::object();
        detail.setMember("value", JsonValue::boolean(nextChecked));
        current->dirty = true;
        triggerChange(node, detail, std::string());
    } else if (node->tag == "slider") {
        float value = valueOf(node);
        float next = value + node->sliderStep;
        if (next > node->sliderMax) next = node->sliderMax;
        current->sliderValues[node->path] = next;
        JsonValue detail = JsonValue::object();
        detail.setMember("value", JsonValue::number(next));
        triggerChange(node, detail, std::string());
        current->dirty = true;
    } else if (node->tag == "navigator" && !node->navUrl.empty()) {
        if (node->openType == "redirect") redirectTo(node->navUrl);
        else if (node->openType == "reLaunch") reLaunch(node->navUrl);
        else navigateTo(node->navUrl);
    } else if (node->tag == "video") {
        toggleVideo(node);
    }

    // 微信的 bindtap 会沿祖先冒泡，catchtap 终止冒泡。
    for (RenderNode* target = node; target; target = target->parent) {
        if (target->bindTap.empty()) continue;
        triggerTap(target);
        if (target->catchTap) break;
    }
}

RenderNode* MiniappView::hitTestNode(RenderNode& node, float x, float y) const {
    if (node.style.displayNone) return nullptr;
    if (node.absWidth <= 0.0f || node.absHeight <= 0.0f) return nullptr;

    bool clip = node.style.overflowHidden || node.isScrollView();
    if (clip) {
        if (x < node.absLeft || x > node.absLeft + node.absWidth || y < node.absTop ||
            y > node.absTop + node.absHeight) {
            return nullptr;
        }
    }

    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        if (RenderNode* hit = hitTestNode(**it, x, y)) return hit;
    }

    if (x >= node.absLeft && x <= node.absLeft + node.absWidth && y >= node.absTop &&
        y <= node.absTop + node.absHeight) {
        return &node;
    }
    return nullptr;
}

RenderNode* MiniappView::findNodeByPath(RenderNode& node, const std::string& path) const {
    if (node.path == path) return &node;
    for (auto& child : node.children) {
        if (RenderNode* hit = findNodeByPath(*child, path)) return hit;
    }
    return nullptr;
}

RenderNode* MiniappView::findNodeById(RenderNode& node, const std::string& id) const {
    if (!id.empty() && node.id == id) return &node;
    for (auto& child : node.children) {
        if (RenderNode* hit = findNodeById(*child, id)) return hit;
    }
    return nullptr;
}

void MiniappView::toggleVideo(RenderNode* node) {
    if (!node || !current) return;

    bool next = !node->videoPlaying;
    node->videoPlaying = next;
    current->videoPlaying[node->path] = next;
    current->dirty = true;

    if (!next) {
        nxkit::VideoView::instance().pause();
        dispatchVideoEvent(node, node->bindPause, "pause");
        return;
    }

    node->videoError = false;
    node->videoErrorText.clear();
    current->videoStarted[node->path] = true;
    // false：已有加载过的源就继续播放，避免每次暂停后点播放都从头重新 begin。
    startVideo(node, false);

    if (node->videoPlaying) {
        dispatchVideoEvent(node, node->bindPlay, "play");
    }
}

void MiniappView::startVideo(RenderNode* node, bool forceLoad) {
    if (!node || !current) return;
    if (node->videoSrc.empty()) {
        node->videoError = true;
        node->videoErrorText = "video src 为空";
        node->videoPlaying = false;
        current->videoPlaying[node->path] = false;
        dispatchVideoEvent(node, node->bindError, "error");
        return;
    }

    const bool remote =
        node->videoSrc.rfind("http://", 0) == 0 || node->videoSrc.rfind("https://", 0) == 0;
    std::string resolved = remote ? node->videoSrc : resolveLocalPath(node->videoSrc, current->dir);
    if (resolved.empty()) {
        // SMB 代理端口耗尽或路径非法时 resolveLocalPath 返回空串，这里给出可读错误。
        node->videoError = true;
        node->videoErrorText = "视频地址不可用（SMB 代理未启动）";
        node->videoPlaying = false;
        current->videoPlaying[node->path] = false;
        current->dirty = true;
        dispatchVideoEvent(node, node->bindError, "error");
        return;
    }
    activeVideoPath = node->path;

    auto& player = nxkit::VideoView::instance();
    if (!player.isValid()) {
        node->videoError = true;
        node->videoErrorText = "视频播放器未初始化";
        node->videoPlaying = false;
        current->videoPlaying[node->path] = false;
        showToast("视频播放需要启用 libmpv 的构建", "none", 2000);
        dispatchVideoEvent(node, node->bindError, "error");
        return;
    }

    std::string& loaded = current->videoLoadedSrc[node->path];
    // 播完后再点播放：需要重新 load 才能从 0 开始。
    if (player.isEnded()) forceLoad = true;
    if (forceLoad || loaded != resolved) {
        std::string extra;
        if (node->videoInitialTime > 0.0) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "start=%.3f", node->videoInitialTime);
            extra = buffer;
        }
        // 需要鉴权的直链（如夸克转码后的 m3u8）必须带上 Cookie，否则 CDN 返回 412。
        // http-header-fields 走 mpv 全局 set（见 VideoView::setUrl）；start 是
        // loadfile 的 per-file 选项，只在首帧生效，不会影响后续 seek。
        player.setUrl(resolved, node->videoHeaders, extra);
        if (!node->videoSubtitle.empty()) {
            const std::string subtitle = resolveLocalPath(node->videoSubtitle, current->dir);
            if (!subtitle.empty()) player.addSubtitle(subtitle);
        }
        loaded = resolved;
        activeVideoSrc = resolved;
        node->videoMetadataDispatched = false;
    }
    if (node->videoPlaybackRate > 0.0) player.setSpeed(node->videoPlaybackRate);
    if (node->videoVolume >= 0) player.setVolume(node->videoVolume);
    player.setLoop(node->videoLoop);
    player.setMute(node->videoMuted);
    player.resume();
}

void MiniappView::stopVideo() {
    nxkit::VideoView::instance().stop();
    activeVideoPath.clear();
    activeVideoSrc.clear();
}

void MiniappView::dispatchVideoEvent(RenderNode* node, const std::string& handler,
                                     const std::string& type) {
    if (!node || handler.empty() || !current || !current->runtime) return;
    JsonValue detail = JsonValue::object();
    detail.setMember("currentTime", JsonValue::number(node->videoTime));
    detail.setMember("duration", JsonValue::number(node->videoDuration));
    if (type == "error") {
        detail.setMember("errMsg", JsonValue::string(node->videoErrorText));
    } else if (type == "fullscreenchange") {
        detail.setMember("fullScreen", JsonValue::boolean(!fullscreenVideoPath.empty()));
        detail.setMember("direction", JsonValue::string("horizontal"));
    }
    current->runtime->dispatchEvent(handler, type, node->dataset, detail);
}

bool MiniappView::videoControl(const std::string& id, const std::string& action,
                               double value, const std::string& text) {
    if (!current || !current->root || id.empty()) return false;
    RenderNode* node = findNodeById(*current->root, id);
    if (!node || node->tag != "video") return false;

    if (action == "play") {
        if (!node->videoPlaying) toggleVideo(node);
    } else if (action == "pause") {
        if (node->videoPlaying) toggleVideo(node);
    } else if (action == "stop") {
        if (node->videoPlaying) toggleVideo(node);
        node->videoTime = 0.0;
        current->videoTime[node->path] = 0.0;
    } else if (action == "seek") {
        if (value < 0.0) value = 0.0;
        node->videoTime = value;
        current->videoTime[node->path] = value;
        if (activeVideoPath == node->path) {
            nxkit::VideoView::instance().seek(value);
        }
    } else if (action == "requestFullScreen") {
        enterVideoFullscreen(node->path);
    } else if (action == "exitFullScreen") {
        exitVideoFullscreen();
    } else if (action == "setPlaybackRate") {
        node->videoPlaybackRate = std::max(0.0, value);
        if (activeVideoPath == node->path) {
            nxkit::VideoView::instance().setSpeed(node->videoPlaybackRate > 0.0
                                                      ? node->videoPlaybackRate
                                                      : 1.0);
        }
    } else if (action == "setVolume") {
        node->videoVolume = static_cast<int>(std::max(0.0, std::min(100.0, value)));
        if (activeVideoPath == node->path) {
            nxkit::VideoView::instance().setVolume(node->videoVolume);
        }
    } else if (action == "addSubtitle") {
        node->videoSubtitle = text;
        if (activeVideoPath == node->path && !text.empty()) {
            const std::string subtitle = resolveLocalPath(text, current->dir);
            if (!subtitle.empty()) nxkit::VideoView::instance().addSubtitle(subtitle);
        }
    }
    return true;
}

void MiniappView::enterVideoFullscreen(const std::string& path) {
    if (!current || !current->root || path.empty()) return;
    fullscreenVideoPath = path;
    current->dirty = true;
    RenderNode* node = findNodeByPath(*current->root, path);
    if (node) {
        // 从未开始播放过的 video：进全屏时顺便起播，否则全屏只有黑屏。
        if (!node->videoPlaying &&
            current->videoStarted.find(path) == current->videoStarted.end()) {
            toggleVideo(node);
        }
        dispatchVideoEvent(node, node->bindFullscreenChange, "fullscreenchange");
    }
}

void MiniappView::exitVideoFullscreen() {
    if (!current || !current->root || fullscreenVideoPath.empty()) return;
    RenderNode* node = findNodeByPath(*current->root, fullscreenVideoPath);
    fullscreenVideoPath.clear();
    current->dirty = true;
    if (node) dispatchVideoEvent(node, node->bindFullscreenChange, "fullscreenchange");
}

void MiniappView::triggerTap(RenderNode* node) {
    if (!node || node->bindTap.empty() || !current || !current->runtime) return;
    JsonValue detail = JsonValue::object();
    detail.setMember("x", JsonValue::number(node->absLeft + node->absWidth * 0.5f));
    detail.setMember("y", JsonValue::number(node->absTop + node->absHeight * 0.5f));
    current->runtime->dispatchEvent(node->bindTap, "tap", node->dataset, detail);
}

void MiniappView::triggerChange(RenderNode* node, const JsonValue& detail,
                                const std::string&) {
    if (!node || !current || !current->runtime) return;
    if (!node->bindChange.empty()) {
        current->runtime->dispatchEvent(node->bindChange, "change", node->dataset, detail);
    }
}

void MiniappView::setLocalValue(RenderNode* node, const std::string& value) {
    if (!node || !current) return;
    current->inputValues[node->path] = value;
    current->dirty = true;
}

void MiniappView::activateInput(RenderNode* node) {
    if (!node || !current) return;
    std::weak_ptr<bool> weak = aliveToken;
    std::string path = node->path;
    std::string initial = node->value;
    std::string placeholder = node->placeholder;
    std::string bindInput = node->bindInput;
    std::string bindConfirm = node->bindConfirm;
    std::map<std::string, std::string> dataset = node->dataset;

    brls::Application::getImeManager()->openForText(
        [this, weak, path, bindInput, bindConfirm, dataset](std::string text) {
            auto token = weak.lock();
            if (!token || !*token) return;
            if (!current) return;
            current->inputValues[path] = text;
            current->dirty = true;
            JsonValue detail = JsonValue::object();
            detail.setMember("value", JsonValue::string(text));
            if (!bindInput.empty()) {
                current->runtime->dispatchEvent(bindInput, "input", dataset, detail);
            }
            if (!bindConfirm.empty()) {
                current->runtime->dispatchEvent(bindConfirm, "confirm", dataset, detail);
            }
        },
        placeholder.empty() ? "输入内容" : placeholder, "", 200, initial);
}

bool MiniappView::handleDirection(brls::FocusDirection direction) {
    if (modal.active) {
        if (direction == brls::FocusDirection::LEFT) modal.focus = 0;
        if (direction == brls::FocusDirection::RIGHT) modal.focus = 1;
        return true;
    }
    RenderNode* node = findFocusByPath(focusPath);
    if (node && node->tag == "slider" &&
        (direction == brls::FocusDirection::LEFT ||
         direction == brls::FocusDirection::RIGHT)) {
        float value = valueOf(node);
        float step = node->sliderStep > 0.0f ? node->sliderStep : 1.0f;
        float next = direction == brls::FocusDirection::LEFT ? value - step : value + step;
        next = std::max(node->sliderMin, std::min(node->sliderMax, next));
        if (current) {
            current->sliderValues[node->path] = next;
            current->dirty = true;
            JsonValue detail = JsonValue::object();
            detail.setMember("value", JsonValue::number(next));
            triggerChange(node, detail, std::string());
        }
        return true;
    }
    return moveFocus(node, direction);
}

bool MiniappView::moveFocus(RenderNode* from, brls::FocusDirection direction) {
    if (!from || focusNodes.empty()) {
        if (!focusNodes.empty()) {
            focusPath = focusNodes.front()->path;
            ensureVisible(focusNodes.front());
            return true;
        }
        return false;
    }

    RenderNode* best = nullptr;
    float bestScore = 1e9f;
    float fromX = from->absLeft + from->absWidth * 0.5f;
    float fromY = from->absTop + from->absHeight * 0.5f;

    for (RenderNode* candidate : focusNodes) {
        if (candidate == from) continue;
        float cx = candidate->absLeft + candidate->absWidth * 0.5f;
        float cy = candidate->absTop + candidate->absHeight * 0.5f;
        float dx = cx - fromX;
        float dy = cy - fromY;

        float primary = 0.0f;
        float secondary = 0.0f;
        // 横向/纵向移动要求目标与当前节点在另一轴上有一定重叠，且位移至少达到节点
        // 尺寸的 40%。这样双列卡片会按「先本列上下、再左右换列」的直觉移动，
        // 不会因为资讯行与卡片高度不齐而误跳到资讯区。
        switch (direction) {
            case brls::FocusDirection::RIGHT:
            case brls::FocusDirection::LEFT: {
                float overlap = std::min(from->absTop + from->absHeight,
                                          candidate->absTop + candidate->absHeight) -
                                std::max(from->absTop, candidate->absTop);
                if (overlap < std::min(from->absHeight, candidate->absHeight) * 0.25f) continue;
                float step = std::min(from->absWidth, candidate->absWidth) * 0.4f;
                if (std::fabs(dx) < step) continue;
                primary = std::fabs(dx);
                secondary = std::fabs(dy);
                break;
            }
            case brls::FocusDirection::DOWN:
            case brls::FocusDirection::UP: {
                float overlap = std::min(from->absLeft + from->absWidth,
                                          candidate->absLeft + candidate->absWidth) -
                                std::max(from->absLeft, candidate->absLeft);
                if (overlap < std::min(from->absWidth, candidate->absWidth) * 0.25f) continue;
                float step = std::min(from->absHeight, candidate->absHeight) * 0.4f;
                if (std::fabs(dy) < step) continue;
                primary = std::fabs(dy);
                secondary = std::fabs(dx);
                break;
            }
            default:
                continue;
        }
        // 排除与按键方向相反的目标。
        if (direction == brls::FocusDirection::RIGHT && dx <= 0.0f) continue;
        if (direction == brls::FocusDirection::LEFT && dx >= 0.0f) continue;
        if (direction == brls::FocusDirection::DOWN && dy <= 0.0f) continue;
        if (direction == brls::FocusDirection::UP && dy >= 0.0f) continue;
        float score = primary + secondary * 0.6f;
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }

    if (best) {
        focusPath = best->path;
        ensureVisible(best);
        return true;
    }
    return false;
}

RenderNode* MiniappView::findFocusByPath(const std::string& path) const {
    if (path.empty() || !current || !current->root) return nullptr;
    std::vector<RenderNode*> stack{current->root.get()};
    while (!stack.empty()) {
        RenderNode* node = stack.back();
        stack.pop_back();
        if (node->path == path) return node;
        for (auto& child : node->children) stack.push_back(child.get());
    }
    return nullptr;
}

RenderNode* MiniappView::firstFocusable() const {
    return focusNodes.empty() ? nullptr : focusNodes.front();
}

void MiniappView::ensureVisible(RenderNode* node) {
    if (!node || !current) return;
    // 必须以焦点节点自身的矩形计算滚动目标。之前把目标退化成「最近的非滚动祖先」，
    // 推荐区的 app-grid 包住了整列卡片，它总是比视口高，于是永远只能滚到底部，
    // 焦点回到第一行时卡片不会滚出来（右列最近使用没有外层容器，所以不受影响）。
    for (RenderNode* parent = node->parent; parent; parent = parent->parent) {
        if (!parent->isScrollView()) continue;
        // 横向列表（继续观看、数据源筛选等）按 X 轴滚动，否则焦点移出右侧后
        // 列表永远停在原位，用户就没法看到后面的卡片。
        if (parent->horizontal) {
            float contentLeft = node->absLeft - parent->absLeft + parent->scrollOffset;
            float contentRight = contentLeft + node->absWidth;
            float hOffset = parent->scrollOffset;
            float visibleRight = hOffset + parent->absWidth;
            if (contentLeft < hOffset) {
                hOffset = contentLeft - 12.0f;
            } else if (contentRight > visibleRight) {
                hOffset = contentRight - parent->absWidth + 12.0f;
            }
            float maxHOffset = std::max(0.0f, parent->contentWidth - parent->absWidth);
            hOffset = std::max(0.0f, std::min(hOffset, maxHOffset));
            parent->scrollOffset = hOffset;
            current->scrollOffsets[parent->path] = hOffset;
            continue;
        }
        float contentTop = node->absTop - parent->absTop + parent->scrollOffset;
        float contentBottom = contentTop + node->absHeight;
        float offset = parent->scrollOffset;
        float visibleBottom = offset + parent->absHeight;
        if (contentTop < offset) {
            offset = contentTop - 12.0f;
        } else if (contentBottom > visibleBottom) {
            offset = contentBottom - parent->absHeight + 12.0f;
        }
        float maxOffset = std::max(0.0f, parent->contentHeight - parent->absHeight);
        offset = std::max(0.0f, std::min(offset, maxOffset));
        parent->scrollOffset = offset;
        current->scrollOffsets[parent->path] = offset;
    }
}

void MiniappView::tick(double now) {
    if (this->pendingLoad) {
        if (this->loadDelayFrames > 0) {
            // 留一帧给加载页，确保窗口里先出现反馈再开始读包。
            --this->loadDelayFrames;
            // 加载页的旋转指示需要持续重绘。
            this->invalidate();
        } else {
            this->pendingLoad = false;
            this->loadPackage();
            this->invalidate();
            return;
        }
    }
    flushPendingNavigation();
    if (!current) return;
    overlayClock = now;
    if (current->runtime) current->runtime->pumpTimers(now);

    if (!toastText.empty() && now >= toastUntil) toastText.clear();

    // swiper 自动轮播。
    if (current->root) {
        std::vector<RenderNode*> stack{current->root.get()};
        while (!stack.empty()) {
            RenderNode* node = stack.back();
            stack.pop_back();
            if (node->tag == "swiper" && node->autoplay && node->swiperCount > 1) {
                auto it = current->swiperLast.find(node->path);
                if (it == current->swiperLast.end()) {
                    current->swiperLast[node->path] = now;
                } else if (now - it->second >= node->swiperInterval) {
                    int next = (node->swiperIndex + 1) % node->swiperCount;
                    current->swiperIndex[node->path] = next;
                    current->swiperLast[node->path] = now;
                    current->dirty = true;
                    break;
                }
            } else if (node->tag == "video") {
                pumpVideo(*current, *node, now);
            }
            for (auto& child : node->children) stack.push_back(child.get());
        }
    }
}

void MiniappView::pumpVideo(PageState& page, RenderNode& node, double now) {
    (void)now;
    if (node.videoSrc.empty()) return;

    // autoplay：首次布局时触发一次加载，之后状态由 VideoView 回调驱动。
    if (node.videoAutoplay && page.videoStarted.find(node.path) == page.videoStarted.end()) {
        page.videoStarted[node.path] = true;
        node.videoPlaying = true;
        page.videoPlaying[node.path] = true;
        node.videoError = false;
        node.videoErrorText.clear();
        page.dirty = true;
        startVideo(&node, true);
        if (node.videoPlaying) dispatchVideoEvent(&node, node.bindPlay, "play");
    }
}

// 由 nxkit::VideoView（wiliwili 风格播放器）的 MpvEvent 回调驱动，
// 替代原先每帧轮询 mpv 的 pumpVideo 状态同步。
void MiniappView::onVideoState(const nxkit::VideoState& vs) {
    if (!current || activeVideoPath.empty()) return;
    RenderNode* node = findNodeByPath(*current->root, activeVideoPath);
    if (!node || node->tag != "video") return;

    bool dirty = false;

    if (vs.currentTime != node->videoTime) {
        node->videoTime = vs.currentTime;
        current->videoTime[node->path] = vs.currentTime;
        dirty = true;
        auto last = current->videoLastUpdate.find(node->path);
        if (last == current->videoLastUpdate.end() ||
            std::abs(static_cast<int>(vs.currentTime) - static_cast<int>(last->second)) >= 1.0) {
            current->videoLastUpdate[node->path] = vs.currentTime;
            dispatchVideoEvent(node, node->bindTimeUpdate, "timeupdate");
        }
    }

    if (vs.duration != node->videoDuration) {
        node->videoDuration = vs.duration;
        dirty = true;
    }

    if (vs.playing != node->videoPlaying) {
        node->videoPlaying = vs.playing;
        current->videoPlaying[node->path] = vs.playing;
        dirty = true;
    }

    if (vs.error && !node->videoError) {
        node->videoError = true;
        node->videoErrorText = vs.errorText;
        node->videoPlaying = false;
        current->videoPlaying[node->path] = false;
        dirty = true;
        dispatchVideoEvent(node, node->bindError, "error");
    }

    if (vs.metadataLoaded && !node->videoMetadataDispatched) {
        node->videoMetadataDispatched = true;
        dispatchVideoEvent(node, node->bindLoadedMetadata, "loadedmetadata");
    }

    // waiting / progress：缓冲开始与恢复，供上层做加载指示。
    if (vs.waiting != node->videoWaiting) {
        node->videoWaiting = vs.waiting;
        dispatchVideoEvent(node, vs.waiting ? node->bindWaiting : node->bindProgress,
                           vs.waiting ? "waiting" : "progress");
    }

    // seekcomplete：由播放器在 seeking 结束时自增序号触发一次。
    if (vs.seekSerial != node->videoSeekSerial) {
        node->videoSeekSerial = vs.seekSerial;
        dispatchVideoEvent(node, node->bindSeekComplete, "seekcomplete");
    }

    if (vs.ended && !node->videoLoop) {
        if (node->videoPlaying) {
            node->videoPlaying = false;
            current->videoPlaying[node->path] = false;
            dirty = true;
        }
        node->videoTime = node->videoDuration;
        dispatchVideoEvent(node, node->bindEnded, "ended");
    }

    if (dirty) current->dirty = true;
}

// ---------------- 宿主能力 ----------------

void MiniappView::showToast(const std::string& title, const std::string& icon,
                            int durationMs) {
    toastText = title;
    toastIcon = icon;
    toastUntil = steadyMilliseconds() + std::max(200, durationMs);
}

void MiniappView::showLoading(const std::string& title) {
    loadingVisible = true;
    loadingText = title.empty() ? "加载中" : title;
}

void MiniappView::hideLoading() {
    loadingVisible = false;
}

void MiniappView::showModal(const std::string& title, const std::string& content,
                            bool showCancel, std::function<void(bool)> done) {
    modal = ModalState{};
    modal.active = true;
    modal.title = title.empty() ? "提示" : title;
    modal.content = content;
    modal.showCancel = showCancel;
    modal.focus = showCancel ? 0 : 1;
    modal.done = std::move(done);
}

void MiniappView::setNavigationBarTitle(const std::string& title) {
    navigationTitle = title;
    if (current) current->title = title;
}

void MiniappView::httpRequest(const std::string& method, const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body, int timeoutMs, HttpDone done) {
    nxkit::HttpClient client;
    std::string contentType = "application/json";
    for (const auto& entry : headers) {
        if (entry.first == "Content-Type" || entry.first == "content-type") {
            contentType = entry.second;
        }
        client.setHeader(entry.first, entry.second);
    }
    client.setTimeout(timeoutMs > 0 ? timeoutMs : 15000);

    // 首页小程序用相对路径请求自己的服务端，这里补上商店地址。
    std::string target = url;
    if (!target.empty() && target[0] == '/' && MiniAppApi::instance().ready()) {
        target = MiniAppApi::instance().serverUrl() + target;
    }

    // 任意方法统一走 requestAsync：方法名由脚本给出，非 2xx 也返回响应体，
    // 由 wx.request 按微信语义决定调用 success 还是 fail。
    client.requestAsync(method, target, body, body.empty() ? std::string() : contentType,
                        [done, target, method](const nxkit::HttpResult& result) {
        if (result.transportError) {
            brls::Logger::error("miniapp httpRequest {} {} transport error: {}",
                                method, target, result.error);
        } else if (!result.ok) {
            brls::Logger::warning("miniapp httpRequest {} {} status={}", method, target,
                                  static_cast<int>(result.status));
        }
        done(!result.transportError, static_cast<int>(result.status), result.body,
             result.headers, result.cookies, result.error);
    });
}

bool MiniappView::navigateTo(const std::string& url) {
    return openPage(url, true);
}

namespace {

// 用 pthread 直接起分离线程：std::thread 在 pthread_create/pthread_detach 失败时会
// 抛异常，而临时对象析构时仍 joinable 就会直接 std::terminate 退出整个容器
// （真机内存/线程资源紧张时已多次触发）。pthread API 只返回错误码，不会抛异常。
struct SmbConnectJob {
    JsRuntimeHost::SmbDone done;
    std::string server;
    std::string share;
    std::string user;
    std::string password;
    std::string domain;
};

struct SmbListJob {
    JsRuntimeHost::SmbDone done;
    std::string id;
    std::string path;
};

struct SmbScanJob {
    JsRuntimeHost::SmbDone done;
    std::function<void(int, int)> progress;
    std::string id;
    std::string path;
    int maxDepth = 4;
    int maxEntries = 200;
};

void* runSmbScanJob(void* raw) {
    std::unique_ptr<SmbScanJob> job(static_cast<SmbScanJob*>(raw));
    std::string error;
    std::vector<SmbEntry> entries;
    try {
        entries = SmbManager::instance().scan(job->id, job->path, job->maxDepth,
                                              job->maxEntries, error, job->progress);
    } catch (const std::exception& e) {
        error = e.what() ? e.what() : "SMB 扫描异常";
    } catch (...) {
        error = "SMB 扫描异常";
    }
    std::vector<JsonValue> items;
    items.reserve(entries.size());
    for (const auto& e : entries) {
        JsonValue item = JsonValue::object();
        item.setMember("name", JsonValue::string(e.name));
        item.setMember("size", JsonValue::number(static_cast<double>(e.size)));
        items.push_back(item);
    }
    job->done(error.empty(), toJsonString(JsonValue::array(items)), error);
    return nullptr;
}

void* runSmbConnectJob(void* raw) {
    std::unique_ptr<SmbConnectJob> job(static_cast<SmbConnectJob*>(raw));
    std::string error;
    std::string id;
    try {
        id = SmbManager::instance().connect(job->server, job->share, job->user, job->password,
                                            job->domain, error);
    } catch (const std::exception& e) {
        error = e.what() ? e.what() : "SMB 连接异常";
    } catch (...) {
        error = "SMB 连接异常";
    }
    job->done(!id.empty(), id, error);
    return nullptr;
}

void* runSmbListJob(void* raw) {
    std::unique_ptr<SmbListJob> job(static_cast<SmbListJob*>(raw));
    std::string error;
    std::vector<SmbEntry> entries;
    try {
        entries = SmbManager::instance().listDir(job->id, job->path, error);
    } catch (const std::exception& e) {
        error = e.what() ? e.what() : "SMB 列目录异常";
    } catch (...) {
        error = "SMB 列目录异常";
    }
    std::vector<JsonValue> items;
    items.reserve(entries.size());
    for (const auto& e : entries) {
        JsonValue item = JsonValue::object();
        item.setMember("name", JsonValue::string(e.name));
        item.setMember("isDir", JsonValue::boolean(e.isDir));
        item.setMember("size", JsonValue::number(static_cast<double>(e.size)));
        items.push_back(item);
    }
    job->done(error.empty(), toJsonString(JsonValue::array(items)), error);
    return nullptr;
}

bool startDetached(void* (*entry)(void*), void* arg) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int rc = pthread_create(&thread, &attr, entry, arg);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        brls::Logger::error("miniapp: pthread_create failed rc={}", rc);
    }
    return rc == 0;
}

}  // namespace


void MiniappView::smbConnect(const std::string& server, const std::string& share,
                             const std::string& user, const std::string& password,
                             const std::string& domain, SmbDone done) {
    // 连接/列目录不经过 HTTP 代理，代理只在真正要播放时才启动（见 smbStreamUrl），
    // 避免小程序启动阶段就拉起后台网络线程。
    SmbConnectJob* job = new (std::nothrow) SmbConnectJob{};
    if (!job) {
        brls::Logger::error("smbConnect: out of memory");
        done(false, "", "内存不足，无法连接 SMB");
        return;
    }
    job->done = done;
    job->server = server;
    job->share = share;
    job->user = user;
    job->password = password;
    job->domain = domain;
    if (!startDetached(runSmbConnectJob, job)) {
        delete job;
        done(false, "", "无法创建 SMB 线程");
    }
}

void MiniappView::smbListFiles(const std::string& id, const std::string& path,
                               SmbDone done) {
    SmbListJob* job = new (std::nothrow) SmbListJob{};
    if (!job) {
        brls::Logger::error("smbListFiles: out of memory");
        done(false, "[]", "内存不足，无法读取 SMB 目录");
        return;
    }
    job->done = done;
    job->id = id;
    job->path = path;
    if (!startDetached(runSmbListJob, job)) {
        delete job;
        done(false, "[]", "无法创建 SMB 线程");
    }
}

void MiniappView::smbScan(const std::string& id, const std::string& path, int maxDepth,
                          int maxEntries, SmbDone done,
                          std::function<void(int, int)> progress) {
    SmbScanJob* job = new (std::nothrow) SmbScanJob{};
    if (!job) {
        brls::Logger::error("smbScan: out of memory");
        done(false, "[]", "内存不足，无法扫描 SMB 目录");
        return;
    }
    job->done = done;
    job->progress = std::move(progress);
    job->id = id;
    job->path = path;
    job->maxDepth = maxDepth;
    job->maxEntries = maxEntries;
    if (!startDetached(runSmbScanJob, job)) {
        delete job;
        done(false, "[]", "无法创建 SMB 线程");
    }
}

void MiniappView::smbCancelScan() { SmbManager::instance().cancelScan(); }

void MiniappView::mediaScan(const std::string& sourceId, const std::string& smbId,
                            const std::string& root, int maxDepth, int maxFiles,
                            SmbDone done) {
    struct MediaScanJob {
        SmbDone done;
        std::string sourceId;
        std::string smbId;
        std::string root;
        int maxDepth = 4;
        int maxFiles = 2000;
    };
    using Job = MediaScanJob;
    Job* job = new (std::nothrow) Job{};
    if (!job) {
        done(false, "{\"total\":0}", "内存不足");
        return;
    }
    job->done = done;
    job->sourceId = sourceId;
    job->smbId = smbId;
    job->root = root;
    job->maxDepth = maxDepth;
    job->maxFiles = maxFiles;
    void* rawJob = job;
    auto entry = [](void* raw) -> void* {
        std::unique_ptr<Job> self(static_cast<Job*>(raw));
        std::string error;
        int total = 0;
        try {
            total = MediaLibrary::instance().scanSmb(self->sourceId, self->smbId, self->root,
                                                    self->maxDepth, self->maxFiles, error);
        } catch (const std::exception& e) {
            error = e.what() ? e.what() : "媒体库扫描异常";
        } catch (...) {
            error = "媒体库扫描异常";
        }
        JsonValue result = JsonValue::object();
        result.setMember("total", JsonValue::number(total));
        self->done(error.empty(), toJsonString(result), error);
        return nullptr;
    };
    if (!startDetached(entry, rawJob)) {
        delete job;
        done(false, "{\"total\":0}", "无法创建媒体库线程");
    }
}

void MiniappView::mediaLibrary(const std::string& sourceId, int offset, int limit,
                               SmbDone done) {
    struct LibraryJob {
        SmbDone done;
        std::string sourceId;
        int offset = 0;
        int limit = 0;
    };
    using Job = LibraryJob;
    Job* job = new (std::nothrow) Job{};
    if (!job) {
        done(false, "{}", "内存不足");
        return;
    }
    job->done = done;
    job->sourceId = sourceId;
    job->offset = offset;
    job->limit = limit;
    // 读本地索引文件放到工作线程，避免在 JS 线程里同步回调造成 QuickJS 重入。
    auto entry = [](void* raw) -> void* {
        std::unique_ptr<Job> self(static_cast<Job*>(raw));
        std::string error;
        int total = 0;
        std::vector<MediaEntry> entries;
        try {
            entries = MediaLibrary::instance().page(self->sourceId, self->offset, self->limit,
                                                   total, error);
        } catch (const std::exception& e) {
            error = e.what() ? e.what() : "媒体库读取异常";
        } catch (...) {
            error = "媒体库读取异常";
        }
        std::vector<JsonValue> items;
        items.reserve(entries.size());
        for (const auto& e : entries) {
            JsonValue item = JsonValue::object();
            item.setMember("name", JsonValue::string(e.path));
            item.setMember("size", JsonValue::number(static_cast<double>(e.size)));
            if (!e.subtitle.empty()) {
                item.setMember("subtitle", JsonValue::string(e.subtitle));
            }
            items.push_back(item);
        }
        JsonValue payload = JsonValue::object();
        payload.setMember("total", JsonValue::number(total));
        payload.setMember("list", JsonValue::array(std::move(items)));
        self->done(error.empty(), toJsonString(payload), error);
        return nullptr;
    };
    if (!startDetached(entry, job)) {
        delete job;
        done(false, "{}", "无法创建媒体库线程");
    }
}

void MiniappView::smbDisconnect(const std::string& id) {
    SmbManager::instance().disconnect(id);
}

std::string MiniappView::smbStreamUrl(const std::string& id, const std::string& path) {
    SmbManager::instance().init();
    return SmbManager::instance().streamUrl(id, path);
}

bool MiniappView::navigateBack(int delta) {
    if (pages.size() <= 1) return false;
    pendingNavigation = [this, delta]() { performNavigateBack(delta); };
    return true;
}

void MiniappView::flushPendingNavigation() {
    if (!pendingNavigation) return;
    auto op = std::move(pendingNavigation);
    pendingNavigation = nullptr;
    op();
}

bool MiniappView::performNavigateBack(int delta) {
    if (pages.size() <= 1) return false;
    int target = std::max(1, static_cast<int>(pages.size()) - std::max(1, delta));
    while (static_cast<int>(pages.size()) > target) {
        PageState& page = *pages.back();
        if (page.runtime) {
            page.runtime->callLifecycle("onHide");
            page.runtime->callLifecycle("onUnload");
        }
        cleanupPage(page);
        pages.pop_back();
    }
    current = pages.back().get();
    if (current->runtime) current->runtime->callLifecycle("onShow");
    current->dirty = true;
    layoutWidth = -1.0f;
    layoutHeight = -1.0f;
    focusPath.clear();
    focusInitialized = false;
    focusNodes.clear();
    return true;
}

bool MiniappView::redirectTo(const std::string& url) {
    pendingNavigation = [this, url]() { performRedirectTo(url); };
    return true;
}

bool MiniappView::performRedirectTo(const std::string& url) {
    std::string path = stripQuery(url);
    auto page = std::make_unique<PageState>();
    loadPageFiles(*page, path, true);
    if (!page->loaded) {
        errorMessage = page->error;
        return false;
    }
    JsonValue query = queryFromUrl(url);
    if (page->runtime) {
        page->runtime->callLifecycle("onLoad", &query);
        page->runtime->callLifecycle("onShow");
        page->data = page->runtime->data();
    }
    if (current) {
        if (current->runtime) {
            current->runtime->callLifecycle("onHide");
            current->runtime->callLifecycle("onUnload");
        }
        auto old = std::move(pages.back());
        pages.pop_back();
        cleanupPage(*old);
    }
    pages.push_back(std::move(page));
    current = pages.back().get();
    current->dirty = true;
    layoutWidth = -1.0f;
    layoutHeight = -1.0f;
    focusPath.clear();
    focusInitialized = false;
    return true;
}

bool MiniappView::reLaunch(const std::string& url) {
    pendingNavigation = [this, url]() { performReLaunch(url); };
    return true;
}

bool MiniappView::performReLaunch(const std::string& url) {
    while (!pages.empty()) {
        PageState& page = *pages.back();
        if (page.runtime) {
            page.runtime->callLifecycle("onHide");
            page.runtime->callLifecycle("onUnload");
        }
        cleanupPage(page);
        pages.pop_back();
    }
    current = nullptr;
    return openPage(url, false);
}

namespace {

// 记录「最近使用」，供 wx.miniapp.listRecent 展示。
void rememberMiniApp(const std::string& id, const std::string& title,
                     const std::string& iconUrl) {
    auto& config = nxkit::Config::instance();
    nlohmann::json updated = nlohmann::json::array();
    updated.push_back(id);

    const nlohmann::json& data = config.data();
    if (data.is_object()) {
        auto section = data.find("miniapp");
        if (section != data.end() && section->is_object()) {
            auto recent = section->find("recent");
            if (recent != section->end() && recent->is_array()) {
                for (const auto& entry : *recent) {
                    if (!entry.is_string()) continue;
                    if (entry.get<std::string>() == id) continue;
                    if (updated.size() >= 9) break;
                    updated.push_back(entry);
                }
            }
        }
    }

    config.setValue("miniapp.recent", updated);
    if (!title.empty()) config.setString("miniapp.title." + id, title);
    // 启动时顺手把图标地址落盘，首页直接用，不必再查商店列表。
    if (!iconUrl.empty()) config.setString("miniapp.icon." + id, iconUrl);
    std::string usage = "miniapp.usage." + id + ".count";
    config.setInt(usage, config.getInt(usage, 0) + 1);
    config.save();
}

}  // namespace

bool MiniappView::launchMiniApp(const std::string& id, const std::string& title) {
    if (id.empty()) return false;

    // 先按商店目录校验版本：有更新（或未安装）则下载安装后进入；
    // 商店不可达但本地有包时退回本地版本，保证离线可用。
    std::weak_ptr<bool> weak = aliveToken;
    MiniAppApi::instance().fetchList([weak, id, title](const std::vector<MiniAppItem>& list,
                                                       const std::string& error) {
        const MiniAppItem* latest = nullptr;
        if (error.empty()) {
            for (const auto& candidate : list) {
                if (candidate.id == id) {
                    latest = &candidate;
                    break;
                }
            }
        }

        // 本地包已是最新（或拿不到商店数据但本地有包）：直接进入。
        if (!latest || packages::installed(id, latest->version)) {
            if (!packages::installed(id, "")) return;
            std::string iconUrl = latest ? latest->iconUrl : "";
            brls::sync([weak, id, title, iconUrl] {
                auto token = weak.lock();
                if (!token || !*token) return;
                // 调用方（首页卡片）通常知道应用名；没有时沿用已有的缓存标题。
                std::string name = title;
                if (name.empty()) {
                    name = nxkit::Config::instance().getString("miniapp.title." + id, "");
                }
                rememberMiniApp(id, name, iconUrl);
                MiniAppApi::instance().reportOpen(id);
                brls::Application::pushActivity(new MiniappActivity(packages::directory(id)));
            });
            return;
        }

        MiniAppItem item = *latest;
        brls::sync([item] {
            brls::Application::notify("正在下载 " + item.title);
        });
        MiniAppApi::instance().download(
            id, [weak, item](const std::string& archive, const std::string& dlError) {
                brls::sync([weak, item, archive, dlError] {
                    auto token = weak.lock();
                    if (!token || !*token) return;
                    std::string installError;
                    if (!dlError.empty() || archive.empty() ||
                        !packages::install(item.id, item.version, archive, installError)) {
                        brls::Logger::warning("miniapp: install {} failed: {}", item.id,
                                              dlError.empty() ? installError : dlError);
                        // 更新下载失败时若本地有旧版本，仍进入旧版本。
                        if (!packages::installed(item.id, "")) return;
                        brls::Application::notify("更新失败，进入旧版本");
                        rememberMiniApp(item.id, item.title, item.iconUrl);
                        MiniAppApi::instance().reportOpen(item.id);
                        brls::Application::pushActivity(
                            new MiniappActivity(packages::directory(item.id)));
                        return;
                    }
                    rememberMiniApp(item.id, item.title, item.iconUrl);
                    MiniAppApi::instance().reportOpen(item.id);
                    brls::Application::pushActivity(
                        new MiniappActivity(packages::directory(item.id)));
                });
            });
    });
    return true;
}

std::string MiniappView::deviceId() { return MiniAppApi::instance().deviceId(); }

JsonValue MiniappView::recentMiniApps() {
    std::vector<JsonValue> items;
    const nlohmann::json& data = nxkit::Config::instance().data();
    if (!data.is_object()) return JsonValue::array();
    auto section = data.find("miniapp");
    if (section == data.end() || !section->is_object()) return JsonValue::array();
    auto recent = section->find("recent");
    if (recent == section->end() || !recent->is_array()) return JsonValue::array();

    for (const auto& entry : *recent) {
        if (!entry.is_string()) continue;
        std::string id = entry.get<std::string>();
        JsonValue item = JsonValue::object();
        item.setMember("id", JsonValue::string(id));
        item.setMember("title", JsonValue::string(
                                    nxkit::Config::instance().getString("miniapp.title." + id, id)));
        item.setMember("count", JsonValue::number(
                                    nxkit::Config::instance().getInt(
                                        "miniapp.usage." + id + ".count", 0)));
        item.setMember("icon", JsonValue::string(
                                   nxkit::Config::instance().getString(
                                       "miniapp.icon." + id, "")));
        item.setMember("installed", JsonValue::boolean(packages::installed(id, "")));
        items.push_back(std::move(item));
    }
    return JsonValue::array(std::move(items));
}

JsonValue MiniappView::systemInfo() {
    JsonValue info = JsonValue::object();
    info.setMember("brand", JsonValue::string("Nintendo"));
    info.setMember("model", JsonValue::string("Switch"));
    info.setMember("system", JsonValue::string("Horizon"));
    info.setMember("platform", JsonValue::string("switch"));
    info.setMember("windowWidth", JsonValue::number(1280.0));
    info.setMember("windowHeight", JsonValue::number(720.0));
    info.setMember("screenWidth", JsonValue::number(1280.0));
    info.setMember("screenHeight", JsonValue::number(720.0));
    info.setMember("pixelRatio", JsonValue::number(1.0));
    info.setMember("statusBarHeight", JsonValue::number(0.0));
    info.setMember("language", JsonValue::string("zh_CN"));
    info.setMember("version", JsonValue::string("1.1.2"));
    // 页面级按键事件（onKeyDown）能力标记，旧容器没有该字段。
    info.setMember("keyEvents", JsonValue::boolean(true));
#ifdef __SWITCH__
    auto* platform = brls::Application::getPlatform();
    if (platform && platform->hasEthernetConnection()) {
        info.setMember("networkType", JsonValue::string("ethernet"));
    } else if (platform && platform->hasWirelessConnection()) {
        info.setMember("networkType", JsonValue::string("wifi"));
    } else {
        info.setMember("networkType", JsonValue::string("none"));
    }
#else
    info.setMember("networkType", JsonValue::string("wifi"));
#endif
    return info;
}

void MiniappView::vibrateShort() {
    // Switch 上没有通用短振动接口，保持 no-op。
}

bool MiniappView::storageGet(const std::string& key, std::string& value) {
    auto it = storageValues.find(key);
    if (it != storageValues.end()) {
        value = it->second;
        return true;
    }
    // 按需从磁盘读单个 key，避免页面启动时把全部缓存都解析一遍。
    if (storageDir.empty()) return false;
    std::string text;
    if (!readTextFile(joinPath(storageDir, storageKeyFile(key)), text)) return false;
    storageValues[key] = text;
    value = text;
    return true;
}

void MiniappView::storageSet(const std::string& key, const std::string& value) {
    storageValues[key] = value;
    saveStorageKey(key, value);
}

void MiniappView::storageRemove(const std::string& key) {
    storageValues.erase(key);
    if (storageDir.empty()) return;
    std::error_code error;
    std::filesystem::remove(joinPath(storageDir, storageKeyFile(key)), error);
}

void MiniappView::storageClear() {
    storageValues.clear();
    if (storageDir.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(storageDir, error);
    nxkit::ensureDirectory(storageDir);
}

std::vector<std::string> MiniappView::storageKeys() {
    std::vector<std::string> keys;
    if (!storageDir.empty()) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(storageDir, error)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
            keys.push_back(storageKeyFromFile(name.substr(0, name.size() - 5)));
        }
    }
    for (const auto& entry : storageValues) {
        bool seen = false;
        for (const auto& key : keys) {
            if (key == entry.first) {
                seen = true;
                break;
            }
        }
        if (!seen) keys.push_back(entry.first);
    }
    return keys;
}

std::string MiniappView::clipboardGet() {
    return clipboardText;
}

void MiniappView::clipboardSet(const std::string& text) {
    clipboardText = text;
}

void MiniappView::loadStorage() {
    if (storageDir.empty()) return;
    // 旧版本把全部 key 塞进一个 JSON 文件，每次页面启动都要整包解析、每次写入都要
    // 全量重写（几千条影片缓存时非常慢）。这里把旧文件拆成「一个 key 一个文件」，
    // 之后启动只按需读用到的 key，写缓存也只写一个文件。
    if (storageFile.empty()) return;
    std::string text;
    if (!readTextFile(storageFile, text)) return;
    JsonValue parsed = parseJson(text);
    if (parsed.isObject()) {
        for (const auto& entry : parsed.asObject()) {
            const std::string value = toJsonString(entry.second);
            saveStorageKey(entry.first, value);
        }
    }
    storageValues.clear();
    std::error_code error;
    std::filesystem::remove(storageFile, error);
}

void MiniappView::saveStorageKey(const std::string& key, const std::string& value) {
    if (storageDir.empty()) return;
    std::ofstream file(joinPath(storageDir, storageKeyFile(key)),
                       std::ios::trunc | std::ios::binary);
    if (file) file << value;
}

}  // namespace miniapp
