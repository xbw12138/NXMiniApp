//
// miniapp: 长生命周期 QuickJS 运行时实现。
//

#include "miniapp/js_runtime.hpp"

#include <quickjs.h>
#include <mongoose.h>

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace miniapp {

struct RuntimeBridge;

namespace {

const char* kHelperScript = R"JS(
(function (g) {
  function isPlain(v) {
    return v && typeof v === 'object' && Object.prototype.toString.call(v) === '[object Object]';
  }
  g.__nx_merge = function (target, patch) {
    if (!patch || typeof patch !== 'object') return target;
    for (var key in patch) {
      if (!Object.prototype.hasOwnProperty.call(patch, key)) continue;
      var value = patch[key];
      if (key.indexOf('.') >= 0) {
        var parts = key.split('.');
        var obj = target;
        for (var i = 0; i < parts.length - 1; i++) {
          if (!isPlain(obj[parts[i]])) obj[parts[i]] = {};
          obj = obj[parts[i]];
        }
        obj[parts[parts.length - 1]] = value;
      } else if (isPlain(value) && isPlain(target[key])) {
        g.__nx_merge(target[key], value);
      } else {
        target[key] = value;
      }
    }
    return target;
  };
  g.__nx_parse = function (text) {
    if (typeof text !== 'string' || text === '') return '';
    try { return JSON.parse(text); } catch (e) { return text; }
  };
  g.__nx_next_tick = function (fn) { if (typeof fn === 'function') setTimeout(fn, 0); };
  // wx.createInnerAudioContext 的 JS 外壳：属性/方法转发到原生 __nx_audio_*。
  g.__nx_make_audio = function (id) {
    var ctx = {
      _id: id, _paused: true, _src: '', _volume: 1,
      _autoplay: false, _loop: false, _startTime: 0
    };
    function apply(key, value) {
      try { __nx_audio_action(id, key, value); } catch (e) {}
    }
    Object.defineProperties(ctx, {
      src: {
        enumerable: true,
        get: function () { return ctx._src; },
        set: function (v) { ctx._src = v == null ? '' : String(v); apply('src', ctx._src); }
      },
      autoplay: {
        enumerable: true,
        get: function () { return ctx._autoplay; },
        set: function (v) { ctx._autoplay = !!v; apply('autoplay', ctx._autoplay ? 1 : 0); }
      },
      loop: {
        enumerable: true,
        get: function () { return ctx._loop; },
        set: function (v) { ctx._loop = !!v; apply('loop', ctx._loop ? 1 : 0); }
      },
      volume: {
        enumerable: true,
        get: function () { return ctx._volume; },
        set: function (v) { ctx._volume = typeof v === 'number' ? v : 1; apply('volume', ctx._volume); }
      },
      startTime: {
        enumerable: true,
        get: function () { return ctx._startTime; },
        set: function (v) { ctx._startTime = Number(v) || 0; }
      },
      duration: { get: function () { return __nx_audio_number(id, 'duration'); } },
      currentTime: {
        get: function () { return __nx_audio_number(id, 'currentTime'); },
        set: function (v) { apply('seek', Number(v) || 0); }
      },
      paused: { get: function () { return ctx._paused; } }
    });
    ctx.play = function () { ctx._paused = false; apply('play', ctx._startTime); };
    ctx.pause = function () { ctx._paused = true; apply('pause', 0); };
    ctx.stop = function () { ctx._paused = true; apply('stop', 0); };
    ctx.seek = function (t) { apply('seek', Number(t) || 0); };
    ctx.destroy = function () { apply('destroy', 0); };
    function listen(name) {
      return function (fn) { try { __nx_audio_on(id, name, fn); } catch (e) {} return ctx; };
    }
    function unlisten(name) {
      return function () { try { __nx_audio_on(id, name, null); } catch (e) {} return ctx; };
    }
    var events = ['play', 'pause', 'stop', 'ended', 'canplay', 'error', 'seeked', 'timeupdate'];
    for (var i = 0; i < events.length; i++) {
      var name = events[i];
      var cap = name.charAt(0).toUpperCase() + name.slice(1);
      ctx['on' + cap] = listen(name);
      ctx['off' + cap] = unlisten(name);
    }
    return ctx;
  };
})(this);
)JS";

double nowMilliseconds() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(
               steady_clock::now().time_since_epoch())
        .count();
}

std::string toStd(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string result = text ? text : "";
    if (text) JS_FreeCString(ctx, text);
    return result;
}

double numberArg(JSContext* ctx, JSValueConst value, double fallback = 0.0) {
    double result = fallback;
    if (!JS_IsUndefined(value)) JS_ToFloat64(ctx, &result, value);
    return result;
}

bool boolArg(JSContext* ctx, JSValueConst value, bool fallback = false) {
    if (JS_IsUndefined(value) || JS_IsNull(value)) return fallback;
    int result = JS_ToBool(ctx, value);
    return result < 0 ? fallback : (result != 0);
}

std::string urlEncode(const std::string& text) {
    std::string out;
    for (char c : text) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", uc);
            out += buffer;
        }
    }
    return out;
}

std::string scalarText(const JsonValue& value) {
    switch (value.type()) {
        case JsonValue::Type::String: return value.asString();
        case JsonValue::Type::Bool: return value.asBool() ? "true" : "false";
        case JsonValue::Type::Number: {
            std::ostringstream stream;
            stream.precision(15);
            stream << value.asNumber();
            return stream.str();
        }
        default: return "";
    }
}

std::string makeQuery(const JsonValue& data) {
    std::string query;
    if (!data.isObject()) return query;
    for (const auto& entry : data.asObject()) {
        const std::string part = urlEncode(entry.first) + "=" + urlEncode(scalarText(entry.second));
        if (!query.empty()) query.push_back('&');
        query += part;
    }
    return query;
}

JSValue makeStringMap(JSContext* ctx, const std::map<std::string, std::string>& values) {
    JSValue object = JS_NewObject(ctx);
    for (const auto& entry : values) {
        JS_SetPropertyStr(ctx, object, entry.first.c_str(),
                          JS_NewString(ctx, entry.second.c_str()));
    }
    return object;
}

/// 微信网络类型：wifi / ethernet / none。Switch 上以太网优先于 Wi-Fi。
std::string detectNetworkType() {
#ifdef __SWITCH__
    auto* platform = brls::Application::getPlatform();
    if (!platform) return "wifi";
    if (platform->hasEthernetConnection()) return "ethernet";
    if (platform->hasWirelessConnection()) return "wifi";
    return "none";
#else
    return "wifi";
#endif
}

}  // namespace

struct JsRuntime::WsClient {
    struct Event {
        std::string type;
        std::string data;
        std::string error;
    };

    mg_mgr manager{};
    mg_connection* connection = nullptr;
    std::thread worker;
    std::mutex mutex;
    std::deque<Event> events;
    std::deque<std::string> outgoing;
    std::atomic<bool> running{true};

    static void onEvent(mg_connection*, int event, void* eventData, void* fnData) {
        auto* self = static_cast<WsClient*>(fnData);
        if (!self) return;
        if (event == MG_EV_WS_OPEN) {
            self->push({"open", "", ""});
        } else if (event == MG_EV_WS_MSG) {
            auto* message = static_cast<mg_ws_message*>(eventData);
            self->push({"message", std::string(message->data.ptr, message->data.len), ""});
        } else if (event == MG_EV_ERROR) {
            self->push({"error", "", static_cast<const char*>(eventData)});
        } else if (event == MG_EV_CLOSE) {
            self->push({"close", "", ""});
            self->running = false;
        }
    }

    void push(Event event) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(std::move(event));
    }

    void start(const std::string& url) {
        worker = std::thread([this, url]() {
            mg_mgr_init(&manager);
            connection = mg_ws_connect(&manager, url.c_str(), onEvent, this, "%s", "");
            if (!connection) push({"error", "", "连接失败"});
            while (running) {
                if (connection) {
                    std::deque<std::string> pending;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        pending.swap(outgoing);
                    }
                    for (const auto& message : pending) {
                        mg_ws_send(connection, message.data(), message.size(), WEBSOCKET_OP_TEXT);
                    }
                }
                mg_mgr_poll(&manager, 50);
            }
            if (connection) connection = nullptr;
            mg_mgr_free(&manager);
        });
    }

    void send(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex);
        outgoing.push_back(data);
    }

    void stop() {
        running = false;
        if (worker.joinable()) worker.join();
    }

    ~WsClient() { stop(); }
};

struct RuntimeBridge {
    static JsRuntime* self(JSContext* ctx) {
        return static_cast<JsRuntime*>(JS_GetContextOpaque(ctx));
    }

    static void logException(JsRuntime* runtime, const std::string& where) {
        if (!runtime || !runtime->context) return;
        const std::string text = runtime->exceptionText();
        brls::Logger::error("miniapp js [{}]: {}", where, text);
    }

    static void invoke(JsRuntime* runtime, JSValue fn, JSValue thisValue, int argc,
                       JSValue* argv) {
        if (!runtime || !runtime->context) return;
        if (!JS_IsFunction(runtime->context, fn)) return;
        JSValue result = JS_Call(runtime->context, fn, thisValue, argc, argv);
        if (JS_IsException(result)) logException(runtime, "callback");
        JS_FreeValue(runtime->context, result);
        runtime->drainJobs();
    }

    // ---------------- 全局对象 ----------------

    static JSValue jsPage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0) return JS_UNDEFINED;
        if (runtime->pageRegistered) JS_FreeValue(ctx, runtime->page);
        runtime->page = JS_DupValue(ctx, argv[0]);
        runtime->pageRegistered = true;
        return JS_UNDEFINED;
    }

    static JSValue jsApp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0) return JS_UNDEFINED;
        if (runtime->appRegistered) JS_FreeValue(ctx, runtime->app);
        runtime->app = JS_DupValue(ctx, argv[0]);
        runtime->appRegistered = true;
        return JS_UNDEFINED;
    }

    static JSValue jsComponent(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        // 自定义组件暂不在渲染管线中实例化，保留接口避免脚本报错。
        return JS_UNDEFINED;
    }

    static JSValue jsGetApp(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->appRegistered) return JS_DupValue(ctx, runtime->app);
        return JS_NewObject(ctx);
    }

    static JSValue jsGetCurrentPages(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return JS_NewArray(ctx);
    }

    static JSValue jsSetData(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->pageRegistered) return JS_UNDEFINED;

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue merge = JS_GetPropertyStr(ctx, global, "__nx_merge");
        JSValue pageData = JS_GetPropertyStr(ctx, runtime->page, "data");
        JSValue args[2] = {pageData, argc > 0 ? argv[0] : JS_UNDEFINED};
        JSValue merged = JS_Call(ctx, merge, JS_UNDEFINED, 2, args);
        if (JS_IsException(merged)) logException(runtime, "setData");
        JS_FreeValue(ctx, merged);
        JS_FreeValue(ctx, pageData);
        JS_FreeValue(ctx, merge);
        JS_FreeValue(ctx, global);

        runtime->refreshData();

        if (argc > 1 && JS_IsFunction(ctx, argv[1])) {
            invoke(runtime, argv[1], runtime->page, 0, nullptr);
        }
        if (runtime->host) runtime->host->onDataChanged();
        return JS_UNDEFINED;
    }

    static JSValue jsConsole(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        std::string text;
        for (int i = 0; i < argc; ++i) {
            if (i) text += " ";
            text += toStd(ctx, argv[i]);
        }
        brls::Logger::info("miniapp console: {}", text);
        return JS_UNDEFINED;
    }

    // ---------------- 定时器 ----------------

    static JSValue jsSetTimeout(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
        const double delay = argc > 1 ? numberArg(ctx, argv[1]) : 0.0;
        return JS_NewInt32(ctx, runtime->addTimer(argv[0], delay, false, nowMilliseconds()));
    }

    static JSValue jsSetInterval(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
        const double delay = argc > 1 ? numberArg(ctx, argv[1]) : 0.0;
        return JS_NewInt32(ctx, runtime->addTimer(argv[0], delay, true, nowMilliseconds()));
    }

    static JSValue jsClearTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && argc > 0) {
            int id = 0;
            JS_ToInt32(ctx, &id, argv[0]);
            runtime->removeTimer(id);
        }
        return JS_UNDEFINED;
    }

    // ---------------- wx ----------------

    static JSValue wxShowToast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host && argc > 0 && JS_IsObject(argv[0])) {
            JSValue title = JS_GetPropertyStr(ctx, argv[0], "title");
            JSValue icon = JS_GetPropertyStr(ctx, argv[0], "icon");
            JSValue duration = JS_GetPropertyStr(ctx, argv[0], "duration");
            runtime->host->showToast(toStd(ctx, title), toStd(ctx, icon),
                                     static_cast<int>(numberArg(ctx, duration, 1500)));
            JS_FreeValue(ctx, title);
            JS_FreeValue(ctx, icon);
            JS_FreeValue(ctx, duration);
        }
        return JS_UNDEFINED;
    }

    static JSValue wxShowLoading(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host) {
            std::string title = "加载中";
            if (argc > 0 && JS_IsObject(argv[0])) {
                JSValue value = JS_GetPropertyStr(ctx, argv[0], "title");
                title = toStd(ctx, value);
                JS_FreeValue(ctx, value);
            }
            runtime->host->showLoading(title);
        }
        return JS_UNDEFINED;
    }

    static JSValue wxHideLoading(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host) runtime->host->hideLoading();
        return JS_UNDEFINED;
    }

    static JSValue wxShowModal(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) {
            return JS_UNDEFINED;
        }
        JSValue title = JS_GetPropertyStr(ctx, argv[0], "title");
        JSValue content = JS_GetPropertyStr(ctx, argv[0], "content");
        JSValue showCancel = JS_GetPropertyStr(ctx, argv[0], "showCancel");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue completeDup = JS_IsFunction(ctx, complete) ? JS_DupValue(ctx, complete)
                                                           : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();
        runtime->host->showModal(
            toStd(ctx, title), toStd(ctx, content),
            boolArg(ctx, showCancel, true),
            [runtime, weak, rtWeak, successDup, completeDup](bool confirm) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, completeDup, confirm]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    if (JS_IsFunction(ctx, successDup)) {
                        JSValue result = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, result, "confirm",
                                          JS_NewBool(ctx, confirm));
                        JS_SetPropertyStr(ctx, result, "cancel",
                                          JS_NewBool(ctx, !confirm));
                        invoke(runtime, successDup, runtime->page, 1, &result);
                        JS_FreeValue(ctx, result);
                    }
                    invoke(runtime, completeDup, runtime->page, 0, nullptr);
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, completeDup);
                });
            });
        JS_FreeValue(ctx, title);
        JS_FreeValue(ctx, content);
        JS_FreeValue(ctx, showCancel);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxSetNavigationBarTitle(JSContext* ctx, JSValueConst, int argc,
                                           JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host && argc > 0 && JS_IsObject(argv[0])) {
            JSValue title = JS_GetPropertyStr(ctx, argv[0], "title");
            runtime->host->setNavigationBarTitle(toStd(ctx, title));
            JS_FreeValue(ctx, title);
        }
        return JS_UNDEFINED;
    }

    static JSValue wxGetSystemInfoSync(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host) return JS_NewObject(ctx);
        return runtime->valueFromJson(runtime->host->systemInfo());
    }

    static JSValue wxGetSystemInfo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        if (runtime->host) {
            JSValue info = runtime->valueFromJson(runtime->host->systemInfo());
            invoke(runtime, success, runtime->page, 1, &info);
            invoke(runtime, complete, runtime->page, 1, &info);
            JS_FreeValue(ctx, info);
        }
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxGetNetworkType(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        const std::string type = detectNetworkType();
        runtime->lastNetworkType = type;
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "networkType", JS_NewString(ctx, type.c_str()));
        invoke(runtime, success, runtime->page, 1, &result);
        invoke(runtime, complete, runtime->page, 1, &result);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxOnNetworkStatusChange(JSContext* ctx, JSValueConst, int argc,
                                           JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
        runtime->networkCallbacks.push_back(JS_DupValue(ctx, argv[0]));
        // 记录基线，只有之后的状态变化才会派发（与微信一致）。
        if (runtime->lastNetworkType.empty()) runtime->lastNetworkType = detectNetworkType();
        return JS_UNDEFINED;
    }

    static JSValue wxOffNetworkStatusChange(JSContext* ctx, JSValueConst, int argc,
                                            JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime) return JS_UNDEFINED;
        if (argc <= 0 || !JS_IsFunction(ctx, argv[0])) {
            for (JSValue fn : runtime->networkCallbacks) JS_FreeValue(ctx, fn);
            runtime->networkCallbacks.clear();
            return JS_UNDEFINED;
        }
        for (auto it = runtime->networkCallbacks.begin();
             it != runtime->networkCallbacks.end(); ++it) {
            if (JS_VALUE_GET_PTR(*it) == JS_VALUE_GET_PTR(argv[0])) {
                JS_FreeValue(ctx, *it);
                runtime->networkCallbacks.erase(it);
                break;
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue wxVibrateShort(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host) runtime->host->vibrateShort();
        return JS_UNDEFINED;
    }

    static JSValue wxCanIUse(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return JS_NewBool(ctx, true);
    }

    static JSValue wxNextTick(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && argc > 0 && JS_IsFunction(ctx, argv[0])) {
            runtime->addTimer(argv[0], 0.0, false, nowMilliseconds());
        }
        return JS_UNDEFINED;
    }

    // ---- 存储 ----

    static JSValue wxSetStorageSync(JSContext* ctx, JSValueConst, int argc,
                                    JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc < 2) return JS_UNDEFINED;
        const std::string key = toStd(ctx, argv[0]);
        bool ok = false;
        std::string text = runtime->serializeJs(argv[1], &ok);
        if (!ok) text = "null";
        runtime->host->storageSet(key, text);
        return JS_UNDEFINED;
    }

    static JSValue wxGetStorageSync(JSContext* ctx, JSValueConst, int argc,
                                    JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc < 1) return JS_UNDEFINED;
        std::string raw;
        if (!runtime->host->storageGet(toStd(ctx, argv[0]), raw) || raw.empty()) {
            return JS_NewString(ctx, "");
        }
        JSValue parsed = JS_ParseJSON(ctx, raw.c_str(), raw.size(), "<storage>");
        if (JS_IsException(parsed)) {
            JSValue exception = JS_GetException(ctx);
            JS_FreeValue(ctx, exception);
            return JS_NewString(ctx, raw.c_str());
        }
        return parsed;
    }

    static JSValue wxRemoveStorageSync(JSContext* ctx, JSValueConst, int argc,
                                       JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host && argc > 0) {
            runtime->host->storageRemove(toStd(ctx, argv[0]));
        }
        return JS_UNDEFINED;
    }

    static JSValue wxClearStorageSync(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host) runtime->host->storageClear();
        return JS_UNDEFINED;
    }

    static JSValue wxGetStorage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue key = JS_GetPropertyStr(ctx, argv[0], "key");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        std::string raw;
        const bool found = runtime->host &&
                           runtime->host->storageGet(toStd(ctx, key), raw);
        JSValue value = found && !raw.empty() ? JS_ParseJSON(ctx, raw.c_str(), raw.size(),
                                                             "<storage>")
                                              : JS_NewString(ctx, "");
        if (JS_IsException(value)) {
            JSValue exception = JS_GetException(ctx);
            JS_FreeValue(ctx, exception);
            value = JS_NewString(ctx, raw.c_str());
        }
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "data", JS_DupValue(ctx, value));
        invoke(runtime, success, runtime->page, 1, &result);
        invoke(runtime, complete, runtime->page, 1, &result);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, value);
        JS_FreeValue(ctx, key);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxSetStorage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue key = JS_GetPropertyStr(ctx, argv[0], "key");
        JSValue dataValue = JS_GetPropertyStr(ctx, argv[0], "data");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        bool ok = false;
        std::string text = runtime->serializeJs(dataValue, &ok);
        if (ok && runtime->host) runtime->host->storageSet(toStd(ctx, key), text);
        invoke(runtime, success, runtime->page, 0, nullptr);
        invoke(runtime, complete, runtime->page, 0, nullptr);
        JS_FreeValue(ctx, key);
        JS_FreeValue(ctx, dataValue);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxRemoveStorage(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && argc > 0 && JS_IsObject(argv[0]) && runtime->host) {
            JSValue key = JS_GetPropertyStr(ctx, argv[0], "key");
            runtime->host->storageRemove(toStd(ctx, key));
            JS_FreeValue(ctx, key);
        }
        return JS_UNDEFINED;
    }

    static JSValue wxClearStorage(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host) runtime->host->storageClear();
        return JS_UNDEFINED;
    }

    static JSValue buildStorageInfo(JSContext* ctx, JsRuntime* runtime) {
        JSValue info = JS_NewObject(ctx);
        JSValue keys = JS_NewArray(ctx);
        double size = 0.0;
        if (runtime && runtime->host) {
            auto names = runtime->host->storageKeys();
            for (size_t i = 0; i < names.size(); ++i) {
                JS_SetPropertyUint32(ctx, keys, static_cast<uint32_t>(i),
                                     JS_NewString(ctx, names[i].c_str()));
                std::string value;
                if (runtime->host->storageGet(names[i], value)) {
                    size += static_cast<double>(value.size());
                }
            }
        }
        JS_SetPropertyStr(ctx, info, "keys", keys);
        JS_SetPropertyStr(ctx, info, "currentSize", JS_NewFloat64(ctx, size));
        JS_SetPropertyStr(ctx, info, "limitSize", JS_NewFloat64(ctx, 10.0 * 1024.0 * 1024.0));
        return info;
    }

    static JSValue wxGetStorageInfoSync(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return buildStorageInfo(ctx, self(ctx));
    }

    static JSValue wxGetStorageInfo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        JSValue info = buildStorageInfo(ctx, runtime);
        invoke(runtime, success, runtime->page, 1, &info);
        invoke(runtime, complete, runtime->page, 1, &info);
        JS_FreeValue(ctx, info);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxSetClipboardData(JSContext* ctx, JSValueConst, int argc,
                                      JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue data = JS_GetPropertyStr(ctx, argv[0], "data");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        std::string text = toStd(ctx, data);
        if (runtime->host) runtime->host->clipboardSet(text);
        invoke(runtime, success, runtime->page, 0, nullptr);
        invoke(runtime, complete, runtime->page, 0, nullptr);
        JS_FreeValue(ctx, data);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxGetClipboardData(JSContext* ctx, JSValueConst, int argc,
                                      JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        std::string text = runtime->host ? runtime->host->clipboardGet() : std::string();
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "data", JS_NewString(ctx, text.c_str()));
        invoke(runtime, success, runtime->page, 1, &result);
        invoke(runtime, complete, runtime->page, 1, &result);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxGetLaunchOptionsSync(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        JSValue options = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, options, "path",
                          JS_NewString(ctx, runtime ? runtime->pagePath.c_str() : ""));
        JS_SetPropertyStr(ctx, options, "query", JS_NewObject(ctx));
        JS_SetPropertyStr(ctx, options, "scene", JS_NewInt32(ctx, 1001));
        return options;
    }

    static JSValue wxGetMenuButtonBoundingClientRect(JSContext* ctx, JSValueConst, int,
                                                     JSValueConst*) {
        JSValue rect = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, rect, "width", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, rect, "height", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, rect, "top", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, rect, "right", JS_NewFloat64(ctx, 1280.0));
        JS_SetPropertyStr(ctx, rect, "bottom", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, rect, "left", JS_NewFloat64(ctx, 1280.0));
        return rect;
    }

    /// 把 systemInfo 拆分成微信新版的 windowInfo / deviceInfo / appBaseInfo。
    static JSValue systemInfoSubset(JSContext* ctx, JsRuntime* runtime, const char* kind) {
        JSValue info = JS_NewObject(ctx);
        if (!runtime || !runtime->host) return info;
        JsonValue source = runtime->host->systemInfo();
        auto copy = [&](const char* key, const char* alias = nullptr) {
            const JsonValue* value = source.find(key);
            if (!value) return;
            JS_SetPropertyStr(ctx, info, alias ? alias : key, runtime->valueFromJson(*value));
        };
        std::string which = kind ? kind : "";
        if (which == "window") {
            copy("pixelRatio");
            copy("screenWidth");
            copy("screenHeight");
            copy("windowWidth");
            copy("windowHeight");
            copy("statusBarHeight");
            copy("safeArea");
        } else if (which == "device") {
            copy("brand");
            copy("model");
            copy("system");
            copy("platform");
            copy("memorySize");
            copy("abi");
        } else {
            copy("language");
            copy("version");
            copy("theme");
            copy("SDKVersion");
            copy("platform");
        }
        return info;
    }

    static JSValue wxGetWindowInfo(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return systemInfoSubset(ctx, self(ctx), "window");
    }

    static JSValue wxGetDeviceInfo(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return systemInfoSubset(ctx, self(ctx), "device");
    }

    static JSValue wxGetAppBaseInfo(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        return systemInfoSubset(ctx, self(ctx), "app");
    }

    static JSValue wxGetUpdateManager(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JSValue manager = JS_NewObject(ctx);
        JSValue noop = JS_NewCFunction(ctx, [](JSContext*, JSValueConst, int, JSValueConst*) {
            return JS_UNDEFINED;
        }, "noop", 1);
        JS_SetPropertyStr(ctx, manager, "onCheckForUpdate", JS_DupValue(ctx, noop));
        JS_SetPropertyStr(ctx, manager, "onUpdateReady", JS_DupValue(ctx, noop));
        JS_SetPropertyStr(ctx, manager, "onUpdateFailed", JS_DupValue(ctx, noop));
        JS_SetPropertyStr(ctx, manager, "applyUpdate", JS_DupValue(ctx, noop));
        JS_FreeValue(ctx, noop);
        return manager;
    }

    /// 宿主暂不支持、但脚本常调用的接口：提供空实现避免中断。
    static JSValue wxNoop(JSContext*, JSValueConst, int, JSValueConst*) {
        return JS_UNDEFINED;
    }

    // ---- video ----

    static JSValue wxVideoControlData(JSContext* ctx, JSValueConst, int argc,
                                      JSValueConst* argv, int magic, JSValue* data) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host) return JS_UNDEFINED;
        const std::string id = toStd(ctx, data[0]);
        const std::string action = toStd(ctx, data[1]);
        double value = (argc > 0) ? numberArg(ctx, argv[0], 0.0) : 0.0;
        std::string text;
        if (argc > 0 && JS_IsString(argv[0])) text = toStd(ctx, argv[0]);
        runtime->host->videoControl(id, action, value, text);
        (void)magic;
        return JS_UNDEFINED;
    }

    static JSValue wxCreateVideoContext(JSContext* ctx, JSValueConst, int argc,
                                        JSValueConst* argv) {
        JSValue context = JS_NewObject(ctx);
        JSValue id = JS_NewString(ctx, argc > 0 ? toStd(ctx, argv[0]).c_str() : "");
        JS_SetPropertyStr(ctx, context, "id", JS_DupValue(ctx, id));

        struct Entry {
            const char* name;
            const char* action;
            int length;
        };
        static const Entry entries[] = {
            {"play", "play", 0},
            {"pause", "pause", 0},
            {"stop", "stop", 0},
            {"seek", "seek", 1},
            {"requestFullScreen", "requestFullScreen", 0},
            {"exitFullScreen", "exitFullScreen", 0},
            {"setPlaybackRate", "setPlaybackRate", 1},
            {"setVolume", "setVolume", 1},
            {"addSubtitle", "addSubtitle", 1},
        };
        for (const auto& entry : entries) {
            JSValue data[2] = {id, JS_NewString(ctx, entry.action)};
            JSValue fn = JS_NewCFunctionData(ctx, wxVideoControlData, entry.length, 0, 2, data);
            JS_FreeValue(ctx, data[1]);
            JS_SetPropertyStr(ctx, context, entry.name, fn);
        }
        JS_FreeValue(ctx, id);
        return context;
    }

    // ---- 网络 ----

    static JSValue wxRequest(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) {
            return JS_UNDEFINED;
        }

        JSValue urlValue = JS_GetPropertyStr(ctx, argv[0], "url");
        JSValue methodValue = JS_GetPropertyStr(ctx, argv[0], "method");
        JSValue dataValue = JS_GetPropertyStr(ctx, argv[0], "data");
        JSValue headerValue = JS_GetPropertyStr(ctx, argv[0], "header");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue fail = JS_GetPropertyStr(ctx, argv[0], "fail");
        JSValue complete = JS_GetPropertyStr(ctx, argv[0], "complete");
        JSValue timeoutValue = JS_GetPropertyStr(ctx, argv[0], "timeout");
        JSValue responseTypeValue = JS_GetPropertyStr(ctx, argv[0], "responseType");

        std::string method =
            (JS_IsUndefined(methodValue) || JS_IsNull(methodValue)) ? std::string() : toStd(ctx, methodValue);
        if (method.empty()) method = "GET";
        for (char& c : method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string url = toStd(ctx, urlValue);
        std::string body;
        std::map<std::string, std::string> headers;
        // 微信默认 60s；0/未设置走宿主的默认值。
        int timeoutMs = static_cast<int>(numberArg(ctx, timeoutValue, 60000.0));
        std::string responseType = toStd(ctx, responseTypeValue);

        bool ok = false;
        if (!JS_IsUndefined(dataValue) && !JS_IsNull(dataValue)) {
            if (method == "GET" || method == "HEAD") {
                std::string text = runtime->serializeJs(dataValue, &ok);
                if (ok) {
                    JsonValue dataJson = parseJson(text);
                    std::string query = makeQuery(dataJson);
                    if (!query.empty()) {
                        url += (url.find('?') == std::string::npos ? "?" : "&") + query;
                    }
                }
            } else if (JS_IsString(dataValue)) {
                // data 直接给字符串时原样发送，不做 JSON 引号包裹。
                body = toStd(ctx, dataValue);
            } else {
                std::string text = runtime->serializeJs(dataValue, &ok);
                if (ok) body = text;
            }
        }

        if (!JS_IsUndefined(headerValue) && !JS_IsNull(headerValue)) {
            std::string headerText = runtime->serializeJs(headerValue, &ok);
            if (ok) {
                JsonValue headerJson = parseJson(headerText);
                for (const auto& entry : headerJson.asObject()) {
                    headers[entry.first] = scalarText(entry.second);
                }
            }
        }
        if (!body.empty() && headers.find("Content-Type") == headers.end() &&
            headers.find("content-type") == headers.end()) {
            headers["Content-Type"] = "application/json";
        }

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue failDup = JS_IsFunction(ctx, fail) ? JS_DupValue(ctx, fail) : JS_UNDEFINED;
        JSValue completeDup = JS_IsFunction(ctx, complete) ? JS_DupValue(ctx, complete)
                                                           : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();

        runtime->host->httpRequest(
            method, url, headers, body, timeoutMs,
            [runtime, weak, rtWeak, successDup, failDup, completeDup, responseType](
                bool transportOk, int status, const std::string& responseBody,
                const std::map<std::string, std::string>& responseHeaders,
                const std::map<std::string, std::string>& responseCookies,
                const std::string& responseError) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, failDup, completeDup, transportOk,
                            status, responseBody, responseHeaders, responseCookies,
                            responseError, responseType]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    JSValue result = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, result, "statusCode", JS_NewInt32(ctx, status));
                    JSValue responseData = JS_UNDEFINED;
                    if (responseType == "arraybuffer") {
                        responseData = JS_NewArrayBufferCopy(
                            ctx, reinterpret_cast<const uint8_t*>(responseBody.data()),
                            responseBody.size());
                    } else {
                        responseData = JS_NewString(ctx, responseBody.c_str());
                        if (!responseBody.empty()) {
                            JSValue parsed = JS_ParseJSON(ctx, responseBody.c_str(),
                                                          responseBody.size(), "<response>");
                            if (JS_IsException(parsed)) {
                                JSValue exception = JS_GetException(ctx);
                                JS_FreeValue(ctx, exception);
                            } else {
                                JS_FreeValue(ctx, responseData);
                                responseData = parsed;
                            }
                        }
                    }
                    JS_SetPropertyStr(ctx, result, "data", responseData);
                    JS_SetPropertyStr(ctx, result, "header",
                                      makeStringMap(ctx, responseHeaders));
                    JS_SetPropertyStr(ctx, result, "cookies",
                                      makeStringMap(ctx, responseCookies));
                    // 微信语义：只要拿到了 HTTP 响应（含 4xx/5xx）就走 success，
                    // 脚本按 statusCode 自行判断；fail 只在网络层失败时触发。
                    if (transportOk) {
                        invoke(runtime, successDup, runtime->page, 1, &result);
                    } else {
                        std::string errMsg = "request:fail";
                        if (!responseError.empty()) errMsg += " " + responseError;
                        JSValue failed = JS_NewObject(ctx);
                        // fail 也带上 statusCode/data/header，便于脚本做兜底提示。
                        JSValue failData = JS_GetPropertyStr(ctx, result, "data");
                        JSValue failHeader = JS_GetPropertyStr(ctx, result, "header");
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, errMsg.c_str()));
                        JS_SetPropertyStr(ctx, failed, "statusCode", JS_NewInt32(ctx, status));
                        JS_SetPropertyStr(ctx, failed, "data", failData);
                        JS_SetPropertyStr(ctx, failed, "header", failHeader);
                        invoke(runtime, failDup, runtime->page, 1, &failed);
                        JS_FreeValue(ctx, failed);
                    }
                    invoke(runtime, completeDup, runtime->page, 1, &result);
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, failDup);
                    JS_FreeValue(ctx, completeDup);
                });
            });

        JS_FreeValue(ctx, urlValue);
        JS_FreeValue(ctx, methodValue);
        JS_FreeValue(ctx, dataValue);
        JS_FreeValue(ctx, headerValue);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        JS_FreeValue(ctx, complete);
        JS_FreeValue(ctx, timeoutValue);
        JS_FreeValue(ctx, responseTypeValue);
        return JS_UNDEFINED;
    }

    // ---- 导航 ----

    static JSValue navigate(JSContext* ctx, JSValueConst, JSValueConst options,
                            const char* kind) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || !JS_IsObject(options)) return JS_UNDEFINED;
        JSValue urlValue = JS_GetPropertyStr(ctx, options, "url");
        JSValue deltaValue = JS_GetPropertyStr(ctx, options, "delta");
        JSValue success = JS_GetPropertyStr(ctx, options, "success");
        JSValue fail = JS_GetPropertyStr(ctx, options, "fail");
        JSValue complete = JS_GetPropertyStr(ctx, options, "complete");

        std::string url = toStd(ctx, urlValue);
        int delta = static_cast<int>(numberArg(ctx, deltaValue, 1));
        bool ok = false;
        if (std::string(kind) == "back") {
            ok = runtime->host->navigateBack(delta);
        } else if (std::string(kind) == "redirect") {
            ok = runtime->host->redirectTo(url);
        } else if (std::string(kind) == "relaunch") {
            ok = runtime->host->reLaunch(url);
        } else {
            ok = runtime->host->navigateTo(url);
        }
        if (ok) {
            invoke(runtime, success, runtime->page, 0, nullptr);
        } else {
            JSValue failed = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, failed, "errMsg",
                              JS_NewString(ctx, "navigate:fail"));
            invoke(runtime, fail, runtime->page, 1, &failed);
            JS_FreeValue(ctx, failed);
        }
        invoke(runtime, complete, runtime->page, 0, nullptr);
        JS_FreeValue(ctx, urlValue);
        JS_FreeValue(ctx, deltaValue);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        JS_FreeValue(ctx, complete);
        return JS_UNDEFINED;
    }

    static JSValue wxNavigateTo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        return navigate(ctx, JS_UNDEFINED, argc > 0 ? argv[0] : JS_UNDEFINED, "to");
    }

    static JSValue wxNavigateBack(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv) {
        return navigate(ctx, JS_UNDEFINED, argc > 0 ? argv[0] : JS_UNDEFINED, "back");
    }

    static JSValue wxRedirectTo(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
        return navigate(ctx, JS_UNDEFINED, argc > 0 ? argv[0] : JS_UNDEFINED, "redirect");
    }

    static JSValue wxReLaunch(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        return navigate(ctx, JS_UNDEFINED, argc > 0 ? argv[0] : JS_UNDEFINED, "relaunch");
    }

    // ---- 小程序互跳与最近使用 ----

    static JSValue wxMiniAppLaunch(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0) return JS_UNDEFINED;

        std::string id;
        std::string title;
        if (JS_IsString(argv[0])) {
            id = toStd(ctx, argv[0]);
            if (argc > 1 && JS_IsString(argv[1])) title = toStd(ctx, argv[1]);
        } else if (JS_IsObject(argv[0])) {
            id = toStd(ctx, JS_GetPropertyStr(ctx, argv[0], "id"));
            title = toStd(ctx, JS_GetPropertyStr(ctx, argv[0], "title"));
        }
        if (id.empty()) return JS_UNDEFINED;

        bool ok = runtime->host->launchMiniApp(id, title);
        return JS_NewBool(ctx, ok);
    }

    static JSValue wxMiniAppListRecent(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host) return JS_NewArray(ctx);
        return runtime->valueFromJson(runtime->host->recentMiniApps());
    }

    static JSValue wxMiniAppDeviceId(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host) return JS_NewString(ctx, "");
        return JS_NewString(ctx, runtime->host->deviceId().c_str());
    }

    // ---- SMB 数据源 ----

    // 原生调用里任何 C++ 异常都不能穿过 QuickJS 的 C 栈帧：一旦逃逸到 C 边界，
    // libstdc++ 会走 __terminate 直接 abort 整个容器（真机上表现为播放时整机退出）。
    // 统一用这个守卫包一层，把异常转成 JS 异常交还给小程序。
    template <JSValue (*Impl)(JSContext*, JSValueConst, int, JSValueConst*)>
    static JSValue guardNative(JSContext* ctx, JSValueConst thisVal, int argc,
                               JSValueConst* argv) {
        try {
            return Impl(ctx, thisVal, argc, argv);
        } catch (const std::exception& error) {
            return JS_ThrowInternalError(ctx, "native exception: %s", error.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "native exception");
        }
    }

    static JSValue wxSmbConnect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;

        JSValue serverV = JS_GetPropertyStr(ctx, argv[0], "server");
        JSValue shareV = JS_GetPropertyStr(ctx, argv[0], "share");
        JSValue userV = JS_GetPropertyStr(ctx, argv[0], "user");
        JSValue passwordV = JS_GetPropertyStr(ctx, argv[0], "password");
        JSValue domainV = JS_GetPropertyStr(ctx, argv[0], "domain");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue fail = JS_GetPropertyStr(ctx, argv[0], "fail");

        std::string server = toStd(ctx, serverV);
        std::string share = toStd(ctx, shareV);
        std::string user = toStd(ctx, userV);
        std::string password = toStd(ctx, passwordV);
        std::string domain = toStd(ctx, domainV);

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue failDup = JS_IsFunction(ctx, fail) ? JS_DupValue(ctx, fail) : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();

        runtime->host->smbConnect(server, share, user, password, domain,
            [runtime, weak, rtWeak, successDup, failDup](bool ok, const std::string& result,
                                                 const std::string& error) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, failDup, ok, result, error]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    if (ok) {
                        JSValue res = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, res, "id", JS_NewString(ctx, result.c_str()));
                        invoke(runtime, successDup, runtime->page, 1, &res);
                        JS_FreeValue(ctx, res);
                    } else {
                        JSValue failed = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, ("smb.connect:fail " + error).c_str()));
                        invoke(runtime, failDup, runtime->page, 1, &failed);
                        JS_FreeValue(ctx, failed);
                    }
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, failDup);
                });
            });

        JS_FreeValue(ctx, serverV);
        JS_FreeValue(ctx, shareV);
        JS_FreeValue(ctx, userV);
        JS_FreeValue(ctx, passwordV);
        JS_FreeValue(ctx, domainV);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        return JS_UNDEFINED;
    }

    static JSValue wxSmbListFiles(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;

        JSValue idV = JS_GetPropertyStr(ctx, argv[0], "id");
        JSValue pathV = JS_GetPropertyStr(ctx, argv[0], "path");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue fail = JS_GetPropertyStr(ctx, argv[0], "fail");

        std::string id = toStd(ctx, idV);
        std::string path = toStd(ctx, pathV);

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue failDup = JS_IsFunction(ctx, fail) ? JS_DupValue(ctx, fail) : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();

        runtime->host->smbListFiles(id, path,
            [runtime, weak, rtWeak, successDup, failDup](bool ok, const std::string& result,
                                                 const std::string& error) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, failDup, ok, result, error]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    if (ok) {
                        JSValue parsed = JS_ParseJSON(ctx, result.c_str(), result.size(),
                                                     "<smb-list>");
                        if (JS_IsException(parsed)) {
                            JSValue exc = JS_GetException(ctx);
                            JS_FreeValue(ctx, exc);
                            parsed = JS_NewArray(ctx);
                        }
                        invoke(runtime, successDup, runtime->page, 1, &parsed);
                        JS_FreeValue(ctx, parsed);
                    } else {
                        JSValue failed = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, ("smb.listFiles:fail " + error).c_str()));
                        invoke(runtime, failDup, runtime->page, 1, &failed);
                        JS_FreeValue(ctx, failed);
                    }
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, failDup);
                });
            });

        JS_FreeValue(ctx, idV);
        JS_FreeValue(ctx, pathV);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        return JS_UNDEFINED;
    }

    static JSValue wxSmbScan(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;

        JSValue idV = JS_GetPropertyStr(ctx, argv[0], "id");
        JSValue pathV = JS_GetPropertyStr(ctx, argv[0], "path");
        JSValue depthV = JS_GetPropertyStr(ctx, argv[0], "maxDepth");
        JSValue entriesV = JS_GetPropertyStr(ctx, argv[0], "maxEntries");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue fail = JS_GetPropertyStr(ctx, argv[0], "fail");
        JSValue progress = JS_GetPropertyStr(ctx, argv[0], "progress");

        std::string id = toStd(ctx, idV);
        std::string path = toStd(ctx, pathV);
        int32_t maxDepth = 4;
        int32_t maxEntries = 200;
        JS_ToInt32(ctx, &maxDepth, depthV);
        JS_ToInt32(ctx, &maxEntries, entriesV);

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue failDup = JS_IsFunction(ctx, fail) ? JS_DupValue(ctx, fail) : JS_UNDEFINED;
        JSValue progressDup = JS_IsFunction(ctx, progress) ? JS_DupValue(ctx, progress)
                                                           : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();

        runtime->host->smbScan(id, path, maxDepth, maxEntries,
            [runtime, weak, rtWeak, successDup, failDup,
             progressDup](bool ok, const std::string& result,
                         const std::string& error) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, failDup, progressDup, ok, result,
                            error]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    if (ok) {
                        JSValue parsed = JS_ParseJSON(ctx, result.c_str(), result.size(),
                                                     "<smb-scan>");
                        if (JS_IsException(parsed)) {
                            JSValue exc = JS_GetException(ctx);
                            JS_FreeValue(ctx, exc);
                            parsed = JS_NewArray(ctx);
                        }
                        invoke(runtime, successDup, runtime->page, 1, &parsed);
                        JS_FreeValue(ctx, parsed);
                    } else {
                        JSValue failed = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, ("smb.scan:fail " + error).c_str()));
                        invoke(runtime, failDup, runtime->page, 1, &failed);
                        JS_FreeValue(ctx, failed);
                    }
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, failDup);
                    JS_FreeValue(ctx, progressDup);
                });
            },
            [runtime, weak, rtWeak, progressDup](int dirs, int files) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, progressDup, dirs, files]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    JSValue detail = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, detail, "dirs", JS_NewInt32(ctx, dirs));
                    JS_SetPropertyStr(ctx, detail, "files", JS_NewInt32(ctx, files));
                    invoke(runtime, progressDup, runtime->page, 1, &detail);
                    JS_FreeValue(ctx, detail);
                });
            });

        JS_FreeValue(ctx, idV);
        JS_FreeValue(ctx, pathV);
        JS_FreeValue(ctx, depthV);
        JS_FreeValue(ctx, entriesV);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        JS_FreeValue(ctx, progress);
        return JS_UNDEFINED;
    }

    static JSValue wxSmbCancelScan(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (runtime && runtime->host) runtime->host->smbCancelScan();
        return JS_UNDEFINED;
    }

    static JSValue wxMediaScan(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;

        JSValue sourceIdV = JS_GetPropertyStr(ctx, argv[0], "sourceId");
        JSValue smbIdV = JS_GetPropertyStr(ctx, argv[0], "id");
        JSValue rootV = JS_GetPropertyStr(ctx, argv[0], "root");
        JSValue depthV = JS_GetPropertyStr(ctx, argv[0], "maxDepth");
        JSValue filesV = JS_GetPropertyStr(ctx, argv[0], "maxFiles");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue fail = JS_GetPropertyStr(ctx, argv[0], "fail");

        std::string sourceId = toStd(ctx, sourceIdV);
        std::string smbId = toStd(ctx, smbIdV);
        std::string root = toStd(ctx, rootV);
        int32_t maxDepth = 4;
        int32_t maxFiles = 2000;
        JS_ToInt32(ctx, &maxDepth, depthV);
        JS_ToInt32(ctx, &maxFiles, filesV);

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue failDup = JS_IsFunction(ctx, fail) ? JS_DupValue(ctx, fail) : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();

        runtime->host->mediaScan(sourceId, smbId, root, maxDepth, maxFiles,
            [runtime, weak, rtWeak, successDup, failDup](bool ok, const std::string& result,
                                                 const std::string& error) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, failDup, ok, result, error]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    if (ok) {
                        JSValue parsed = JS_ParseJSON(ctx, result.c_str(), result.size(),
                                                     "<media-scan>");
                        if (JS_IsException(parsed)) {
                            JSValue exc = JS_GetException(ctx);
                            JS_FreeValue(ctx, exc);
                            parsed = JS_NewObject(ctx);
                        }
                        invoke(runtime, successDup, runtime->page, 1, &parsed);
                        JS_FreeValue(ctx, parsed);
                    } else {
                        JSValue failed = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, ("media.scan:fail " + error).c_str()));
                        invoke(runtime, failDup, runtime->page, 1, &failed);
                        JS_FreeValue(ctx, failed);
                    }
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, failDup);
                });
            });

        JS_FreeValue(ctx, sourceIdV);
        JS_FreeValue(ctx, smbIdV);
        JS_FreeValue(ctx, rootV);
        JS_FreeValue(ctx, depthV);
        JS_FreeValue(ctx, filesV);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        return JS_UNDEFINED;
    }

    static JSValue wxMediaLibrary(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;

        JSValue sourceIdV = JS_GetPropertyStr(ctx, argv[0], "sourceId");
        JSValue rootV = JS_GetPropertyStr(ctx, argv[0], "root");
        JSValue offsetV = JS_GetPropertyStr(ctx, argv[0], "offset");
        JSValue limitV = JS_GetPropertyStr(ctx, argv[0], "limit");
        JSValue success = JS_GetPropertyStr(ctx, argv[0], "success");
        JSValue fail = JS_GetPropertyStr(ctx, argv[0], "fail");

        std::string sourceId = toStd(ctx, sourceIdV);
        std::string root = toStd(ctx, rootV);
        int32_t offset = 0;
        int32_t limit = 0;
        JS_ToInt32(ctx, &offset, offsetV);
        JS_ToInt32(ctx, &limit, limitV);

        JSValue successDup = JS_IsFunction(ctx, success) ? JS_DupValue(ctx, success)
                                                         : JS_UNDEFINED;
        JSValue failDup = JS_IsFunction(ctx, fail) ? JS_DupValue(ctx, fail) : JS_UNDEFINED;
        std::weak_ptr<bool> weak = runtime->alive;
        // 回调可能晚于页面销毁才返回，投递前用 weak_ptr 确认 runtime 还在。
        std::weak_ptr<JsRuntime> rtWeak = runtime->weak_from_this();

        runtime->host->mediaLibrary(sourceId, root, offset, limit,
            [runtime, weak, rtWeak, successDup, failDup](bool ok, const std::string& result,
                                                 const std::string& error) {
                auto rt = rtWeak.lock();
                if (!rt) return;
                rt->postToScript([runtime, weak, successDup, failDup, ok, result, error]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    if (ok) {
                        JSValue parsed = JS_ParseJSON(ctx, result.c_str(), result.size(),
                                                     "<media-library>");
                        if (JS_IsException(parsed)) {
                            JSValue exc = JS_GetException(ctx);
                            JS_FreeValue(ctx, exc);
                            parsed = JS_NewObject(ctx);
                        }
                        invoke(runtime, successDup, runtime->page, 1, &parsed);
                        JS_FreeValue(ctx, parsed);
                    } else {
                        JSValue failed = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, ("media.library:fail " + error).c_str()));
                        invoke(runtime, failDup, runtime->page, 1, &failed);
                        JS_FreeValue(ctx, failed);
                    }
                    JS_FreeValue(ctx, successDup);
                    JS_FreeValue(ctx, failDup);
                });
            });

        JS_FreeValue(ctx, sourceIdV);
        JS_FreeValue(ctx, rootV);
        JS_FreeValue(ctx, offsetV);
        JS_FreeValue(ctx, limitV);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, fail);
        return JS_UNDEFINED;
    }

    static JSValue wxSmbDisconnect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_UNDEFINED;
        JSValue idV = JS_GetPropertyStr(ctx, argv[0], "id");
        std::string id = toStd(ctx, idV);
        JS_FreeValue(ctx, idV);
        if (!id.empty()) runtime->host->smbDisconnect(id);
        return JS_UNDEFINED;
    }

    static JSValue wxSmbStreamUrl(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host || argc <= 0 || !JS_IsObject(argv[0])) return JS_NewString(ctx, "");
        JSValue idV = JS_GetPropertyStr(ctx, argv[0], "id");
        JSValue pathV = JS_GetPropertyStr(ctx, argv[0], "path");
        std::string id = toStd(ctx, idV);
        std::string path = toStd(ctx, pathV);
        JS_FreeValue(ctx, idV);
        JS_FreeValue(ctx, pathV);
        std::string url = runtime->host->smbStreamUrl(id, path);
        return JS_NewString(ctx, url.c_str());
    }

    // ---- 动画帧 ----

    static JSValue jsRequestAnimationFrame(JSContext* ctx, JSValueConst, int argc,
                                           JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc <= 0 || !JS_IsFunction(ctx, argv[0])) return JS_NewInt32(ctx, 0);
        return JS_NewInt32(ctx, runtime->addAnimationFrame(argv[0]));
    }

    static JSValue jsCancelAnimationFrame(JSContext* ctx, JSValueConst, int argc,
                                          JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (runtime && argc > 0) {
            int id = 0;
            JS_ToInt32(ctx, &id, argv[0]);
            runtime->cancelAnimationFrame(id);
        }
        return JS_UNDEFINED;
    }

    // ---- 手柄 ----

    static JSValue wxGetGamepadState(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || !runtime->host) return JS_NewObject(ctx);
        return runtime->valueFromJson(runtime->host->gamepadState());
    }

    // ---- canvas 2D ----

    static JSValue wxCanvasOp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                              int magic, JSValue* data) {
        JsRuntime* runtime = self(ctx);
        if (runtime) {
            runtime->canvasOp(toStd(ctx, data[0]), toStd(ctx, data[1]), ctx, argc, argv);
        }
        (void)magic;
        return JS_UNDEFINED;
    }

    static JSValue wxCreateCanvasContext(JSContext* ctx, JSValueConst, int argc,
                                         JSValueConst* argv) {
        JSValue context = JS_NewObject(ctx);
        JSValue id = JS_NewString(ctx, argc > 0 ? toStd(ctx, argv[0]).c_str() : "");
        JS_SetPropertyStr(ctx, context, "id", JS_DupValue(ctx, id));

        struct Entry {
            const char* name;
            const char* kind;
            int length;
        };
        static const Entry entries[] = {
            {"draw", "draw", 0},
            {"setFillStyle", "setFillStyle", 1},
            {"setStrokeStyle", "setStrokeStyle", 1},
            {"setLineWidth", "setLineWidth", 1},
            {"setFontSize", "setFontSize", 1},
            {"setTextAlign", "setTextAlign", 1},
            {"setTextBaseline", "noop", 1},
            {"setGlobalAlpha", "setGlobalAlpha", 1},
            {"setLineCap", "noop", 1},
            {"setLineJoin", "noop", 1},
            {"setMiterLimit", "noop", 1},
            {"setShadow", "noop", 4},
            {"fillRect", "fillRect", 4},
            {"strokeRect", "strokeRect", 4},
            {"clearRect", "clearRect", 4},
            {"beginPath", "beginPath", 0},
            {"closePath", "closePath", 0},
            {"moveTo", "moveTo", 2},
            {"lineTo", "lineTo", 2},
            {"arc", "arc", 5},
            {"rect", "rect", 4},
            {"fill", "fill", 0},
            {"stroke", "stroke", 0},
            {"fillText", "fillText", 3},
            {"drawImage", "drawImage", 5},
            {"save", "save", 0},
            {"restore", "restore", 0},
            {"translate", "translate", 2},
            {"scale", "scale", 2},
            {"rotate", "rotate", 1},
        };
        for (const auto& entry : entries) {
            JSValue data[2] = {id, JS_NewString(ctx, entry.kind)};
            JSValue fn = JS_NewCFunctionData(ctx, wxCanvasOp, entry.length, 0, 2, data);
            JS_FreeValue(ctx, data[1]);
            JS_SetPropertyStr(ctx, context, entry.name, fn);
        }
        JS_FreeValue(ctx, id);
        return context;
    }

    // ---- WebSocket ----

    static JSValue wxSocketOn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                              int, JSValue* data) {
        JsRuntime* runtime = self(ctx);
        int id = 0;
        JS_ToInt32(ctx, &id, data[0]);
        if (runtime && argc > 0 && JS_IsFunction(ctx, argv[0])) {
            runtime->setSocketHandler(id, toStd(ctx, data[1]), argv[0]);
        }
        return JS_UNDEFINED;
    }

    static JSValue wxSocketAction(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                                  int, JSValue* data) {
        JsRuntime* runtime = self(ctx);
        if (!runtime) return JS_UNDEFINED;
        int id = 0;
        JS_ToInt32(ctx, &id, data[0]);
        const std::string action = toStd(ctx, data[1]);
        if (action == "send") {
            std::string payload;
            if (argc > 0) {
                if (JS_IsString(argv[0])) {
                    payload = toStd(ctx, argv[0]);
                } else {
                    JSValue dataValue = JS_GetPropertyStr(ctx, argv[0], "data");
                    payload = toStd(ctx, dataValue);
                    if (!JS_IsString(dataValue)) {
                        bool ok = false;
                        payload = runtime->serializeJs(dataValue, &ok);
                    }
                    JS_FreeValue(ctx, dataValue);
                }
            }
            runtime->sendSocket(id, payload);
        } else if (action == "close") {
            runtime->closeSocket(id);
        }
        return JS_UNDEFINED;
    }

    static JSValue wxConnectSocket(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        JSValue task = JS_NewObject(ctx);
        if (!runtime || argc <= 0 || !JS_IsObject(argv[0])) return task;

        JSValue urlValue = JS_GetPropertyStr(ctx, argv[0], "url");
        const std::string url = toStd(ctx, urlValue);
        JS_FreeValue(ctx, urlValue);
        if (url.empty()) return task;

        const int id = runtime->openSocket(url);
        JSValue idValue = JS_NewInt32(ctx, id);
        JS_SetPropertyStr(ctx, task, "socketId", JS_DupValue(ctx, idValue));

        struct Binding {
            const char* name;
            const char* event;
        };
        static const Binding kEvents[] = {
            {"onOpen", "open"},
            {"onMessage", "message"},
            {"onError", "error"},
            {"onClose", "close"},
        };
        for (const auto& binding : kEvents) {
            JSValue data[2] = {idValue, JS_NewString(ctx, binding.event)};
            JS_SetPropertyStr(ctx, task, binding.name,
                              JS_NewCFunctionData(ctx, wxSocketOn, 1, 0, 2, data));
            JS_FreeValue(ctx, data[1]);
        }
        struct Action {
            const char* name;
            const char* action;
            int length;
        };
        static const Action kActions[] = {
            {"send", "send", 1},
            {"close", "close", 0},
        };
        for (const auto& binding : kActions) {
            JSValue data[2] = {idValue, JS_NewString(ctx, binding.action)};
            JS_SetPropertyStr(ctx, task, binding.name,
                              JS_NewCFunctionData(ctx, wxSocketAction, binding.length, 0, 2,
                                                  data));
            JS_FreeValue(ctx, data[1]);
        }
        JS_FreeValue(ctx, idValue);
        return task;
    }

    // ---- 音频（wx.createInnerAudioContext） ----

    static JSValue wxAudioCreate(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (!runtime) return JS_NewInt32(ctx, 0);
        return JS_NewInt32(ctx, runtime->createAudioContext());
    }

    static JSValue wxAudioAction(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc < 2) return JS_UNDEFINED;
        int id = 0;
        JS_ToInt32(ctx, &id, argv[0]);
        const std::string action = toStd(ctx, argv[1]);
        double value = 0.0;
        std::string text;
        if (argc > 2) {
            if (JS_IsString(argv[2])) {
                text = toStd(ctx, argv[2]);
            } else {
                value = numberArg(ctx, argv[2]);
            }
        }
        if (argc > 3) text = toStd(ctx, argv[3]);
        runtime->audioAction(id, action, value, text);
        return JS_UNDEFINED;
    }

    static JSValue wxAudioOn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc < 2) return JS_UNDEFINED;
        int id = 0;
        JS_ToInt32(ctx, &id, argv[0]);
        runtime->setAudioHandler(id, toStd(ctx, argv[1]), argc > 2 ? argv[2] : JS_UNDEFINED);
        return JS_UNDEFINED;
    }

    static JSValue wxAudioNumber(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        JsRuntime* runtime = self(ctx);
        if (!runtime || argc < 2) return JS_NewFloat64(ctx, 0.0);
        int id = 0;
        JS_ToInt32(ctx, &id, argv[0]);
        return JS_NewFloat64(ctx, runtime->audioNumber(id, toStd(ctx, argv[1])));
    }

    static JSValue wxCreateInnerAudioContext(JSContext* ctx, JSValueConst, int,
                                             JSValueConst*) {
        JsRuntime* runtime = self(ctx);
        if (!runtime) return JS_NewObject(ctx);
        const int id = runtime->createAudioContext();

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue factory = JS_GetPropertyStr(ctx, global, "__nx_make_audio");
        JSValue arg = JS_NewInt32(ctx, id);
        JSValue result = JS_Call(ctx, factory, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, arg);
        if (JS_IsException(result)) {
            logException(runtime, "createInnerAudioContext");
            JS_FreeValue(ctx, result);
            result = JS_NewObject(ctx);
        }
        JS_FreeValue(ctx, factory);
        JS_FreeValue(ctx, global);
        return result;
    }
};

JsRuntime::JsRuntime(JsRuntimeHost* host, std::string packageDir, std::string pagePath,
                     std::string appSource)
    : host(host),
      packageDir(std::move(packageDir)),
      pagePath(std::move(pagePath)),
      appSource(std::move(appSource)) {}

void JsRuntime::setTaskPoster(TaskPoster value) { poster = std::move(value); }

void JsRuntime::postToScript(std::function<void()> task) {
    if (!task) return;
    std::weak_ptr<JsRuntime> weak = weak_from_this();
    auto runner = [weak, task = std::move(task)]() mutable {
        auto runtime = weak.lock();
        if (!runtime) return;
        std::lock_guard<std::recursive_timed_mutex> lock(runtime->mutex);
        if (!runtime->context) return;
        runtime->syncStackTop();
        task();
    };
    if (poster) {
        poster(std::move(runner));
    } else {
        runner();
    }
}

JsRuntime::~JsRuntime() {
    std::lock_guard<std::recursive_timed_mutex> lock(mutex);
    if (alive) *alive = false;
    if (context) {
        clearTimers();
        for (JSValue fn : networkCallbacks) JS_FreeValue(context, fn);
        networkCallbacks.clear();
        for (auto& entry : audioHandlers) {
            AudioHandlers& handlers = entry.second;
            for (JSValue* slot : {&handlers.onPlay, &handlers.onPause, &handlers.onStop,
                                  &handlers.onEnded, &handlers.onCanplay, &handlers.onError,
                                  &handlers.onSeeked, &handlers.onTimeUpdate}) {
                JS_FreeValue(context, *slot);
            }
        }
        audioHandlers.clear();
        for (const auto& entry : audios) {
            if (host) host->audioControl(std::to_string(entry.first), "destroy");
        }
        audios.clear();
        for (auto& entry : expressionCache) {
            JS_FreeValue(context, entry.second);
        }
        expressionCache.clear();
        if (pageRegistered) JS_FreeValue(context, page);
        if (appRegistered) JS_FreeValue(context, app);
        page = JS_UNDEFINED;
        app = JS_UNDEFINED;
        JS_FreeContext(context);
        context = nullptr;
    }
    if (runtime) {
        JS_FreeRuntime(runtime);
        runtime = nullptr;
    }
}

bool JsRuntime::createContext(std::string& error) {
    runtime = JS_NewRuntime();
    if (!runtime) {
        error = "failed to create QuickJS runtime";
        return false;
    }
    JS_SetMaxStackSize(runtime, 2 * 1024 * 1024);

    context = JS_NewContext(runtime);
    if (!context) {
        error = "failed to create QuickJS context";
        return false;
    }
    JS_SetContextOpaque(context, this);
    installGlobals();
    installWx();
    return true;
}

bool JsRuntime::initialize(const std::string& script, const std::string& filename,
                           std::string& error) {
    std::lock_guard<std::recursive_timed_mutex> lock(mutex);
    if (!createContext(error)) return false;
    syncStackTop();

    if (!appSource.empty()) {
        std::string appMessage;
        if (!evaluateScript(appSource, "app.js", appMessage)) {
            // app.js 出错不应阻塞页面加载。
            brls::Logger::warning("miniapp: app.js error: {}", appMessage);
        }
    }
    brls::Logger::info("miniapp: page '{}' app.js evaluated", filename);

    if (!evaluateScript(script, filename, error)) return false;
    brls::Logger::info("miniapp: page '{}' script evaluated", filename);
    if (!finishPage(error)) return false;
    brls::Logger::info("miniapp: page '{}' runtime ready", filename);
    return true;
}

bool JsRuntime::evaluateScript(const std::string& source, const std::string& filename,
                               std::string& error) {
    JSValue result = JS_Eval(context, source.c_str(), source.size(), filename.c_str(),
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        error = "script error: " + exceptionText();
        JS_FreeValue(context, result);
        return false;
    }
    JS_FreeValue(context, result);
    drainJobs();
    return true;
}

bool JsRuntime::finishPage(std::string& error) {
    if (!pageRegistered) {
        error = "index.js did not call Page()";
        return false;
    }

    JSValue pageData = JS_GetPropertyStr(context, page, "data");
    if (!JS_IsObject(pageData)) {
        JS_FreeValue(context, pageData);
        pageData = JS_NewObject(context);
        JS_SetPropertyStr(context, page, "data", JS_DupValue(context, pageData));
    }
    JS_FreeValue(context, pageData);

    JS_SetPropertyStr(context, page, "setData",
                      JS_NewCFunction(context, RuntimeBridge::jsSetData, "setData", 2));
    JS_SetPropertyStr(context, page, "route",
                      JS_NewString(context, pagePath.c_str()));

    refreshData();
    return true;
}

bool JsRuntime::hasHandler(const std::string& name) const {
    if (!context || !pageRegistered || name.empty()) return false;
    std::lock_guard<std::recursive_timed_mutex> lock(mutex);
    JSValue value = JS_GetPropertyStr(context, page, name.c_str());
    bool result = JS_IsFunction(context, value);
    JS_FreeValue(context, value);
    return result;
}

bool JsRuntime::callLifecycle(const std::string& name, const JsonValue* query) {
    std::lock_guard<std::recursive_timed_mutex> lock(mutex);
    syncStackTop();
    if (!context || !pageRegistered) return false;
    JSValue fn = JS_GetPropertyStr(context, page, name.c_str());
    if (!JS_IsFunction(context, fn)) {
        JS_FreeValue(context, fn);
        return false;
    }
    bool ok = true;
    if (query && (name == "onLoad" || name == "onShow")) {
        JSValue arg = valueFromJson(*query);
        ok = callJsFunction(fn, page, 1, &arg, name);
        JS_FreeValue(context, arg);
    } else {
        ok = callJsFunction(fn, page, 0, nullptr, name);
    }
    JS_FreeValue(context, fn);
    return ok;
}

bool JsRuntime::dispatchEvent(const std::string& handler, const std::string& type,
                              const std::map<std::string, std::string>& dataset,
                              const JsonValue& detail) {
    // 事件在 UI 线程派发：最多等 8ms 拿脚本锁，拿不到就丢弃这次事件，
    // 不把输入卡在脚本线程上（8ms 仍在人眼可接受的一帧内）。
    std::unique_lock<std::recursive_timed_mutex> lock(mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(8))) {
        brls::Logger::debug("miniapp: dispatch {} skipped, script busy", handler);
        return false;
    }
    syncStackTop();
    if (!context || !pageRegistered || handler.empty()) return false;
    JSValue fn = JS_GetPropertyStr(context, page, handler.c_str());
    if (!JS_IsFunction(context, fn)) {
        JS_FreeValue(context, fn);
        return false;
    }
    JSValue event = makeEventValue(type, dataset, detail);
    bool ok = callJsFunction(fn, page, 1, &event, handler);
    JS_FreeValue(context, event);
    JS_FreeValue(context, fn);
    return ok;
}

bool JsRuntime::dispatchKey(const std::string& key, bool& consumed) {
    consumed = false;
    // 按键在 UI 线程派发：脚本线程正忙时按「未消费」处理，让焦点导航继续可用。
    std::unique_lock<std::recursive_timed_mutex> lock(mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(8))) return false;
    syncStackTop();
    if (!context || !pageRegistered || key.empty()) return false;
    JSValue fn = JS_GetPropertyStr(context, page, "onKeyDown");
    if (!JS_IsFunction(context, fn)) {
        JS_FreeValue(context, fn);
        return false;
    }
    JSValue event = JS_NewObject(context);
    JS_SetPropertyStr(context, event, "type", JS_NewString(context, "keydown"));
    JS_SetPropertyStr(context, event, "timeStamp",
                      JS_NewFloat64(context, nowMilliseconds()));
    JS_SetPropertyStr(context, event, "key", JS_NewString(context, key.c_str()));
    JSValue result = JS_Call(context, fn, page, 1, &event);
    if (JS_IsException(result)) {
        brls::Logger::error("miniapp js [onKeyDown]: {}", exceptionText());
        JS_FreeValue(context, result);
        JS_FreeValue(context, event);
        JS_FreeValue(context, fn);
        drainJobs();
        return true;
    }
    int truthy = JS_ToBool(context, result);
    consumed = truthy > 0;
    JS_FreeValue(context, result);
    JS_FreeValue(context, event);
    JS_FreeValue(context, fn);
    drainJobs();
    return true;
}

bool JsRuntime::callJsFunction(JSValue fn, JSValue thisValue, int argc, JSValue* argv,
                               const std::string& what) {
    if (!context || !JS_IsFunction(context, fn)) return false;
    JSValue result = JS_Call(context, fn, thisValue, argc, argv);
    if (JS_IsException(result)) {
        brls::Logger::error("miniapp js [{}]: {}", what, exceptionText());
        JS_FreeValue(context, result);
        drainJobs();
        return false;
    }
    JS_FreeValue(context, result);
    drainJobs();
    return true;
}

void JsRuntime::refreshData() {
    if (!context || !pageRegistered) return;
    JSValue pageData = JS_GetPropertyStr(context, page, "data");
    bool ok = false;
    std::string text = serializeJs(pageData, &ok);
    JS_FreeValue(context, pageData);
    if (!ok) return;
    std::string parseError;
    JsonValue parsed = parseJson(text, &parseError);
    if (parseError.empty()) {
        data_ = std::move(parsed);
        // UI 线程靠版本号判断要不要重新取一份 data 快照。
        dataVersion_.fetch_add(1, std::memory_order_relaxed);
    }
}

JsonValue JsRuntime::dataCopy() const {
    std::lock_guard<std::recursive_timed_mutex> lock(mutex);
    return data_;
}

bool JsRuntime::tryDataCopy(JsonValue& out, int timeoutMs) const {
    std::unique_lock<std::recursive_timed_mutex> lock(mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(timeoutMs))) return false;
    out = data_;
    return true;
}

void JsRuntime::drainJobs() {
    if (!runtime || !context) return;
    JSContext* jobContext = nullptr;
    JSValue job = JS_UNDEFINED;
    while (JS_ExecutePendingJob(runtime, &jobContext) > 0) {
        if (JS_IsException(job)) {
            brls::Logger::error("miniapp js [job]: {}", exceptionText());
        }
        JS_FreeValue(jobContext ? jobContext : context, job);
    }
}

std::string JsRuntime::exceptionText() {
    if (!context) return "no context";
    JSValue exception = JS_GetException(context);
    std::string message = toStd(context, exception);
    JS_FreeValue(context, exception);
    if (message.empty()) message = "unknown script error";
    return message;
}

void JsRuntime::syncStackTop() {
    if (context) JS_UpdateStackTop(JS_GetRuntime(context));
}

std::string JsRuntime::serializeJs(JSValue value, bool* ok) {
    if (ok) *ok = false;
    if (!context) return "null";
    if (JS_IsUndefined(value)) return "null";
    JSValue json = JS_JSONStringify(context, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        brls::Logger::error("miniapp js [stringify]: {}", exceptionText());
        JS_FreeValue(context, json);
        return "null";
    }
    if (JS_IsUndefined(json)) {
        JS_FreeValue(context, json);
        return "null";
    }
    std::string text = toStd(context, json);
    JS_FreeValue(context, json);
    if (ok) *ok = true;
    return text;
}

JSValue JsRuntime::valueFromJsonText(const std::string& text) {
    if (!context) return JS_UNDEFINED;
    JSValue value = JS_ParseJSON(context, text.c_str(), text.size(), "<json>");
    if (JS_IsException(value)) {
        JSValue exception = JS_GetException(context);
        JS_FreeValue(context, exception);
        return JS_NewObject(context);
    }
    return value;
}

JSValue JsRuntime::valueFromJson(const JsonValue& value) {
    if (!context) return JS_UNDEFINED;
    // 结构化转换：不再经过 JSON 文本，wx:for 里每层 item 的绑定省掉一次
    // stringify + parse。
    switch (value.type()) {
        case JsonValue::Type::Null: return JS_NULL;
        case JsonValue::Type::Bool: return JS_NewBool(context, value.asBool());
        case JsonValue::Type::Number: return JS_NewFloat64(context, value.asNumber());
        case JsonValue::Type::String: {
            const std::string& text = value.asString();
            return JS_NewStringLen(context, text.c_str(), text.size());
        }
        case JsonValue::Type::Array: {
            const auto& items = value.asArray();
            JSValue array = JS_NewArray(context);
            for (size_t i = 0; i < items.size(); ++i) {
                JS_SetPropertyUint32(context, array, static_cast<uint32_t>(i),
                                     valueFromJson(items[i]));
            }
            return array;
        }
        case JsonValue::Type::Object: {
            JSValue object = JS_NewObject(context);
            for (const auto& entry : value.asObject()) {
                JS_SetPropertyStr(context, object, entry.first.c_str(),
                                  valueFromJson(entry.second));
            }
            return object;
        }
    }
    return JS_UNDEFINED;
}

JSValue JsRuntime::makeEventValue(const std::string& type,
                                  const std::map<std::string, std::string>& dataset,
                                  const JsonValue& detail) {
    JSValue event = JS_NewObject(context);
    JS_SetPropertyStr(context, event, "type", JS_NewString(context, type.c_str()));
    JS_SetPropertyStr(context, event, "timeStamp",
                      JS_NewFloat64(context, nowMilliseconds()));
    JSValue datasetValue = makeStringMap(context, dataset);
    JSValue target = JS_NewObject(context);
    JS_SetPropertyStr(context, target, "id", JS_NewString(context, ""));
    JS_SetPropertyStr(context, target, "dataset", JS_DupValue(context, datasetValue));
    JSValue current = JS_NewObject(context);
    JS_SetPropertyStr(context, current, "id", JS_NewString(context, ""));
    JS_SetPropertyStr(context, current, "dataset", datasetValue);
    JS_SetPropertyStr(context, event, "target", target);
    JS_SetPropertyStr(context, event, "currentTarget", current);
    JS_SetPropertyStr(context, event, "detail", valueFromJson(detail));
    JS_SetPropertyStr(context, event, "touches", JS_NewArray(context));
    return event;
}

void JsRuntime::installGlobals() {
    JSValue global = JS_GetGlobalObject(context);

    JS_SetPropertyStr(context, global, "Page",
                      JS_NewCFunction(context, RuntimeBridge::jsPage, "Page", 1));
    JS_SetPropertyStr(context, global, "App",
                      JS_NewCFunction(context, RuntimeBridge::jsApp, "App", 1));
    JS_SetPropertyStr(context, global, "getApp",
                      JS_NewCFunction(context, RuntimeBridge::jsGetApp, "getApp", 0));
    JS_SetPropertyStr(context, global, "Component",
                      JS_NewCFunction(context, RuntimeBridge::jsComponent, "Component", 1));
    JS_SetPropertyStr(context, global,
                      "getCurrentPages",
                      JS_NewCFunction(context, RuntimeBridge::jsGetCurrentPages,
                                      "getCurrentPages", 0));
    JS_SetPropertyStr(context, global, "setTimeout",
                      JS_NewCFunction(context, RuntimeBridge::jsSetTimeout, "setTimeout", 2));
    JS_SetPropertyStr(context, global, "setInterval",
                      JS_NewCFunction(context, RuntimeBridge::jsSetInterval, "setInterval", 2));
    JS_SetPropertyStr(context, global, "clearTimeout",
                      JS_NewCFunction(context, RuntimeBridge::jsClearTimer, "clearTimeout", 1));
    JS_SetPropertyStr(context, global, "clearInterval",
                      JS_NewCFunction(context, RuntimeBridge::jsClearTimer, "clearInterval", 1));
    JS_SetPropertyStr(context, global, "requestAnimationFrame",
                      JS_NewCFunction(context, RuntimeBridge::jsRequestAnimationFrame,
                                      "requestAnimationFrame", 1));
    JS_SetPropertyStr(context, global, "cancelAnimationFrame",
                      JS_NewCFunction(context, RuntimeBridge::jsCancelAnimationFrame,
                                      "cancelAnimationFrame", 1));

    // wx.createInnerAudioContext 的原生底层，JS 外壳见 __nx_make_audio。
    JS_SetPropertyStr(context, global, "__nx_audio_create",
                      JS_NewCFunction(context, RuntimeBridge::wxAudioCreate,
                                      "__nx_audio_create", 0));
    JS_SetPropertyStr(context, global, "__nx_audio_action",
                      JS_NewCFunction(context, RuntimeBridge::wxAudioAction,
                                      "__nx_audio_action", 3));
    JS_SetPropertyStr(context, global, "__nx_audio_on",
                      JS_NewCFunction(context, RuntimeBridge::wxAudioOn,
                                      "__nx_audio_on", 3));
    JS_SetPropertyStr(context, global, "__nx_audio_number",
                      JS_NewCFunction(context, RuntimeBridge::wxAudioNumber,
                                      "__nx_audio_number", 2));

    JSValue console = JS_NewObject(context);
    JS_SetPropertyStr(context, console, "log",
                      JS_NewCFunction(context, RuntimeBridge::jsConsole, "log", 1));
    JS_SetPropertyStr(context, console, "info",
                      JS_NewCFunction(context, RuntimeBridge::jsConsole, "info", 1));
    JS_SetPropertyStr(context, console, "warn",
                      JS_NewCFunction(context, RuntimeBridge::jsConsole, "warn", 1));
    JS_SetPropertyStr(context, console, "error",
                      JS_NewCFunction(context, RuntimeBridge::jsConsole, "error", 1));
    JS_SetPropertyStr(context, global, "console", console);

    JS_FreeValue(context, global);

    JSValue helper = JS_Eval(context, kHelperScript,
                             std::char_traits<char>::length(kHelperScript),
                             "<helpers>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(helper)) {
        brls::Logger::error("miniapp js [helpers]: {}", exceptionText());
    }
    JS_FreeValue(context, helper);
}

void JsRuntime::installWx() {
    JSValue global = JS_GetGlobalObject(context);
    JSValue wx = JS_NewObject(context);

    auto set = [&](const char* name, JSCFunction* fn, int length) {
        JS_SetPropertyStr(context, wx, name,
                          JS_NewCFunction(context, fn, name, length));
    };

    set("showToast", RuntimeBridge::wxShowToast, 1);
    set("showLoading", RuntimeBridge::wxShowLoading, 1);
    set("hideLoading", RuntimeBridge::wxHideLoading, 0);
    set("showModal", RuntimeBridge::wxShowModal, 1);
    set("setNavigationBarTitle", RuntimeBridge::wxSetNavigationBarTitle, 1);
    set("getSystemInfoSync", RuntimeBridge::wxGetSystemInfoSync, 0);
    set("getSystemInfo", RuntimeBridge::wxGetSystemInfo, 1);
    set("getNetworkType", RuntimeBridge::wxGetNetworkType, 1);
    set("onNetworkStatusChange", RuntimeBridge::wxOnNetworkStatusChange, 1);
    set("offNetworkStatusChange", RuntimeBridge::wxOffNetworkStatusChange, 1);
    set("vibrateShort", RuntimeBridge::wxVibrateShort, 1);
    set("canIUse", RuntimeBridge::wxCanIUse, 1);
    set("nextTick", RuntimeBridge::wxNextTick, 1);

    set("getStorageInfoSync", RuntimeBridge::wxGetStorageInfoSync, 0);
    set("getStorageInfo", RuntimeBridge::wxGetStorageInfo, 1);
    set("setClipboardData", RuntimeBridge::wxSetClipboardData, 1);
    set("getClipboardData", RuntimeBridge::wxGetClipboardData, 1);
    set("getLaunchOptionsSync", RuntimeBridge::wxGetLaunchOptionsSync, 0);
    set("getEnterOptionsSync", RuntimeBridge::wxGetLaunchOptionsSync, 0);
    set("getMenuButtonBoundingClientRect",
        RuntimeBridge::wxGetMenuButtonBoundingClientRect, 0);
    set("getWindowInfo", RuntimeBridge::wxGetWindowInfo, 0);
    set("getDeviceInfo", RuntimeBridge::wxGetDeviceInfo, 0);
    set("getAppBaseInfo", RuntimeBridge::wxGetAppBaseInfo, 0);
    set("getUpdateManager", RuntimeBridge::wxGetUpdateManager, 0);

    for (const char* name : {"setNavigationBarColor", "showNavigationBarLoading",
                             "hideNavigationBarLoading", "setKeepScreenOn",
                             "setPreferredFramesPerSecond", "reportAnalytics",
                             "reportEvent", "onAppShow", "onAppHide", "onError",
                             "onUnhandledRejection", "offAppShow", "offAppHide",
                             "offError", "offUnhandledRejection", "pageScrollTo"}) {
        set(name, RuntimeBridge::wxNoop, 1);
    }

    set("setStorageSync", RuntimeBridge::wxSetStorageSync, 2);
    set("getStorageSync", RuntimeBridge::wxGetStorageSync, 1);
    set("removeStorageSync", RuntimeBridge::wxRemoveStorageSync, 1);
    set("clearStorageSync", RuntimeBridge::wxClearStorageSync, 0);
    set("setStorage", RuntimeBridge::wxSetStorage, 1);
    set("getStorage", RuntimeBridge::wxGetStorage, 1);
    set("removeStorage", RuntimeBridge::wxRemoveStorage, 1);
    set("clearStorage", RuntimeBridge::wxClearStorage, 1);

    set("request", RuntimeBridge::wxRequest, 1);
    set("createVideoContext", RuntimeBridge::wxCreateVideoContext, 1);
    set("createCanvasContext", RuntimeBridge::wxCreateCanvasContext, 1);
    set("getGamepadState", RuntimeBridge::wxGetGamepadState, 0);
    set("connectSocket", RuntimeBridge::wxConnectSocket, 1);
    set("createInnerAudioContext", RuntimeBridge::wxCreateInnerAudioContext, 0);
    set("navigateTo", RuntimeBridge::wxNavigateTo, 1);
    set("navigateBack", RuntimeBridge::wxNavigateBack, 1);
    set("redirectTo", RuntimeBridge::wxRedirectTo, 1);
    set("reLaunch", RuntimeBridge::wxReLaunch, 1);

    // wx.miniapp：小程序互跳与最近使用，供首页小程序组织内容。
    JSValue miniappNamespace = JS_NewObject(context);
    JS_SetPropertyStr(context, miniappNamespace, "launch",
                      JS_NewCFunction(context, RuntimeBridge::wxMiniAppLaunch, "launch", 1));
    JS_SetPropertyStr(context, miniappNamespace, "listRecent",
                      JS_NewCFunction(context, RuntimeBridge::wxMiniAppListRecent, "listRecent", 0));
    JS_SetPropertyStr(context, miniappNamespace, "deviceId",
                      JS_NewCFunction(context, RuntimeBridge::wxMiniAppDeviceId, "deviceId", 0));
    JS_SetPropertyStr(context, wx, "miniapp", miniappNamespace);

    // wx.smb：SMB 数据源能力。
    JSValue smbNamespace = JS_NewObject(context);
    JS_SetPropertyStr(context, smbNamespace, "connect",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxSmbConnect>,
                                      "connect", 1));
    JS_SetPropertyStr(context, smbNamespace, "listFiles",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxSmbListFiles>,
                                      "listFiles", 1));
    JS_SetPropertyStr(context, smbNamespace, "scan",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxSmbScan>,
                                      "scan", 1));
    JS_SetPropertyStr(context, smbNamespace, "cancelScan",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxSmbCancelScan>,
                                      "cancelScan", 0));
    JS_SetPropertyStr(context, smbNamespace, "disconnect",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxSmbDisconnect>,
                                      "disconnect", 1));
    JS_SetPropertyStr(context, smbNamespace, "streamUrl",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxSmbStreamUrl>,
                                      "streamUrl", 1));
    JS_SetPropertyStr(context, wx, "smb", smbNamespace);

    // wx.media：原生媒体库（增量扫描 + 本地索引）。
    JSValue mediaNamespace = JS_NewObject(context);
    JS_SetPropertyStr(context, mediaNamespace, "scan",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxMediaScan>,
                                      "scan", 1));
    JS_SetPropertyStr(context, mediaNamespace, "library",
                      JS_NewCFunction(context, RuntimeBridge::guardNative<RuntimeBridge::wxMediaLibrary>,
                                      "library", 1));
    JS_SetPropertyStr(context, wx, "media", mediaNamespace);

    JS_SetPropertyStr(context, global, "wx", wx);
    JS_FreeValue(context, global);
}

JsRuntime::SessionStatus JsRuntime::beginEvalSession(EvalSession& session, int timeoutMs) {
    // 上一轮如果没收尾（异常路径），先释放，避免锁泄漏。
    if (session.ownsLock) endEvalSession(session);
    // 树构建要连续进 JS 求值：拿不到锁就整帧跳过，绝不阻塞等脚本线程。
    std::unique_lock<std::recursive_timed_mutex> lock(mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(timeoutMs))) {
        return SessionStatus::Busy;
    }
    syncStackTop();
    if (!context || !pageRegistered) return SessionStatus::Unavailable;
    // 直接用 page.data 这个活对象做作用域原型，避免把数据再序列化一份。
    session.root = JS_GetPropertyStr(context, page, "data");
    if (!JS_IsObject(session.root)) {
        JS_FreeValue(context, session.root);
        session.root = JS_UNDEFINED;
        return SessionStatus::Unavailable;
    }
    session.ownsLock = true;
    lock.release();
    return SessionStatus::Ready;
}

void JsRuntime::endEvalSession(EvalSession& session) {
    if (context && !JS_IsUndefined(session.root)) {
        JS_FreeValue(context, session.root);
    }
    session.root = JS_UNDEFINED;
    if (session.ownsLock) {
        session.ownsLock = false;
        mutex.unlock();
    }
}

bool JsRuntime::evaluate(const std::string& expression, const EvalSession& session,
                         const std::vector<EvalLocal>& locals, JsonValue& out,
                         std::string& error) {
    out = JsonValue();
    error.clear();
    if (!context) {
        error = "no context";
        return false;
    }
    if (JS_IsUndefined(session.root)) {
        error = "no eval session";
        return false;
    }

    auto cached = expressionCache.find(expression);
    JSValue fn = JS_UNDEFINED;
    if (cached != expressionCache.end()) {
        fn = cached->second;
    } else {
        std::string source = "(function(__scope){ with(__scope){ return (" + expression + "); } })";
        JSValue compiled = JS_Eval(context, source.c_str(), source.size(), "<expr>",
                                   JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(compiled)) {
            error = exceptionText();
            JS_FreeValue(context, compiled);
            return false;
        }
        expressionCache[expression] = compiled;
        fn = compiled;
    }

    // 作用域对象：data 挂在原型链上，locals 作为自有属性覆盖同名 data 字段。
    // 每个表达式只额外绑定少量局部变量，代价与 data 规模无关。
    JSValue scope = JS_NewObject(context);
    if (!JS_IsUndefined(session.root)) JS_SetPrototype(context, scope, session.root);
    for (const auto& local : locals) {
        if (local.name.empty()) continue;
        JS_SetPropertyStr(context, scope, local.name.c_str(), valueFromJson(local.value));
    }

    JSValue result = JS_Call(context, fn, JS_UNDEFINED, 1, &scope);
    JS_FreeValue(context, scope);
    if (JS_IsException(result)) {
        error = exceptionText();
        JS_FreeValue(context, result);
        return false;
    }

    const bool converted = jsonFromJs(result, out, error);
    JS_FreeValue(context, result);
    return converted;
}

bool JsRuntime::jsonFromJs(JSValue value, JsonValue& out, std::string& error) {
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        out = JsonValue();
        return true;
    }
    if (JS_IsBool(value)) {
        out = JsonValue::boolean(JS_ToBool(context, value) != 0);
        return true;
    }
    if (JS_IsNumber(value)) {
        double number = 0.0;
        if (JS_ToFloat64(context, &number, value) < 0) {
            error = "number out of range";
            return false;
        }
        out = JsonValue::number(number);
        return true;
    }
    if (JS_IsString(value)) {
        const char* text = JS_ToCString(context, value);
        out = JsonValue::string(text ? text : "");
        if (text) JS_FreeCString(context, text);
        return true;
    }

    // 数组/对象仍走 JSON 文本，但只对这类表达式付费。
    const std::string text = serializeJs(value, nullptr);
    if (text == "null") {
        out = JsonValue();
        return true;
    }
    std::string parseError;
    out = parseJson(text, &parseError);
    if (!parseError.empty()) {
        error = parseError;
        return false;
    }
    return true;
}

int JsRuntime::addTimer(JSValue fn, double delay, bool repeat, double now) {
    Timer timer;
    timer.id = nextTimerId++;
    timer.repeat = repeat;
    timer.interval = std::max(delay, 1.0);
    timer.next = now + std::max(delay, 0.0);
    timer.fn = JS_DupValue(context, fn);
    timers.push_back(timer);
    return timer.id;
}

void JsRuntime::removeTimer(int id) {
    for (auto it = timers.begin(); it != timers.end(); ++it) {
        if (it->id == id) {
            JS_FreeValue(context, it->fn);
            timers.erase(it);
            return;
        }
    }
}

void JsRuntime::clearTimers() {
    for (auto& timer : timers) {
        if (context) JS_FreeValue(context, timer.fn);
        timer.fn = JS_UNDEFINED;
    }
    timers.clear();
    for (auto& frame : animationFrames) {
        if (context) JS_FreeValue(context, frame.fn);
        frame.fn = JS_UNDEFINED;
    }
    animationFrames.clear();
}

int JsRuntime::addAnimationFrame(JSValue fn) {
    AnimationFrame frame;
    frame.id = nextAnimationFrameId++;
    frame.fn = JS_DupValue(context, fn);
    animationFrames.push_back(frame);
    return frame.id;
}

void JsRuntime::cancelAnimationFrame(int id) {
    for (auto it = animationFrames.begin(); it != animationFrames.end(); ++it) {
        if (it->id == id) {
            JS_FreeValue(context, it->fn);
            animationFrames.erase(it);
            return;
        }
    }
}

void JsRuntime::pumpAnimationFrames(double nowMillisecondsValue) {
    if (!context || animationFrames.empty()) return;
    std::vector<JSValue> due;
    due.reserve(animationFrames.size());
    for (auto& frame : animationFrames) {
        due.push_back(JS_DupValue(context, frame.fn));
        JS_FreeValue(context, frame.fn);
    }
    animationFrames.clear();
    JSValue timestamp = JS_NewFloat64(context, nowMillisecondsValue);
    for (JSValue fn : due) {
        callJsFunction(fn, page, 1, &timestamp, "requestAnimationFrame");
        JS_FreeValue(context, fn);
    }
    JS_FreeValue(context, timestamp);
}

// 解析常见的 CSS 颜色写法；不支持的写法回退为黑色。
static void parseCanvasColor(const std::string& input, float out[4]) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    std::string s;
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(c);
    }
    if (s.empty()) return;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };

    if (s[0] == '#') {
        if (s.size() == 4) {
            out[0] = hex(s[1]) * 17 / 255.0f;
            out[1] = hex(s[2]) * 17 / 255.0f;
            out[2] = hex(s[3]) * 17 / 255.0f;
        } else if (s.size() == 7 || s.size() == 9) {
            out[0] = (hex(s[1]) * 16 + hex(s[2])) / 255.0f;
            out[1] = (hex(s[3]) * 16 + hex(s[4])) / 255.0f;
            out[2] = (hex(s[5]) * 16 + hex(s[6])) / 255.0f;
            if (s.size() == 9) out[3] = (hex(s[7]) * 16 + hex(s[8])) / 255.0f;
        }
        return;
    }

    if (s.compare(0, 4, "rgb(") == 0 || s.compare(0, 5, "rgba(") == 0) {
        size_t open = s.find('(');
        size_t close = s.find(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) return;
        std::string inner = s.substr(open + 1, close - open - 1);
        std::vector<float> values;
        std::string token;
        for (char c : inner + ",") {
            if (c == ',') {
                if (!token.empty()) values.push_back(static_cast<float>(std::atof(token.c_str())));
                token.clear();
            } else {
                token.push_back(c);
            }
        }
        if (values.size() >= 3) {
            out[0] = values[0] / 255.0f;
            out[1] = values[1] / 255.0f;
            out[2] = values[2] / 255.0f;
            if (values.size() >= 4) out[3] = values[3];
        }
        return;
    }

    struct Named {
        const char* name;
        float r, g, b;
    };
    static const Named kNamed[] = {
        {"white", 1, 1, 1},       {"black", 0, 0, 0},     {"red", 1, 0, 0},
        {"green", 0, 0.5f, 0},    {"lime", 0, 1, 0},      {"blue", 0, 0, 1},
        {"yellow", 1, 1, 0},      {"orange", 1, 0.647f, 0}, {"purple", 0.5f, 0, 0.5f},
        {"gray", 0.5f, 0.5f, 0.5f}, {"grey", 0.5f, 0.5f, 0.5f}, {"cyan", 0, 1, 1},
        {"magenta", 1, 0, 1},     {"transparent", 0, 0, 0},
    };
    for (const auto& named : kNamed) {
        if (s == named.name) {
            out[0] = named.r;
            out[1] = named.g;
            out[2] = named.b;
            if (s == "transparent") out[3] = 0.0f;
            return;
        }
    }
}

int JsRuntime::openSocket(const std::string& url) {
    const int id = nextSocketId++;
    auto client = std::make_shared<WsClient>();
    sockets[id] = client;
    client->start(url);
    return id;
}

void JsRuntime::setSocketHandler(int id, const std::string& event, JSValue handler) {
    SocketHandlers& handlers = socketHandlers[id];
    JSValue* target = nullptr;
    if (event == "open") {
        target = &handlers.onOpen;
    } else if (event == "message") {
        target = &handlers.onMessage;
    } else if (event == "error") {
        target = &handlers.onError;
    } else if (event == "close") {
        target = &handlers.onClose;
    }
    if (!target) return;
    JS_FreeValue(context, *target);
    *target = JS_DupValue(context, handler);
}

void JsRuntime::sendSocket(int id, const std::string& data) {
    auto it = sockets.find(id);
    if (it != sockets.end()) it->second->send(data);
}

void JsRuntime::closeSocket(int id) {
    auto handlerIt = socketHandlers.find(id);
    if (handlerIt != socketHandlers.end()) {
        JS_FreeValue(context, handlerIt->second.onOpen);
        JS_FreeValue(context, handlerIt->second.onMessage);
        JS_FreeValue(context, handlerIt->second.onError);
        JS_FreeValue(context, handlerIt->second.onClose);
        socketHandlers.erase(handlerIt);
    }
    auto socketIt = sockets.find(id);
    if (socketIt != sockets.end()) {
        socketIt->second->stop();
        sockets.erase(socketIt);
    }
}

void JsRuntime::pollSockets() {
    if (sockets.empty() || !context) return;

    std::vector<int> ids;
    ids.reserve(sockets.size());
    for (const auto& entry : sockets) ids.push_back(entry.first);

    for (int id : ids) {
        auto socketIt = sockets.find(id);
        auto handlerIt = socketHandlers.find(id);
        if (socketIt == sockets.end() || handlerIt == socketHandlers.end()) continue;

        std::deque<WsClient::Event> events;
        {
            std::lock_guard<std::mutex> lock(socketIt->second->mutex);
            events.swap(socketIt->second->events);
        }
        // 回调里可能直接 close()，先把回调值取出来，避免迭代器失效。
        JSValue openHandler = JS_DupValue(context, handlerIt->second.onOpen);
        JSValue messageHandler = JS_DupValue(context, handlerIt->second.onMessage);
        JSValue errorHandler = JS_DupValue(context, handlerIt->second.onError);
        JSValue closeHandler = JS_DupValue(context, handlerIt->second.onClose);

        for (const auto& event : events) {
            JSValue handler = JS_UNDEFINED;
            JsonValue payload = JsonValue::object();
            if (event.type == "open") {
                handler = openHandler;
                payload.setMember("header", JsonValue::object());
            } else if (event.type == "message") {
                handler = messageHandler;
                payload.setMember("data", JsonValue::string(event.data));
            } else if (event.type == "error") {
                handler = errorHandler;
                payload.setMember("errMsg", JsonValue::string(event.error.empty()
                                                                  ? "websocket error"
                                                                  : event.error));
            } else if (event.type == "close") {
                handler = closeHandler;
                payload.setMember("code", JsonValue::number(1000));
                payload.setMember("reason", JsonValue::string(""));
            }
            if (!JS_IsFunction(context, handler)) continue;

            JSValue argument = valueFromJson(payload);
            callJsFunction(handler, page, 1, &argument, "socket");
            JS_FreeValue(context, argument);

            if (sockets.find(id) == sockets.end()) break;
        }
        JS_FreeValue(context, openHandler);
        JS_FreeValue(context, messageHandler);
        JS_FreeValue(context, errorHandler);
        JS_FreeValue(context, closeHandler);
    }
}

int JsRuntime::createAudioContext() {
    const int id = nextAudioId++;
    audios[id] = AudioInstance();
    if (host) host->audioControl(std::to_string(id), "create");
    return id;
}

void JsRuntime::audioAction(int id, const std::string& action, double value,
                            const std::string& text) {
    auto it = audios.find(id);
    if (it == audios.end() || !host) return;
    AudioInstance& audio = it->second;
    const std::string key = std::to_string(id);

    if (action == "src") {
        audio.src = text;
        audio.lastLoaded = false;
        audio.lastPlaying = false;
        audio.lastEnded = false;
        audio.lastError.clear();
        host->audioControl(key, "src", 0.0, text);
        if (audio.autoplay) host->audioControl(key, "play");
    } else if (action == "autoplay") {
        audio.autoplay = value != 0.0;
        if (audio.autoplay && !audio.src.empty() && !audio.lastPlaying) {
            host->audioControl(key, "play");
        }
    } else if (action == "loop") {
        audio.loop = value != 0.0;
        host->audioControl(key, "loop", audio.loop ? 1.0 : 0.0);
    } else if (action == "volume") {
        audio.volume = value;
        host->audioControl(key, "volume", value * 100.0);
    } else if (action == "play") {
        audio.lastEnded = false;
        host->audioControl(key, "play", value);
    } else if (action == "pause") {
        host->audioControl(key, "pause");
    } else if (action == "stop") {
        audio.lastPlaying = false;
        audio.lastEnded = false;
        host->audioControl(key, "stop");
        auto handlerIt = audioHandlers.find(id);
        if (handlerIt != audioHandlers.end() && JS_IsFunction(context, handlerIt->second.onStop)) {
            callJsFunction(handlerIt->second.onStop, page, 0, nullptr, "onStop");
        }
    } else if (action == "seek") {
        host->audioControl(key, "seek", value);
        auto handlerIt = audioHandlers.find(id);
        if (handlerIt != audioHandlers.end() &&
            JS_IsFunction(context, handlerIt->second.onSeeked)) {
            callJsFunction(handlerIt->second.onSeeked, page, 0, nullptr, "onSeeked");
        }
    } else if (action == "destroy") {
        host->audioControl(key, "destroy");
        auto handlerIt = audioHandlers.find(id);
        if (handlerIt != audioHandlers.end()) {
            AudioHandlers& handlers = handlerIt->second;
            for (JSValue* slot : {&handlers.onPlay, &handlers.onPause, &handlers.onStop,
                                  &handlers.onEnded, &handlers.onCanplay, &handlers.onError,
                                  &handlers.onSeeked, &handlers.onTimeUpdate}) {
                JS_FreeValue(context, *slot);
                *slot = JS_UNDEFINED;
            }
            audioHandlers.erase(handlerIt);
        }
        audios.erase(it);
    }
}

void JsRuntime::setAudioHandler(int id, const std::string& event, JSValue handler) {
    if (audios.find(id) == audios.end()) return;
    AudioHandlers& handlers = audioHandlers[id];
    JSValue* target = nullptr;
    if (event == "play") {
        target = &handlers.onPlay;
    } else if (event == "pause") {
        target = &handlers.onPause;
    } else if (event == "stop") {
        target = &handlers.onStop;
    } else if (event == "ended") {
        target = &handlers.onEnded;
    } else if (event == "canplay") {
        target = &handlers.onCanplay;
    } else if (event == "error") {
        target = &handlers.onError;
    } else if (event == "seeked") {
        target = &handlers.onSeeked;
    } else if (event == "timeupdate") {
        target = &handlers.onTimeUpdate;
    }
    if (!target) return;
    JS_FreeValue(context, *target);
    *target = JS_IsFunction(context, handler) ? JS_DupValue(context, handler) : JS_UNDEFINED;
}

double JsRuntime::audioNumber(int id, const std::string& key) const {
    if (!host) return 0.0;
    const JsonValue state = host->audioState(std::to_string(id));
    const JsonValue* value = state.find(key);
    return value ? value->asNumber() : 0.0;
}

void JsRuntime::pollAudio() {
    if (!context || !host || audios.empty()) return;

    std::vector<int> ids;
    ids.reserve(audios.size());
    for (const auto& entry : audios) ids.push_back(entry.first);

    for (int id : ids) {
        auto audioIt = audios.find(id);
        if (audioIt == audios.end()) continue;
        AudioInstance& audio = audioIt->second;

        const JsonValue state = host->audioState(std::to_string(id));
        if (!state.isObject()) continue;

        const JsonValue* playingValue = state.find("playing");
        const JsonValue* loadedValue = state.find("loaded");
        const JsonValue* endedValue = state.find("ended");
        const JsonValue* errorValue = state.find("error");
        const bool playing = playingValue && playingValue->asBool();
        const bool loaded = loadedValue && loadedValue->asBool();
        const bool ended = endedValue && endedValue->asBool();
        const std::string error = errorValue ? errorValue->asString() : std::string();

        AudioHandlers handlers;
        auto handlerIt = audioHandlers.find(id);
        if (handlerIt != audioHandlers.end()) {
            handlers.onPlay = JS_DupValue(context, handlerIt->second.onPlay);
            handlers.onPause = JS_DupValue(context, handlerIt->second.onPause);
            handlers.onEnded = JS_DupValue(context, handlerIt->second.onEnded);
            handlers.onCanplay = JS_DupValue(context, handlerIt->second.onCanplay);
            handlers.onError = JS_DupValue(context, handlerIt->second.onError);
        }

        auto fire = [&](JSValue fn, const char* name) {
            if (JS_IsFunction(context, fn)) callJsFunction(fn, page, 0, nullptr, name);
        };
        if (loaded && !audio.lastLoaded) fire(handlers.onCanplay, "onCanplay");
        if (ended && !audio.lastEnded) fire(handlers.onEnded, "onEnded");
        if (playing && !audio.lastPlaying) fire(handlers.onPlay, "onPlay");
        if (!playing && audio.lastPlaying && !ended) fire(handlers.onPause, "onPause");
        if (!error.empty() && error != audio.lastError) fire(handlers.onError, "onError");

        JS_FreeValue(context, handlers.onPlay);
        JS_FreeValue(context, handlers.onPause);
        JS_FreeValue(context, handlers.onEnded);
        JS_FreeValue(context, handlers.onCanplay);
        JS_FreeValue(context, handlers.onError);

        // 回调里可能 destroy 了实例，重新确认再更新快照。
        auto latest = audios.find(id);
        if (latest == audios.end()) continue;
        latest->second.lastLoaded = loaded;
        latest->second.lastPlaying = playing;
        latest->second.lastEnded = ended;
        latest->second.lastError = error;
    }
}

bool JsRuntime::canvasSnapshot(const std::string& id, CanvasState& out) const {
    std::unique_lock<std::recursive_timed_mutex> lock(mutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(2))) return false;
    auto it = canvases.find(id);
    if (it == canvases.end()) return false;
    out = it->second;
    return true;
}

void JsRuntime::canvasOp(const std::string& id, const std::string& kind, JSContext* ctx,
                         int argc, JSValueConst* argv) {
    if (id.empty() || !ctx) return;
    CanvasState& canvas = canvases[id];

    auto num = [&](int index) { return index < argc ? numberArg(ctx, argv[index], 0.0) : 0.0; };
    auto text = [&](int index) { return index < argc ? toStd(ctx, argv[index]) : std::string(); };
    auto pushNumbers = [&](const char* name, std::initializer_list<double> values) {
        CanvasCommand command;
        command.kind = name;
        command.nums.assign(values);
        canvas.commands.push_back(std::move(command));
    };

    if (kind == "draw") {
        canvas.frame = canvas.commands;
        canvas.commands.clear();
    } else if (kind == "setFillStyle") {
        parseCanvasColor(text(0), canvas.fill);
    } else if (kind == "setStrokeStyle") {
        parseCanvasColor(text(0), canvas.stroke);
    } else if (kind == "setLineWidth") {
        canvas.lineWidth = static_cast<float>(std::max(0.0, num(0)));
    } else if (kind == "setFontSize") {
        canvas.fontSize = static_cast<float>(std::max(1.0, num(0)));
    } else if (kind == "setTextAlign") {
        const std::string align = text(0);
        canvas.textAlign = align == "center" ? 1 : (align == "right" || align == "end" ? 2 : 0);
    } else if (kind == "setGlobalAlpha") {
        canvas.globalAlpha = static_cast<float>(std::max(0.0, std::min(1.0, num(0))));
    } else if (kind == "fillRect" || kind == "strokeRect") {
        const bool fill = kind == "fillRect";
        const float* color = fill ? canvas.fill : canvas.stroke;
        CanvasCommand command;
        command.kind = kind;
        command.nums = {num(0), num(1), num(2), num(3),
                        color[0], color[1], color[2], color[3] * canvas.globalAlpha,
                        canvas.lineWidth};
        canvas.commands.push_back(std::move(command));
    } else if (kind == "clearRect") {
        pushNumbers("clearRect", {num(0), num(1), num(2), num(3)});
    } else if (kind == "beginPath") {
        pushNumbers("beginPath", {});
    } else if (kind == "closePath") {
        pushNumbers("closePath", {});
    } else if (kind == "moveTo") {
        pushNumbers("moveTo", {num(0), num(1)});
    } else if (kind == "lineTo") {
        pushNumbers("lineTo", {num(0), num(1)});
    } else if (kind == "rect") {
        pushNumbers("rect", {num(0), num(1), num(2), num(3)});
    } else if (kind == "arc") {
        pushNumbers("arc", {num(0), num(1), num(2), num(3), num(4), num(5)});
    } else if (kind == "fill") {
        CanvasCommand command;
        command.kind = "fill";
        command.nums = {canvas.fill[0], canvas.fill[1], canvas.fill[2],
                        canvas.fill[3] * canvas.globalAlpha};
        canvas.commands.push_back(std::move(command));
    } else if (kind == "stroke") {
        CanvasCommand command;
        command.kind = "stroke";
        command.nums = {canvas.stroke[0], canvas.stroke[1], canvas.stroke[2],
                        canvas.stroke[3] * canvas.globalAlpha, canvas.lineWidth};
        canvas.commands.push_back(std::move(command));
    } else if (kind == "fillText") {
        CanvasCommand command;
        command.kind = "fillText";
        command.text = text(0);
        command.nums = {num(1), num(2), canvas.fontSize, static_cast<double>(canvas.textAlign),
                        canvas.fill[0], canvas.fill[1], canvas.fill[2],
                        canvas.fill[3] * canvas.globalAlpha};
        canvas.commands.push_back(std::move(command));
    } else if (kind == "drawImage") {
        CanvasCommand command;
        command.kind = "drawImage";
        command.text = text(0);
        command.nums = {num(1), num(2), num(3), num(4)};
        canvas.commands.push_back(std::move(command));
    } else if (kind == "save") {
        CanvasState::Saved saved{};
        for (int i = 0; i < 4; ++i) {
            saved.fill[i] = canvas.fill[i];
            saved.stroke[i] = canvas.stroke[i];
        }
        saved.lineWidth = canvas.lineWidth;
        saved.fontSize = canvas.fontSize;
        saved.textAlign = canvas.textAlign;
        saved.globalAlpha = canvas.globalAlpha;
        canvas.stack.push_back(saved);
        pushNumbers("save", {});
    } else if (kind == "restore") {
        if (!canvas.stack.empty()) {
            const CanvasState::Saved& saved = canvas.stack.back();
            for (int i = 0; i < 4; ++i) {
                canvas.fill[i] = saved.fill[i];
                canvas.stroke[i] = saved.stroke[i];
            }
            canvas.lineWidth = saved.lineWidth;
            canvas.fontSize = saved.fontSize;
            canvas.textAlign = saved.textAlign;
            canvas.globalAlpha = saved.globalAlpha;
            canvas.stack.pop_back();
        }
        pushNumbers("restore", {});
    } else if (kind == "translate") {
        pushNumbers("translate", {num(0), num(1)});
    } else if (kind == "scale") {
        pushNumbers("scale", {num(0), num(1)});
    } else if (kind == "rotate") {
        pushNumbers("rotate", {num(0)});
    }
}

void JsRuntime::pollNetworkStatus(double nowMillisecondsValue) {
    if (!context || networkCallbacks.empty()) return;
    if (lastNetworkCheck != 0.0 && nowMillisecondsValue - lastNetworkCheck < 2000.0) return;
    lastNetworkCheck = nowMillisecondsValue;

    const std::string type = detectNetworkType();
    if (type == lastNetworkType) return;
    const bool hadBaseline = !lastNetworkType.empty();
    lastNetworkType = type;
    if (!hadBaseline) return;

    JSValue detail = JS_NewObject(context);
    JS_SetPropertyStr(context, detail, "isConnected",
                      JS_NewBool(context, type != "none"));
    JS_SetPropertyStr(context, detail, "networkType", JS_NewString(context, type.c_str()));
    for (const JSValue fn : networkCallbacks) {
        callJsFunction(fn, page, 1, &detail, "onNetworkStatusChange");
    }
    JS_FreeValue(context, detail);
}

void JsRuntime::pumpTimers(double nowMillisecondsValue) {
    // 由逻辑线程驱动：定时器/轮询都在这里跑，慢脚本只会拖慢自己。
    std::lock_guard<std::recursive_timed_mutex> lock(mutex);
    syncStackTop();
    if (!context) return;
    pollNetworkStatus(nowMillisecondsValue);
    pumpAnimationFrames(nowMillisecondsValue);
    pollSockets();
    // 音频状态轮询：50ms 一次足够，避免每帧都向宿主取快照。
    if (!audios.empty() && nowMillisecondsValue - lastAudioPoll >= 50.0) {
        lastAudioPoll = nowMillisecondsValue;
        pollAudio();
    }
    if (timers.empty()) return;

    struct Pending {
        JSValue fn = JS_UNDEFINED;
    };
    std::vector<Pending> due;
    for (auto it = timers.begin(); it != timers.end();) {
        if (nowMillisecondsValue < it->next) {
            ++it;
            continue;
        }
        Pending pending;
        pending.fn = JS_DupValue(context, it->fn);
        due.push_back(pending);
        if (it->repeat) {
            while (it->next <= nowMillisecondsValue) it->next += it->interval;
            ++it;
        } else {
            JS_FreeValue(context, it->fn);
            it = timers.erase(it);
        }
    }

    for (auto& pending : due) {
        callJsFunction(pending.fn, page, 0, nullptr, "timer");
        JS_FreeValue(context, pending.fn);
    }
}

}  // namespace miniapp
