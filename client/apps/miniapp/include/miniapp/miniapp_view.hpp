//
// miniapp: 承载 WXML 渲染树的 borealis 视图。
//

#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "miniapp/json.hpp"
#include "miniapp/render_node.hpp"
#include "miniapp/wxml_parser.hpp"
#include "miniapp/wxss.hpp"

namespace miniapp {

class MiniappView : public brls::View {
public:
    explicit MiniappView(std::string packagePath = "");
    ~MiniappView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    using LocalValue = std::pair<std::string, const JsonValue*>;

    void loadPackage();
    void buildTree();
    void rebuildLayout(float width, float height);

    void buildElement(const WxmlNode& node, const Style& parentStyle,
                      std::vector<SelectorNode>& ancestors,
                      const std::vector<LocalValue>& locals,
                      std::vector<std::unique_ptr<RenderNode>>& out, bool skipFor);
    void buildChildren(const WxmlNode& node, const Style& parentStyle,
                       std::vector<SelectorNode>& ancestors,
                       const std::vector<LocalValue>& locals,
                       std::vector<std::unique_ptr<RenderNode>>& out);
    std::string collectText(const WxmlNode& node,
                            const std::vector<LocalValue>& locals) const;

    const JsonValue* resolvePath(const std::string& path,
                                 const std::vector<LocalValue>& locals) const;
    std::string interpolate(const std::string& text,
                            const std::vector<LocalValue>& locals) const;
    std::string valueToText(const JsonValue& value) const;

    void drawNode(NVGcontext* vg, RenderNode& node, float x, float y);
    void drawTextBlock(NVGcontext* vg, const RenderNode& node, float x, float y,
                       float width);
    void drawError(NVGcontext* vg, float x, float y, float width, float height);

    std::string packagePathOverride;
    std::string packageDir;
    std::string appJsonText;
    std::string wxmlText;
    std::string wxssText;
    std::string jsText;
    std::string pagePath;
    std::string pageBackground = "#141518";
    std::string errorMessage;

    JsonValue data;
    WxssStylesheet stylesheet;
    WxmlNode wxmlRoot;
    std::unique_ptr<RenderNode> root;

    float rpxScale = 1.0f;
    float layoutWidth = -1.0f;
    float layoutHeight = -1.0f;
    int fontId = -1;
    bool loaded = false;
};

}  // namespace miniapp
