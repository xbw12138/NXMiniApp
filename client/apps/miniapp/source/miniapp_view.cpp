//
// miniapp: WXML → 渲染树 → Yoga 布局 → NanoVG 绘制。
//

#include "miniapp/miniapp_view.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/font.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "miniapp/js_runtime.hpp"
#include "miniapp/layout.hpp"
#include "miniapp/wxml_parser.hpp"
#include "miniapp/wxss.hpp"

namespace miniapp {

namespace {

// rpx 基准：750 设计宽映射到 640 逻辑宽，48rpx 在桌面 1280 宽下约 41px。
constexpr float kRpxDesignWidth = 750.0f;
constexpr float kRpxBaseWidth = 640.0f;

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

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

std::string directiveValue(const std::string& raw) {
    std::string value = trim(raw);
    if (value.size() >= 4 && value.compare(0, 2, "{{") == 0 &&
        value.compare(value.size() - 2, 2, "}}") == 0) {
        value = trim(value.substr(2, value.size() - 4));
    }
    return value;
}

bool isTruthy(const JsonValue* value) {
    if (!value) return false;
    switch (value->type()) {
        case JsonValue::Type::Null: return false;
        case JsonValue::Type::Bool: return value->asBool();
        case JsonValue::Type::Number: return value->asNumber() != 0.0;
        case JsonValue::Type::String: return !value->asString().empty();
        default: return true;
    }
}

NVGcolor toColor(const Rgba& rgba) {
    return nvgRGBAf(rgba.r, rgba.g, rgba.b, rgba.a);
}

float clampRadius(float radius, float width, float height) {
    return std::max(0.0f, std::min(radius, std::min(width, height) * 0.5f));
}

}  // namespace

MiniappView::MiniappView(std::string packagePath)
    : packagePathOverride(std::move(packagePath)) {
    rpxScale = kRpxBaseWidth / kRpxDesignWidth;

    // 没有焦点导航，直接注册全局返回/示例动作。
    this->registerAction("", brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->registerAction("", brls::BUTTON_A, [](brls::View*) {
        brls::Logger::info("miniapp: A pressed");
        return true;
    });

    this->loadPackage();
}

MiniappView::~MiniappView() {
    if (root) freeYogaTree(*root);
}

void MiniappView::loadPackage() {
    // 商店启动时会直接传入安装目录；桌面调试也可用 NXKIT_MINIAPP 指定包目录。
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

    pagePath = "pages/index/index";
    if (const JsonValue* pages = appJson.find("pages")) {
        if (pages->isArray() && pages->size() > 0) {
            pagePath = pages->at(0).asString();
            if (pagePath.empty()) pagePath = "pages/index/index";
        }
    }
    if (const JsonValue* window = appJson.find("window")) {
        if (const JsonValue* background = window->find("backgroundColor")) {
            if (background->isString()) pageBackground = background->asString();
        }
    }

    std::string wxmlPath = joinPath(packageDir, pagePath + ".wxml");
    std::string wxssPath = joinPath(packageDir, pagePath + ".wxss");
    std::string jsPath = joinPath(packageDir, pagePath + ".js");

    if (!readTextFile(wxmlPath, wxmlText)) {
        errorMessage = "cannot open " + wxmlPath;
        return;
    }
    if (!readTextFile(wxssPath, wxssText)) {
        errorMessage = "cannot open " + wxssPath;
        return;
    }
    if (!readTextFile(jsPath, jsText)) {
        errorMessage = "cannot open " + jsPath;
        return;
    }

    std::string dataJson;
    if (!runPageScript(jsText, pagePath + ".js", dataJson, errorMessage)) {
        return;
    }
    brls::Logger::info("miniapp: page data = {}", dataJson);

    data = parseJson(dataJson, &parseError);
    if (!parseError.empty()) {
        errorMessage = "page data: " + parseError;
        return;
    }

    wxmlRoot = parseWxml(wxmlText, &parseError);
    if (!parseError.empty()) {
        errorMessage = "WXML: " + parseError;
        return;
    }

    stylesheet = WxssStylesheet::parse(wxssText, &parseError);
    if (!parseError.empty()) {
        errorMessage = "WXSS: " + parseError;
        return;
    }

    fontId = brls::Application::getFont(brls::FONT_REGULAR);
    if (fontId == brls::FONT_INVALID) {
        brls::Logger::warning("miniapp: regular font is not loaded");
    }

    buildTree();
    loaded = errorMessage.empty() && root != nullptr;
}

void MiniappView::buildTree() {
    std::vector<std::unique_ptr<RenderNode>> roots;
    std::vector<SelectorNode> ancestors;
    std::vector<LocalValue> locals;
    buildChildren(wxmlRoot, inheritStyle(Style()), ancestors, locals, roots);

    if (roots.size() == 1) {
        root = std::move(roots[0]);
    } else if (!roots.empty()) {
        root = std::make_unique<RenderNode>();
        root->tag = "view";
        root->classes = {"page"};
        root->style = inheritStyle(Style());
        SelectorNode selector{"view", "", root->classes};
        stylesheet.apply(selector, ancestors, root->style, rpxScale);
        root->children = std::move(roots);
        root->fontSize = root->style.fontSize;
        root->lineHeight = effectiveLineHeight(root->style);
    }

    if (!root) {
        errorMessage = "WXML has no renderable root";
        return;
    }

    // app.json 的 window.backgroundColor 作为页面背景兜底。
    if (!root->style.hasBackground) {
        Rgba background;
        if (parseColor(pageBackground, background)) {
            root->style.hasBackground = true;
            root->style.background = background;
        }
    }
}

void MiniappView::buildChildren(const WxmlNode& node, const Style& parentStyle,
                                std::vector<SelectorNode>& ancestors,
                                const std::vector<LocalValue>& locals,
                                std::vector<std::unique_ptr<RenderNode>>& out) {
    for (const auto& child : node.children) {
        if (child.isText()) {
            std::string text = trim(interpolate(child.text, locals));
            if (text.empty()) continue;

            auto render = std::make_unique<RenderNode>();
            render->tag = "#text";
            render->text = text;
            render->style = inheritStyle(parentStyle);
            render->fontSize = render->style.fontSize;
            render->lineHeight = effectiveLineHeight(render->style);
            out.push_back(std::move(render));
            continue;
        }
        buildElement(child, parentStyle, ancestors, locals, out, false);
    }
}

void MiniappView::buildElement(const WxmlNode& node, const Style& parentStyle,
                               std::vector<SelectorNode>& ancestors,
                               const std::vector<LocalValue>& locals,
                               std::vector<std::unique_ptr<RenderNode>>& out,
                               bool skipFor) {
    const std::string* forAttr = node.attr("wx:for");
    if (forAttr && !skipFor) {
        const JsonValue* list = resolvePath(directiveValue(*forAttr), locals);
        if (!list || !list->isArray()) return;

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

        for (size_t i = 0; i < list->size(); ++i) {
            std::vector<LocalValue> scoped = locals;
            scoped.emplace_back(itemName, &list->at(i));
            JsonValue indexValue = JsonValue::number(static_cast<double>(i));
            scoped.emplace_back(indexName, &indexValue);
            buildElement(node, parentStyle, ancestors, scoped, out, true);
        }
        return;
    }

    if (const std::string* ifAttr = node.attr("wx:if")) {
        std::string raw = trim(*ifAttr);
        bool condition = false;
        if (raw == "true") {
            condition = true;
        } else {
            condition = isTruthy(resolvePath(directiveValue(raw), locals));
        }
        if (!condition) return;
    }

    std::string tag = node.tag;
    if (tag != "text" && tag != "image" && tag != "button") tag = "view";

    std::string classText;
    if (const std::string* attr = node.attr("class")) {
        classText = trim(interpolate(*attr, locals));
    }
    std::string idText;
    if (const std::string* attr = node.attr("id")) {
        idText = trim(interpolate(*attr, locals));
    }

    auto render = std::make_unique<RenderNode>();
    render->tag = tag;
    render->classes.clear();
    std::istringstream classStream(classText);
    std::string className;
    while (classStream >> className) render->classes.push_back(className);

    SelectorNode selector{tag, idText, render->classes};

    Style style = inheritStyle(parentStyle);
    if (tag == "button") {
        // 按钮默认深色文字，WXSS 的 color 仍会覆盖。
        style.hasColor = true;
        style.color = Rgba{0.04f, 0.10f, 0.07f, 1.0f};
    }
    stylesheet.apply(selector, ancestors, style, rpxScale);
    if (const std::string* inlineStyle = node.attr("style")) {
        auto declarations = parseDeclarations(interpolate(*inlineStyle, locals));
        applyDeclarations(style, declarations, rpxScale);
    }

    if (tag == "button") {
        if (!style.hasBackground) {
            style.hasBackground = true;
            style.background = Rgba{0.059f, 0.863f, 0.471f, 1.0f};
        }
        if (style.borderRadius <= 0.0f) style.borderRadius = 44.0f * rpxScale;
        render->text = trim(collectText(node, locals));
    } else if (tag == "image") {
        std::string src;
        if (const std::string* attr = node.attr("src")) {
            src = trim(interpolate(*attr, locals));
        }
        render->imageLabel = baseName(src);
        if (render->imageLabel.empty()) render->imageLabel = "image";
    } else if (tag == "text") {
        render->text = trim(collectText(node, locals));
    }

    render->style = style;
    render->fontSize = style.fontSize;
    render->lineHeight = effectiveLineHeight(style);

    if (tag != "text" && tag != "image" && tag != "button") {
        ancestors.push_back(selector);
        buildChildren(node, style, ancestors, locals, render->children);
        ancestors.pop_back();
    }

    out.push_back(std::move(render));
}

std::string MiniappView::collectText(const WxmlNode& node,
                                     const std::vector<LocalValue>& locals) const {
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

const JsonValue* MiniappView::resolvePath(const std::string& path,
                                          const std::vector<LocalValue>& locals) const {
    std::string full = trim(path);
    if (full.empty()) return nullptr;

    std::vector<std::string> parts;
    std::istringstream stream(full);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (!part.empty()) parts.push_back(part);
    }
    if (parts.empty()) return nullptr;

    const JsonValue* current = nullptr;
    for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
        if (it->first == parts[0]) {
            current = it->second;
            break;
        }
    }
    if (!current) current = data.find(parts[0]);
    if (!current) return nullptr;

    for (size_t i = 1; i < parts.size(); ++i) {
        if (!current->isObject()) return nullptr;
        current = current->find(parts[i]);
        if (!current) return nullptr;
    }
    return current;
}

std::string MiniappView::valueToText(const JsonValue& value) const {
    switch (value.type()) {
        case JsonValue::Type::String:
            return value.asString();
        case JsonValue::Type::Bool:
            return value.asBool() ? "true" : "false";
        case JsonValue::Type::Number: {
            double number = value.asNumber();
            if (std::fabs(number - std::round(number)) < 1e-9 &&
                std::fabs(number) < 1e9) {
                return std::to_string(static_cast<long long>(number));
            }
            std::ostringstream stream;
            stream << number;
            return stream.str();
        }
        default:
            return "";
    }
}

std::string MiniappView::interpolate(const std::string& text,
                                     const std::vector<LocalValue>& locals) const {
    std::string result;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t open = text.find("{{", pos);
        if (open == std::string::npos) {
            result += text.substr(pos);
            break;
        }
        result += text.substr(pos, open - pos);
        size_t close = text.find("}}", open + 2);
        if (close == std::string::npos) {
            result += text.substr(open);
            break;
        }
        const JsonValue* value = resolvePath(trim(text.substr(open + 2, close - open - 2)), locals);
        if (value) result += valueToText(*value);
        pos = close + 2;
    }
    return result;
}

void MiniappView::rebuildLayout(float width, float height) {
    if (!root) return;

    if (!root->yoga) {
        buildYogaTree(*root, width, height, rpxScale);
    } else {
        applyRootSize(*root, width, height);
    }

    calculateYogaTree(*root, width, height, brls::Application::getNVGContext(), fontId);

    layoutWidth = width;
    layoutHeight = height;
}

void MiniappView::draw(NVGcontext* vg, float x, float y, float width, float height,
                       brls::Style, brls::FrameContext*) {
    if (!loaded || !root) {
        drawError(vg, x, y, width, height);
        return;
    }

    if (!root->yoga || std::fabs(width - layoutWidth) > 0.5f ||
        std::fabs(height - layoutHeight) > 0.5f) {
        rebuildLayout(width, height);
    }

    drawNode(vg, *root, x, y);
}

void MiniappView::drawNode(NVGcontext* vg, RenderNode& node, float x, float y) {
    LayoutBox box = layoutBox(node);
    float width = box.width;
    float height = box.height;
    float drawX = x + box.left;
    float drawY = y + box.top;
    if (width <= 0.0f || height <= 0.0f) return;

    float radius = clampRadius(node.style.borderRadius, width, height);

    if (node.style.hasBackground && node.style.background.a > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, drawX, drawY, width, height, radius);
        nvgFillColor(vg, toColor(node.style.background));
        nvgFill(vg);
    }
    if (node.style.hasBorder && node.style.borderWidth > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, drawX, drawY, width, height, radius);
        nvgStrokeColor(vg, toColor(node.style.borderColor));
        nvgStrokeWidth(vg, node.style.borderWidth);
        nvgStroke(vg);
    }

    if (node.isText()) {
        drawTextBlock(vg, node, drawX, drawY, width);
        return;
    }

    if (node.isButton()) {
        if (!node.text.empty()) {
            nvgFontFaceId(vg, fontId);
            nvgFontSize(vg, node.fontSize);
            nvgFillColor(vg, toColor(node.style.color));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(vg, drawX + width * 0.5f, drawY + height * 0.5f,
                    node.text.c_str(), nullptr);
            if (node.style.bold) {
                nvgText(vg, drawX + width * 0.5f + 0.6f, drawY + height * 0.5f,
                        node.text.c_str(), nullptr);
            }
        }
        return;
    }

    if (node.isImage()) {
        if (!node.style.hasBackground) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, drawX, drawY, width, height, radius);
            nvgFillColor(vg, nvgRGBA(42, 45, 49, 255));
            nvgFill(vg);
        }
        nvgFontFaceId(vg, fontId);
        nvgFontSize(vg, std::min(node.fontSize, std::min(width, height) * 0.35f));
        nvgFillColor(vg, nvgRGBA(138, 144, 153, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, drawX + width * 0.5f, drawY + height * 0.5f,
                node.imageLabel.c_str(), nullptr);
        return;
    }

    for (auto& child : node.children) {
        drawNode(vg, *child, drawX, drawY);
    }
}

void MiniappView::drawTextBlock(NVGcontext* vg, const RenderNode& node, float x,
                                float y, float width) {
    if (node.text.empty() || width <= 0.0f) return;

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, node.fontSize);
    nvgFillColor(vg, toColor(node.style.color));
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

void MiniappView::drawError(NVGcontext* vg, float x, float y, float width,
                            float height) {
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(20, 21, 24));
    nvgFill(vg);

    nvgFontFaceId(vg, fontId);
    nvgFontSize(vg, 24);
    nvgFillColor(vg, nvgRGB(255, 107, 107));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + width * 0.5f, y + height * 0.5f,
            errorMessage.empty() ? "miniapp failed to load" : errorMessage.c_str(),
            nullptr);
}

}  // namespace miniapp
