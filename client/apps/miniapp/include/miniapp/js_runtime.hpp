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
    using HttpDone = std::function<void(bool ok, int status, const std::string& body,
                                       const std::map<std::string, std::string>& headers)>;
    virtual void httpRequest(const std::string& method, const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             const std::string& body, HttpDone done) = 0;

    virtual bool navigateTo(const std::string& url) = 0;
    virtual bool navigateBack(int delta) = 0;
    virtual bool redirectTo(const std::string& url) = 0;
    virtual bool reLaunch(const std::string& url) = 0;

    /// wx.miniapp：启动另一个小程序 / 读取最近使用列表。
    virtual bool launchMiniApp(const std::string& /*id*/) { return false; }
    virtual JsonValue recentMiniApps() { return JsonValue::array(); }

    virtual JsonValue systemInfo() = 0;
    virtual void vibrateShort() {}

    virtual bool storageGet(const std::string& key, std::string& value) = 0;
    virtual void storageSet(const std::string& key, const std::string& value) = 0;
    virtual void storageRemove(const std::string& key) = 0;
    virtual void storageClear() = 0;
    virtual std::vector<std::string> storageKeys() { return {}; }

    virtual std::string clipboardGet() { return {}; }
    virtual void clipboardSet(const std::string&) {}

    /// wx.createVideoContext 背后的宿主能力。
    virtual bool videoControl(const std::string& id, const std::string& action,
                              double value) {
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
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

}  // namespace miniapp
