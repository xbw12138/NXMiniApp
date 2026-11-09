//
// miniapp: 长生命周期的 QuickJS 运行环境。
//
// 一个 JsRuntime 对应一个页面实例：持有 JSContext，注入 Page/App/wx/定时器，
// 通过 JsRuntimeHost 把宿主能力（渲染、存储、网络、导航、弹窗）暴露给脚本。
//

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <quickjs.h>

#include "miniapp/json.hpp"

namespace miniapp {

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
    /// 读取媒体库一页（JSON 数组），instant 无网络请求。
    virtual void mediaLibrary(const std::string& sourceId, int offset, int limit,
                              SmbDone done) {}
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
};

/// 页面脚本运行时。
class JsRuntime {
public:
    JsRuntime(JsRuntimeHost* host, std::string packageDir, std::string pagePath,
              std::string appSource = "");
    ~JsRuntime();

    JsRuntime(const JsRuntime&) = delete;
    JsRuntime& operator=(const JsRuntime&) = delete;

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

    /// 页面当前 data。
    const JsonValue& data() const { return data_; }

    /// 在 data + 局部变量作用域中求值 {{...}} 表达式。
    bool evaluate(const std::string& expression, const JsonValue& scope, JsonValue& out,
                  std::string& error);

    /// 每帧驱动 setTimeout/setInterval。
    void pumpTimers(double nowMilliseconds);

    bool hasPendingTimers() const { return !timers.empty(); }

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

    std::string serializeJs(JSValue value, bool* ok);
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
    /// 定期探测网络状态并按需派发 wx.onNetworkStatusChange 回调。
    void pollNetworkStatus(double nowMilliseconds);

    JsRuntimeHost* host = nullptr;
    std::string packageDir;
    std::string pagePath;
    std::string appSource;
    std::string appJsonText;
    std::string appError;

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
    std::vector<JSValue> networkCallbacks;
    std::string lastNetworkType;
    double lastNetworkCheck = 0.0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

}  // namespace miniapp
