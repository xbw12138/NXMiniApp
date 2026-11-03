//
// miniapp: WXML 数据绑定后的渲染节点。
//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "miniapp/style.hpp"

struct YGNode;
using YGNodeRef = YGNode*;

namespace miniapp {

struct RenderNode {
    std::string tag;
    std::string text;       // text / button 的展示文字
    std::string imageLabel;  // image 占位文字
    std::vector<std::string> classes;
    Style style;

    std::vector<std::unique_ptr<RenderNode>> children;
    YGNodeRef yoga = nullptr;

    float fontSize = 28.0f;
    float lineHeight = 39.2f;

    bool isText() const { return tag == "#text" || tag == "text"; }
    bool isButton() const { return tag == "button"; }
    bool isImage() const { return tag == "image"; }
};

}  // namespace miniapp
