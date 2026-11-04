//
// miniapp: 长生命周期 QuickJS 运行时实现。
//

#include "miniapp/js_runtime.hpp"

#include <quickjs.h>

#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>

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

}  // namespace

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
        runtime->host->showModal(
            toStd(ctx, title), toStd(ctx, content),
            boolArg(ctx, showCancel, true),
            [runtime, weak, successDup, completeDup](bool confirm) {
                brls::sync([runtime, weak, successDup, completeDup, confirm]() {
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
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "networkType", JS_NewString(ctx, "wifi"));
        invoke(runtime, success, runtime->page, 1, &result);
        invoke(runtime, complete, runtime->page, 1, &result);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, success);
        JS_FreeValue(ctx, complete);
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

        std::string method = toStd(ctx, methodValue);
        if (method.empty()) method = "GET";
        for (char& c : method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string url = toStd(ctx, urlValue);
        std::string body;
        std::map<std::string, std::string> headers;

        bool ok = false;
        if (!JS_IsUndefined(dataValue) && !JS_IsNull(dataValue)) {
            std::string text = runtime->serializeJs(dataValue, &ok);
            if (ok) {
                if (method == "GET" || method == "HEAD") {
                    JsonValue dataJson = parseJson(text);
                    std::string query = makeQuery(dataJson);
                    if (!query.empty()) {
                        url += (url.find('?') == std::string::npos ? "?" : "&") + query;
                    }
                } else {
                    body = text;
                }
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

        runtime->host->httpRequest(
            method, url, headers, body,
            [runtime, weak, successDup, failDup, completeDup](
                bool requestOk, int status, const std::string& responseBody,
                const std::map<std::string, std::string>& responseHeaders) {
                brls::sync([runtime, weak, successDup, failDup, completeDup, requestOk,
                            status, responseBody, responseHeaders]() {
                    auto token = weak.lock();
                    if (!token || !*token || !runtime->context) return;
                    JSContext* ctx = runtime->context;
                    JSValue result = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, result, "statusCode", JS_NewInt32(ctx, status));
                    JSValue responseData = JS_NewString(ctx, responseBody.c_str());
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
                    JS_SetPropertyStr(ctx, result, "data", responseData);
                    JS_SetPropertyStr(ctx, result, "header",
                                      makeStringMap(ctx, responseHeaders));
                    JS_SetPropertyStr(ctx, result, "cookies", JS_NewObject(ctx));
                    if (requestOk) {
                        invoke(runtime, successDup, runtime->page, 1, &result);
                    } else {
                        JSValue failed = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, failed, "errMsg",
                                          JS_NewString(ctx, "request:fail"));
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
};

JsRuntime::JsRuntime(JsRuntimeHost* host, std::string packageDir, std::string pagePath,
                     std::string appSource)
    : host(host),
      packageDir(std::move(packageDir)),
      pagePath(std::move(pagePath)),
      appSource(std::move(appSource)) {}

JsRuntime::~JsRuntime() {
    if (alive) *alive = false;
    if (context) {
        clearTimers();
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
    if (!createContext(error)) return false;

    if (!appSource.empty()) {
        std::string appMessage;
        if (!evaluateScript(appSource, "app.js", appMessage)) {
            // app.js 出错不应阻塞页面加载。
            brls::Logger::warning("miniapp: app.js error: {}", appMessage);
        }
    }

    if (!evaluateScript(script, filename, error)) return false;
    if (!finishPage(error)) return false;
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
    JSValue value = JS_GetPropertyStr(context, page, name.c_str());
    bool result = JS_IsFunction(context, value);
    JS_FreeValue(context, value);
    return result;
}

bool JsRuntime::callLifecycle(const std::string& name, const JsonValue* query) {
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
    if (parseError.empty()) data_ = std::move(parsed);
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
    return valueFromJsonText(toJsonString(value));
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
                             "reportEvent", "onNetworkStatusChange", "onAppShow",
                             "onAppHide", "onError", "onUnhandledRejection",
                             "offNetworkStatusChange", "offAppShow", "offAppHide",
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
    set("navigateTo", RuntimeBridge::wxNavigateTo, 1);
    set("navigateBack", RuntimeBridge::wxNavigateBack, 1);
    set("redirectTo", RuntimeBridge::wxRedirectTo, 1);
    set("reLaunch", RuntimeBridge::wxReLaunch, 1);

    JS_SetPropertyStr(context, global, "wx", wx);
    JS_FreeValue(context, global);
}

bool JsRuntime::evaluate(const std::string& expression, const JsonValue& scope,
                         JsonValue& out, std::string& error) {
    out = JsonValue();
    error.clear();
    if (!context) {
        error = "no context";
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

    JSValue scopeValue = valueFromJson(scope);
    JSValue result = JS_Call(context, fn, JS_UNDEFINED, 1, &scopeValue);
    JS_FreeValue(context, scopeValue);
    if (JS_IsException(result)) {
        error = exceptionText();
        JS_FreeValue(context, result);
        return false;
    }

    bool ok = false;
    std::string text = serializeJs(result, &ok);
    JS_FreeValue(context, result);
    if (!ok) {
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
}

void JsRuntime::pumpTimers(double nowMillisecondsValue) {
    if (!context || timers.empty()) return;

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
