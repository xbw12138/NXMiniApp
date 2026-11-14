//
// miniapp: 长生命周期的 QuickJS 运行环境。
//
// 一个 JsRuntime 对应一个页面实例：持有 JSContext，注入 Page/App/wx/定时器，
// 通过 JsRuntimeHost 把宿主能力（渲染、存储、网络、导航、弹窗）暴露给脚本。
//

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <quickjs.h>

#include "miniapp/json.hpp"

namespace miniapp {

/// WXML 构建期的局部变量（wx:for 的 item/index 等）。
struct EvalLocal {
    std::string name;
    JsonValue value;
};

/// 脚本可调用的宿主能力。
class JsRuntimeHost {
public:
    virtual ~JsRuntimeHost() = default;

    /// setData 之后触发；宿主在合适的时机重建渲染树。
    virtual void onDataChanged() = 0;

    virtual void showToast(const std::string& title, const std::string& icon,
                           int durationMs) = 0;
    virtual void showLoading(const std::string& title) = 0;
    virtual void hideLoading() = 0;
    virtual void showModal(const std::string& title, const std::string& content,
                           bool showCancel,
                           std::function<void(bool confirm)> done) = 0;
    virtual void setNavigationBarTitle(const std::string& title) = 0;

    /// 异步网络请求；done 可能在工作线程被调用。
    /// transportOk=false 表示没拿到任何 HTTP 响应（DNS/连接/超时/TLS 失败）；
    /// 只要拿到了响应（含 4xx/5xx），transportOk 都为 true，由脚本按 status 自行处理。
    using HttpDone = std::function<void(bool transportOk, int status, const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       const std::map<std::string, std::string>& cookies,
                                       const std::string& error)>;
    virtual void httpRequest(const std::string& method, const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             const std::string& body, int timeoutMs, HttpDone done) = 0;

    /// SMB 数据源能力。done 可能在工作线程被调用。
    /// result 语义：connect 成功时为会话 id，listFiles 成功时为 JSON 数组字符串。
    using SmbDone = std::function<void(bool ok, const std::string& result,
                                       const std::string& error)>;
    virtual void smbConnect(const std::string& server, const std::string& share,
                            const std::string& user, const std::string& password,
                            const std::string& domain, SmbDone done) {}
    virtual void smbListFiles(const std::string& id, const std::string& path,
                              SmbDone done) {}
    /// 一次递归扫描目录树（原生侧完成遍历），result 为文件数组 JSON。
    /// progress 在工作线程被调用，携带 (已遍历目录数, 已收集文件数)。
    virtual void smbScan(const std::string& id, const std::string& path, int maxDepth,
                         int maxEntries, SmbDone done,
                         std::function<void(int, int)> progress = {}) {}
    /// 取消正在进行的 SMB 扫描（非阻塞）。
    virtual void smbCancelScan() {}
    /// 媒体库增量扫描：复用已有 SMB 会话，按目录 mtime 决定是否重列，结果落盘。
    virtual void mediaScan(const std::string& sourceId, const std::string& smbId,
                           const std::string& root, int maxDepth, int maxFiles, SmbDone done) {}
    /// 读取媒体库一页（JSON 数组），instant 无网络请求。root 需与扫描时一致。
    virtual void mediaLibrary(const std::string& sourceId, const std::string& root, int offset,
                              int limit, SmbDone done) {}
    virtual void smbDisconnect(const std::string& id) {}
    /// 生成本地 HTTP 代理播放地址（同步，不阻塞）。
    virtual std::string smbStreamUrl(const std::string& id, const std::string& path) {
        return {};
    }

    virtual bool navigateTo(const std::string& url) = 0;
    virtual bool navigateBack(int delta) = 0;
    virtual bool redirectTo(const std::string& url) = 0;
    virtual bool reLaunch(const std::string& url) = 0;

    /// wx.miniapp：启动另一个小程序 / 读取最近使用列表。
    /// title 可选，用于"最近使用"展示应用名。
    virtual bool launchMiniApp(const std::string& /*id*/,
                               const std::string& /*title*/ = "") {
        return false;
    }
    virtual JsonValue recentMiniApps() { return JsonValue::array(); }
    /// wx.miniapp.deviceId()：容器持久化的设备标识，用于服务端 UV 统计。
    virtual std::string deviceId() { return {}; }

    virtual JsonValue systemInfo() = 0;
    virtual void vibrateShort() {}

    virtual bool storageGet(const std::string& key, std::string& value) = 0;
    virtual void storageSet(const std::string& key, const std::string& value) = 0;
    virtual void storageRemove(const std::string& key) = 0;
    virtual void storageClear() = 0;
    virtual std::vector<std::string> storageKeys() { return {}; }

    virtual std::string clipboardGet() { return {}; }
    virtual void clipboardSet(const std::string&) {}

    /// wx.createVideoContext 背后的宿主能力。text 用于 addSubtitle 之类的字符串参数。
    virtual bool videoControl(const std::string& id, const std::string& action, double value,
                              const std::string& text = std::string()) {
        return false;
    }

    /// wx.getGamepadState：当前手柄按键与摇杆轴。
    virtual JsonValue gamepadState() { return JsonValue::object(); }

    /// wx.createInnerAudioContext 背后的宿主能力。
    virtual bool audioControl(const std::string& id, const std::string& action,
                              double value = 0.0,
                              const std::string& text = std::string()) {
        return false;
    }

    /// 音频实例当前状态；宿主不支持时返回空对象。
    virtual JsonValue audioState(const std::string& id) { return JsonValue::object(); }
};

/// 页面脚本运行时。
///
/// 线程模型：QuickJS 本身不是线程安全的，所有入口都用内部的递归锁串行化。
/// 约定哪个线程持锁，哪个线程就拥有这次脚本执行：
///   - 定时器 / 网络与 SMB 回调在逻辑线程执行（由宿主通过 setTaskPoster 提供队列）；
///   - 生命周期、事件、{{}} 求值在 UI 线程执行，用 try_lock 超时让帧不被脚本拖住。
class JsRuntime : public std::enable_shared_from_this<JsRuntime> {
public:
    JsRuntime(JsRuntimeHost* host, std::string packageDir, std::string pagePath,
              std::string appSource = "");
    ~JsRuntime();

    JsRuntime(const JsRuntime&) = delete;
    JsRuntime& operator=(const JsRuntime&) = delete;

    /// 逻辑线程的任务投递；为空时任务在调用线程同步执行。
    using TaskPoster = std::function<void(std::function<void()>)>;
    void setTaskPoster(TaskPoster poster);

    /// 把一段脚本调用投递到逻辑线程（持锁执行）。异步宿主回调都走这里。
    void postToScript(std::function<void()> task);

    /// 执行 app.js（如果有）和页面脚本；成功后 page 实例可用。
    bool initialize(const std::string& script, const std::string& filename,
                    std::string& error);

    /// 调用页面生命周期；不存在时返回 false。
    bool callLifecycle(const std::string& name, const JsonValue* query = nullptr);
    bool hasHandler(const std::string& name) const;

    /// 触发 bindtap/bindchange/bindinput 等事件。
    bool dispatchEvent(const std::string& handler, const std::string& type,
                       const std::map<std::string, std::string>& dataset,
                       const JsonValue& detail);

    /// 派发页面级按键事件 onKeyDown；key 为 up/down/left/right/a/b。
    /// 返回 true 表示页面注册了该事件；consumed 表示脚本返回真值，
    /// 容器应跳过默认的焦点移动 / 确认 / 返回逻辑。
    bool dispatchKey(const std::string& key, bool& consumed);

    /// 页面当前 data 的拷贝。data 由脚本线程更新，UI 线程只能拿快照。
    JsonValue dataCopy() const;
    /// 同上，但只等 timeoutMs 毫秒；脚本正忙时返回 false，调用方下一帧再取。
    bool tryDataCopy(JsonValue& out, int timeoutMs = 2) const;
    /// data 版本号：每次 setData 自增，UI 线程靠它决定要不要取快照。
    std::uint64_t dataVersion() const { return dataVersion_.load(std::memory_order_relaxed); }

    /// 渲染求值会话：把 page.data 这个 JS 对象本身当作作用域原型，
    /// 一次树构建里所有 {{...}} 共用它，不再为每个表达式重建整份 data。
    struct EvalSession {
        JSValue root = JS_UNDEFINED;
        // 会话期间持有脚本锁，由 endEvalSession 释放。
        bool ownsLock = false;
    };
    enum class SessionStatus {
        Ready,        // 拿到会话，可以求值
        Unavailable,  // 没有脚本或没有 data，走降级路径
        Busy,         // 脚本线程正忙，本轮跳过重建
    };
    SessionStatus beginEvalSession(EvalSession& session, int timeoutMs = 2);
    void endEvalSession(EvalSession& session);

    /// 在会话作用域 + locals 中求值 {{...}} 表达式。
    bool evaluate(const std::string& expression, const EvalSession& session,
                  const std::vector<EvalLocal>& locals, JsonValue& out,
                  std::string& error);

    /// 每帧驱动 setTimeout/setInterval。
    void pumpTimers(double nowMilliseconds);

    bool hasPendingTimers() const { return !timers.empty(); }

    /// wx.createCanvasContext 的绘制指令列表（retained mode，宿主每帧回放）。
    struct CanvasCommand {
        std::string kind;
        std::vector<double> nums;
        std::string text;
    };
    struct CanvasState {
        // 正在构建 / 已提交（ctx.draw() 交换）两套指令。
        std::vector<CanvasCommand> commands;
        std::vector<CanvasCommand> frame;

        // 绘制状态（颜色为 0..1 RGBA，alpha 已乘 globalAlpha）。
        float fill[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float stroke[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float lineWidth = 1.0f;
        float fontSize = 10.0f;
        int textAlign = 0;  // 0 left / 1 center / 2 right
        float globalAlpha = 1.0f;

        struct Saved {
            float fill[4];
            float stroke[4];
            float lineWidth;
            float fontSize;
            int textAlign;
            float globalAlpha;
        };
        std::vector<Saved> stack;
    };
    bool canvasSnapshot(const std::string& id, CanvasState& out) const;

private:
    friend struct RuntimeBridge;

    struct Timer {
        int id = 0;
        bool repeat = false;
        double next = 0.0;
        double interval = 0.0;
        JSValue fn = JS_UNDEFINED;
    };

    bool createContext(std::string& error);
    bool evaluateScript(const std::string& source, const std::string& filename,
                        std::string& error);
    bool finishPage(std::string& error);
    bool callJsFunction(JSValue fn, JSValue thisValue, int argc, JSValue* argv,
                        const std::string& what);
    void refreshData();
    void drainJobs();
    std::string exceptionText();
    /// QuickJS 的栈上限绑定在创建 context 的线程栈上，换线程执行前必须刷新，
    /// 否则会误报 "InternalError: stack overflow"。
    void syncStackTop();

    std::string serializeJs(JSValue value, bool* ok);
    /// 把脚本求值结果转成 JsonValue：基本类型直接读取，只有对象/数组才走 JSON 文本。
    bool jsonFromJs(JSValue value, JsonValue& out, std::string& error);
    JSValue valueFromJson(const JsonValue& value);
    JSValue valueFromJsonText(const std::string& text);
    JSValue makeEventValue(const std::string& type,
                           const std::map<std::string, std::string>& dataset,
                           const JsonValue& detail);
    void installGlobals();
    void installWx();

    /// 读取定时器数组中的函数。
    int addTimer(JSValue fn, double delay, bool repeat, double now);
    void removeTimer(int id);
    void clearTimers();
    /// requestAnimationFrame：回调在每帧 tick 时触发一次。
    int addAnimationFrame(JSValue fn);
    void cancelAnimationFrame(int id);
    void pumpAnimationFrames(double nowMilliseconds);
    /// canvas 2D 指令：kind 为操作名，参数从 argv 读取。
    void canvasOp(const std::string& id, const std::string& kind, JSContext* ctx,
                  int argc, JSValueConst* argv);

    /// WebSocket：建立连接、注册回调、发送与关闭。
    int openSocket(const std::string& url);
    void setSocketHandler(int id, const std::string& event, JSValue handler);
    void sendSocket(int id, const std::string& data);
    void closeSocket(int id);
    void pollSockets();

    /// wx.createInnerAudioContext：音频实例的创建、控制与状态回调。
    int createAudioContext();
    void audioAction(int id, const std::string& action, double value = 0.0,
                     const std::string& text = std::string());
    void setAudioHandler(int id, const std::string& event, JSValue handler);
    void pollAudio();
    double audioNumber(int id, const std::string& key) const;
    /// 定期探测网络状态并按需派发 wx.onNetworkStatusChange 回调。
    void pollNetworkStatus(double nowMilliseconds);

    JsRuntimeHost* host = nullptr;
    std::string packageDir;
    std::string pagePath;
    std::string appSource;
    std::string appJsonText;
    std::string appError;

    // 脚本锁：递归是因为 setData 会在持锁期间回调宿主（onDataChanged 等）。
    mutable std::recursive_timed_mutex mutex;
    // 逻辑线程队列；为空表示没有独立线程，任务就地执行。
    TaskPoster poster;
    std::atomic<std::uint64_t> dataVersion_{0};

    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JSValue page = JS_UNDEFINED;
    JSValue app = JS_UNDEFINED;
    bool pageRegistered = false;
    bool appRegistered = false;

    JsonValue data_;
    std::unordered_map<std::string, JSValue> expressionCache;
    std::vector<Timer> timers;
    int nextTimerId = 1;

    struct AnimationFrame {
        int id = 0;
        JSValue fn = JS_UNDEFINED;
    };
    std::vector<AnimationFrame> animationFrames;
    int nextAnimationFrameId = 1;

    std::map<std::string, CanvasState> canvases;

    // WebSocket 连接与回调。WsClient 定义在 .cpp 中，线程负责网络，回调在主线程触发。
    struct WsClient;
    struct SocketHandlers {
        JSValue onOpen = JS_UNDEFINED;
        JSValue onMessage = JS_UNDEFINED;
        JSValue onError = JS_UNDEFINED;
        JSValue onClose = JS_UNDEFINED;
    };
    std::map<int, std::shared_ptr<WsClient>> sockets;
    std::map<int, SocketHandlers> socketHandlers;
    int nextSocketId = 1;

    // 音频实例（wx.createInnerAudioContext）。播放由宿主负责，这里只保存参数
    // 与回调，并在每帧轮询宿主状态派发 play/pause/ended 等事件。
    struct AudioHandlers {
        JSValue onPlay = JS_UNDEFINED;
        JSValue onPause = JS_UNDEFINED;
        JSValue onStop = JS_UNDEFINED;
        JSValue onEnded = JS_UNDEFINED;
        JSValue onCanplay = JS_UNDEFINED;
        JSValue onError = JS_UNDEFINED;
        JSValue onSeeked = JS_UNDEFINED;
        JSValue onTimeUpdate = JS_UNDEFINED;
    };
    struct AudioInstance {
        std::string src;
        bool autoplay = false;
        bool loop = false;
        double volume = 1.0;
        double startTime = 0.0;
        bool destroyed = false;
        bool lastPlaying = false;
        bool lastLoaded = false;
        bool lastEnded = false;
        std::string lastError;
    };
    std::map<int, AudioInstance> audios;
    std::map<int, AudioHandlers> audioHandlers;
    int nextAudioId = 1;
    double lastAudioPoll = 0.0;
    std::vector<JSValue> networkCallbacks;
    std::string lastNetworkType;
    double lastNetworkCheck = 0.0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

}  // namespace miniapp
