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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>

#include "miniapp/js_runtime.hpp"
#include "miniapp/layout.hpp"
#include "miniapp/wxml_parser.hpp"
#include "miniapp/wxss.hpp"

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

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
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

}  // namespace

MiniappView::MiniappView(std::string packagePath)
    : packagePathOverride(std::move(packagePath)) {
    rpxScale = kRpxBaseWidth / kRpxDesignWidth;
    fontId = brls::Application::getFont(brls::FONT_REGULAR);
    if (fontId == brls::FONT_INVALID) {
        brls::Logger::warning("miniapp: regular font is not loaded");
    }

    this->setFocusable(true);

    auto action = [this](brls::View*) {
        this->handleActivate();
        return true;
    };
    this->registerAction("确认", brls::BUTTON_A, action);

    this->registerAction("返回", brls::BUTTON_B, [this](brls::View*) {
        if (modal.active) {
            bool confirm = false;
            auto done = modal.done;
            modal = ModalState{};
            if (done) done(confirm);
            return true;
        }
        if (!navigateBack(1)) brls::Application::popActivity();
        return true;
    });

    auto up = [this](brls::View*) {
        this->handleDirection(brls::FocusDirection::UP);
        return true;
    };
    auto down = [this](brls::View*) {
        this->handleDirection(brls::FocusDirection::DOWN);
        return true;
    };
    auto left = [this](brls::View*) {
        this->handleDirection(brls::FocusDirection::LEFT);
        return true;
    };
    auto right = [this](brls::View*) {
        this->handleDirection(brls::FocusDirection::RIGHT);
        return true;
    };
    this->registerAction("上", brls::BUTTON_UP, up);
    this->registerAction("上", brls::BUTTON_NAV_UP, up);
    this->registerAction("下", brls::BUTTON_DOWN, down);
    this->registerAction("下", brls::BUTTON_NAV_DOWN, down);
    this->registerAction("左", brls::BUTTON_LEFT, left);
    this->registerAction("左", brls::BUTTON_NAV_LEFT, left);
    this->registerAction("右", brls::BUTTON_RIGHT, right);
    this->registerAction("右", brls::BUTTON_NAV_RIGHT, right);

    // 鼠标左键 / 触摸点击：由手势识别器统一转发到渲染树命中测试。
    this->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END) {
                this->handlePointerTap(status.position.x, status.position.y);
            }
        }));

    this->loadPackage();

    runLoopToken = brls::Application::getRunLoopEvent()->subscribe([this]() {
        this->tick(steadyMilliseconds());
    });
    runLoopActive = true;

    // 视频走 OpenGL 直出，只能在 NanoVG 帧结束后、交换缓冲前绘制。
    postDrawToken = brls::Application::getPostDrawEvent()->subscribe([this]() {
        this->drawVideoSurface();
    });

    // 对接 wiliwili 风格播放器：用 MpvEvent 回调驱动状态，不轮询 mpv。
    nxkit::VideoView::instance().setStateCallback(
        [this](const nxkit::VideoState& vs) { this->onVideoState(vs); });
}

MiniappView::~MiniappView() {
    if (aliveToken) *aliveToken = false;
    if (runLoopActive) {
        brls::Application::getRunLoopEvent()->unsubscribe(runLoopToken);
        runLoopActive = false;
    }
    brls::Application::getPostDrawEvent()->unsubscribe(postDrawToken);
    nxkit::VideoView::instance().setStateCallback(nullptr);
    stopVideo();
    for (auto& page : pages) {
        cleanupPage(*page);
    }
    pages.clear();
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
    storageFile = nxkit::dataPath("miniapp/storage/" +
                                  std::to_string(static_cast<unsigned long long>(hash)) +
                                  ".json");
    nxkit::ensureDirectory(nxkit::dataPath("miniapp/storage"));
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
    for (const auto& entry : node.attributes) {
        if (entry.first.size() > 5 && entry.first.compare(0, 5, "data-") == 0) {
            render->dataset[entry.first.substr(5)] = interpolate(entry.second, locals);
        }
    }

    // 组件属性。
    if (tag == "image") {
        render->src = attributeText(node, "src", locals);
        render->imageMode = attributeText(node, "mode", locals);
        if (render->imageMode.empty()) render->imageMode = "scaleToFill";
        render->imageLabel = baseName(render->src);
        if (render->imageLabel.empty()) render->imageLabel = "image";
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

int MiniappView::imageTexture(const std::string& src, const std::string& pageDir) {
    if (src.empty() || src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0) {
        return -1;
    }
    std::string path = resolveLocalPath(src, pageDir);
    auto it = imageTextures.find(path);
    if (it != imageTextures.end()) return it->second;

    NVGcontext* vg = brls::Application::getNVGContext();
    int texture = nvgCreateImage(vg, path.c_str(), 0);
    if (texture <= 0) {
        texture = -1;
        brls::Logger::warning("miniapp: cannot load image {}", path);
    }
    imageTextures[path] = texture;
    return texture;
}

std::string MiniappView::resolveLocalPath(const std::string& src, const std::string& pageDir) {
    if (src.empty()) return src;
    if (src[0] == '/' || src[0] == '\\') return packageDir + src;
    return joinPath(pageDir, src);
}

void MiniappView::drawImage(NVGcontext* vg, RenderNode& node, float alpha) {
    int texture = imageTexture(node.src, current ? current->dir : packageDir);
    if (texture <= 0) {
        if (!node.style.hasBackground) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight,
                           clampRadius(node.style.borderRadius, node.absWidth, node.absHeight));
            nvgFillColor(vg, nvgRGBAf(0.16f, 0.17f, 0.19f, alpha));
            nvgFill(vg);
        }
        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, std::min(node.fontSize, std::min(node.absWidth, node.absHeight) * 0.3f));
        nvgFillColor(vg, nvgRGBAf(kTextMuted.r, kTextMuted.g, kTextMuted.b, alpha));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, node.absLeft + node.absWidth * 0.5f,
                node.absTop + node.absHeight * 0.5f, node.imageLabel.c_str(), nullptr);
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
    nvgScissor(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight);
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
    nvgScissor(vg, node.absLeft, node.absTop, node.absWidth, node.absHeight);

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
    if (!node || !node->videoPlaying || node->videoError) return;
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
    startVideo(node, true);

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

    std::string resolved = resolveLocalPath(node->videoSrc, current->dir);
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
    if (forceLoad || loaded != resolved) {
        player.setUrl(resolved);
        loaded = resolved;
        activeVideoSrc = resolved;
    }
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
    }
    current->runtime->dispatchEvent(handler, type, node->dataset, detail);
}

bool MiniappView::videoControl(const std::string& id, const std::string& action,
                               double value) {
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
    }
    return true;
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

void MiniappView::handleDirection(brls::FocusDirection direction) {
    if (modal.active) {
        if (direction == brls::FocusDirection::LEFT) modal.focus = 0;
        if (direction == brls::FocusDirection::RIGHT) modal.focus = 1;
        return;
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
        return;
    }
    moveFocus(node, direction);
}

void MiniappView::moveFocus(RenderNode* from, brls::FocusDirection direction) {
    if (!from || focusNodes.empty()) {
        if (!focusNodes.empty()) {
            focusPath = focusNodes.front()->path;
            ensureVisible(focusNodes.front());
        }
        return;
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
        switch (direction) {
            case brls::FocusDirection::RIGHT:
                if (dx < 8.0f) continue;
                primary = dx;
                secondary = std::fabs(dy);
                break;
            case brls::FocusDirection::LEFT:
                if (dx > -8.0f) continue;
                primary = -dx;
                secondary = std::fabs(dy);
                break;
            case brls::FocusDirection::DOWN:
                if (dy < 8.0f) continue;
                primary = dy;
                secondary = std::fabs(dx);
                break;
            case brls::FocusDirection::UP:
                if (dy > -8.0f) continue;
                primary = -dy;
                secondary = std::fabs(dx);
                break;
            default:
                continue;
        }
        float score = primary + secondary * 0.6f;
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }

    if (best) {
        focusPath = best->path;
        ensureVisible(best);
    }
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
    RenderNode* child = node;
    for (RenderNode* parent = node->parent; parent; parent = parent->parent) {
        if (!parent->isScrollView()) {
            child = parent;
            continue;
        }
        float contentTop = (child->absTop - parent->absTop) + parent->scrollOffset;
        float contentBottom = contentTop + child->absHeight;
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
        child = parent;
    }
}

void MiniappView::tick(double now) {
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
                              const std::string& body, HttpDone done) {
    nxkit::HttpClient client;
    for (const auto& entry : headers) client.setHeader(entry.first, entry.second);
    client.setTimeout(15000);

    if (method == "GET" || method == "HEAD") {
        client.getAsync(url, {}, [done](const nxkit::HttpResult& result) {
            done(result.ok, static_cast<int>(result.status), result.body, result.headers);
        });
    } else {
        nlohmann::json payload = nlohmann::json::object();
        if (!body.empty()) {
            try {
                payload = nlohmann::json::parse(body);
            } catch (...) {
                payload = body;
            }
        }
        client.postJsonAsync(url, {}, payload, [done](const nxkit::HttpResult& result) {
            done(result.ok, static_cast<int>(result.status), result.body, result.headers);
        });
    }
}

bool MiniappView::navigateTo(const std::string& url) {
    return openPage(url, true);
}

bool MiniappView::navigateBack(int delta) {
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
    info.setMember("version", JsonValue::string("1.0.0"));
    return info;
}

void MiniappView::vibrateShort() {
    // Switch 上没有通用短振动接口，保持 no-op。
}

bool MiniappView::storageGet(const std::string& key, std::string& value) {
    auto it = storageValues.find(key);
    if (it == storageValues.end()) return false;
    value = it->second;
    return true;
}

void MiniappView::storageSet(const std::string& key, const std::string& value) {
    storageValues[key] = value;
    saveStorage();
}

void MiniappView::storageRemove(const std::string& key) {
    storageValues.erase(key);
    saveStorage();
}

void MiniappView::storageClear() {
    storageValues.clear();
    saveStorage();
}

std::vector<std::string> MiniappView::storageKeys() {
    std::vector<std::string> keys;
    keys.reserve(storageValues.size());
    for (const auto& entry : storageValues) keys.push_back(entry.first);
    return keys;
}

std::string MiniappView::clipboardGet() {
    return clipboardText;
}

void MiniappView::clipboardSet(const std::string& text) {
    clipboardText = text;
}

void MiniappView::loadStorage() {
    std::string text;
    if (!readTextFile(storageFile, text)) return;
    JsonValue parsed = parseJson(text);
    if (!parsed.isObject()) return;
    for (const auto& entry : parsed.asObject()) {
        storageValues[entry.first] = toJsonString(entry.second);
    }
}

void MiniappView::saveStorage() {
    JsonValue root = JsonValue::object();
    for (const auto& entry : storageValues) {
        JsonValue value = parseJson(entry.second);
        root.setMember(entry.first, value);
    }
    std::ofstream file(storageFile, std::ios::trunc);
    if (!file) return;
    file << toJsonString(root);
}

}  // namespace miniapp
