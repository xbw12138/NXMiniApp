//
// miniapp: QuickJS 执行环境（只注入全局 Page）。
//

#include "miniapp/js_runtime.hpp"

#include <quickjs.h>

namespace miniapp {

namespace {

struct PageState {
    JSValue page = JS_UNDEFINED;
    bool hasPage = false;
};

JSValue pageCallback(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* state = static_cast<PageState*>(JS_GetContextOpaque(ctx));
    if (!state || argc <= 0) return JS_UNDEFINED;
    if (state->hasPage) JS_FreeValue(ctx, state->page);
    state->page = JS_DupValue(ctx, argv[0]);
    state->hasPage = true;
    return JS_UNDEFINED;
}

std::string takeException(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    const char* text = JS_ToCString(ctx, exception);
    std::string message = text ? text : "unknown script error";
    if (text) JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
    return message;
}

}  // namespace

bool runPageScript(const std::string& source, const std::string& filename,
                   std::string& jsonOut, std::string& error) {
    jsonOut.clear();
    error.clear();

    JSRuntime* runtime = JS_NewRuntime();
    if (!runtime) {
        error = "failed to create QuickJS runtime";
        return false;
    }
    JS_SetMaxStackSize(runtime, 2 * 1024 * 1024);

    JSContext* context = JS_NewContext(runtime);
    if (!context) {
        error = "failed to create QuickJS context";
        JS_FreeRuntime(runtime);
        return false;
    }

    PageState state;
    JS_SetContextOpaque(context, &state);

    JSValue global = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, global, "Page",
                      JS_NewCFunction(context, pageCallback, "Page", 1));
    JS_FreeValue(context, global);

    JSValue result = JS_Eval(context, source.c_str(), source.size(),
                             filename.c_str(), JS_EVAL_TYPE_GLOBAL);

    bool success = false;
    if (JS_IsException(result)) {
        error = "script error: " + takeException(context);
    } else if (!state.hasPage) {
        error = "index.js did not call Page()";
    } else {
        // 取 pageObj.data，作为渲染数据根对象。
        JSValue pageData = JS_GetPropertyStr(context, state.page, "data");
        if (JS_IsUndefined(pageData) || JS_IsNull(pageData)) {
            JS_FreeValue(context, pageData);
            pageData = JS_DupValue(context, state.page);
        }
        JSValue json = JS_JSONStringify(context, pageData, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json)) {
            error = "JSON.stringify failed: " + takeException(context);
        } else {
            const char* text = JS_ToCString(context, json);
            if (text) {
                jsonOut = text;
                JS_FreeCString(context, text);
                success = true;
            } else {
                error = "failed to read page data";
            }
            JS_FreeValue(context, json);
        }
        JS_FreeValue(context, pageData);
    }
    JS_FreeValue(context, result);

    if (state.hasPage) JS_FreeValue(context, state.page);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return success;
}

}  // namespace miniapp
