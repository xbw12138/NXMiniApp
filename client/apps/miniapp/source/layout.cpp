//
// miniapp: Yoga 布局实现。故意不包含 borealis 头文件，避免与内置 Yoga 冲突。
//

#include "miniapp/layout.hpp"

// 使用 borealis 内置的 Yoga 头文件，避免与内置 yogacore 的符号冲突。
#include <yoga/Yoga.h>

#include <borealis/extern/nanovg-gl/nanovg.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace miniapp {

namespace {

struct MeasureContext {
    NVGcontext* vg = nullptr;
    int fontId = -1;
};

MeasureContext* gMeasureContext = nullptr;

// borealis 用全局 Yoga 事件订阅，把每个节点的 context 当作 brls::View* 调用
// onLayout()。因此小程序节点不能把自己的 RenderNode 放进 context（会崩溃），
// 这里改用独立的注册表，让节点 context 保持 nullptr。
std::unordered_map<YGNodeRef, RenderNode*> gNodeOwners;

YGSize measureTextNode(YGNodeRef yogaNode, float width, YGMeasureMode widthMode,
                       float, YGMeasureMode) {
    auto it = gNodeOwners.find(yogaNode);
    RenderNode* node = it == gNodeOwners.end() ? nullptr : it->second;
    if (!node || !gMeasureContext || !gMeasureContext->vg || node->text.empty()) {
        return YGSize{0.0f, 0.0f};
    }

    NVGcontext* vg = gMeasureContext->vg;
    nvgFontFaceId(vg, gMeasureContext->fontId);
    nvgFontSize(vg, node->fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    float available = widthMode == YGMeasureModeUndefined || width <= 0.0f
                          ? 100000.0f
                          : width;
    NVGtextRow rows[64];
    int count = nvgTextBreakLines(vg, node->text.c_str(), nullptr, available, rows, 64);

    float contentWidth = 0.0f;
    for (int i = 0; i < count; ++i) {
        contentWidth = std::max(contentWidth, rows[i].width);
    }
    float contentHeight = node->lineHeight * static_cast<float>(std::max(count, 1));

    if (widthMode == YGMeasureModeExactly) contentWidth = width;
    return YGSize{std::ceil(contentWidth), std::ceil(contentHeight)};
}

void setupNode(RenderNode& node, bool root, float rootWidth, float rootHeight,
               float rpxScale) {
    node.yoga = YGNodeNew();
    YGNodeSetContext(node.yoga, nullptr);
    gNodeOwners[node.yoga] = &node;

    const Style& style = node.style;
    YGNodeStyleSetFlexDirection(
        node.yoga, style.flexDirection == 1 ? YGFlexDirectionRow : YGFlexDirectionColumn);

    switch (style.justifyContent) {
        case 1: YGNodeStyleSetJustifyContent(node.yoga, YGJustifyCenter); break;
        case 2: YGNodeStyleSetJustifyContent(node.yoga, YGJustifyFlexEnd); break;
        case 3: YGNodeStyleSetJustifyContent(node.yoga, YGJustifySpaceBetween); break;
        case 4: YGNodeStyleSetJustifyContent(node.yoga, YGJustifySpaceAround); break;
        default: YGNodeStyleSetJustifyContent(node.yoga, YGJustifyFlexStart); break;
    }
    switch (style.alignItems) {
        case 1: YGNodeStyleSetAlignItems(node.yoga, YGAlignFlexStart); break;
        case 2: YGNodeStyleSetAlignItems(node.yoga, YGAlignCenter); break;
        case 3: YGNodeStyleSetAlignItems(node.yoga, YGAlignFlexEnd); break;
        default: YGNodeStyleSetAlignItems(node.yoga, YGAlignStretch); break;
    }

    switch (style.alignSelf) {
        case 1: YGNodeStyleSetAlignSelf(node.yoga, YGAlignFlexStart); break;
        case 2: YGNodeStyleSetAlignSelf(node.yoga, YGAlignCenter); break;
        case 3: YGNodeStyleSetAlignSelf(node.yoga, YGAlignFlexEnd); break;
        case 4: YGNodeStyleSetAlignSelf(node.yoga, YGAlignStretch); break;
        default: YGNodeStyleSetAlignSelf(node.yoga, YGAlignAuto); break;
    }

    YGNodeStyleSetFlexGrow(node.yoga, style.flexGrow);
    YGNodeStyleSetFlexShrink(node.yoga, style.flexShrink);
    if (style.hasFlexBasis) {
        if (style.flexBasisPercent) {
            YGNodeStyleSetFlexBasisPercent(node.yoga, style.flexBasis);
        } else {
            YGNodeStyleSetFlexBasis(node.yoga, style.flexBasis);
        }
    }
    if (style.displayNone) {
        YGNodeStyleSetDisplay(node.yoga, YGDisplayNone);
    }
    if (style.absolute) {
        YGNodeStyleSetPositionType(node.yoga, YGPositionTypeAbsolute);
        if (style.hasLeft) YGNodeStyleSetPosition(node.yoga, YGEdgeLeft, style.left);
        if (style.hasTop) YGNodeStyleSetPosition(node.yoga, YGEdgeTop, style.top);
        if (style.hasRight) YGNodeStyleSetPosition(node.yoga, YGEdgeRight, style.right);
        if (style.hasBottom) YGNodeStyleSetPosition(node.yoga, YGEdgeBottom, style.bottom);
    }

    if (style.hasMinWidth) {
        if (style.minWidthPercent) {
            YGNodeStyleSetMinWidthPercent(node.yoga, style.minWidth);
        } else {
            YGNodeStyleSetMinWidth(node.yoga, style.minWidth);
        }
    }
    if (style.hasMinHeight) {
        if (style.minHeightPercent) {
            YGNodeStyleSetMinHeightPercent(node.yoga, style.minHeight);
        } else {
            YGNodeStyleSetMinHeight(node.yoga, style.minHeight);
        }
    }
    if (style.hasMaxWidth) {
        if (style.maxWidthPercent) {
            YGNodeStyleSetMaxWidthPercent(node.yoga, style.maxWidth);
        } else {
            YGNodeStyleSetMaxWidth(node.yoga, style.maxWidth);
        }
    }
    if (style.hasMaxHeight) {
        if (style.maxHeightPercent) {
            YGNodeStyleSetMaxHeightPercent(node.yoga, style.maxHeight);
        } else {
            YGNodeStyleSetMaxHeight(node.yoga, style.maxHeight);
        }
    }

    YGNodeStyleSetPadding(node.yoga, YGEdgeTop, style.padding.top);
    YGNodeStyleSetPadding(node.yoga, YGEdgeRight, style.padding.right);
    YGNodeStyleSetPadding(node.yoga, YGEdgeBottom, style.padding.bottom);
    YGNodeStyleSetPadding(node.yoga, YGEdgeLeft, style.padding.left);

    YGNodeStyleSetMargin(node.yoga, YGEdgeTop, style.margin.top);
    YGNodeStyleSetMargin(node.yoga, YGEdgeRight, style.margin.right);
    YGNodeStyleSetMargin(node.yoga, YGEdgeBottom, style.margin.bottom);
    YGNodeStyleSetMargin(node.yoga, YGEdgeLeft, style.margin.left);

    if (root) {
        YGNodeStyleSetWidth(node.yoga, rootWidth);
        YGNodeStyleSetHeight(node.yoga, rootHeight);
    } else if (style.hasWidth) {
        if (style.widthPercent) {
            YGNodeStyleSetWidthPercent(node.yoga, style.width);
        } else {
            YGNodeStyleSetWidth(node.yoga, style.width);
        }
    }
    if (!root && style.hasHeight) {
        if (style.heightPercent) {
            YGNodeStyleSetHeightPercent(node.yoga, style.height);
        } else {
            YGNodeStyleSetHeight(node.yoga, style.height);
        }
    }

    if (node.isButton()) {
        if (!style.hasWidth) YGNodeStyleSetWidth(node.yoga, 320.0f * rpxScale);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 88.0f * rpxScale);
    }
    if (node.isImage()) {
        if (!style.hasWidth) YGNodeStyleSetWidth(node.yoga, 200.0f * rpxScale);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 120.0f * rpxScale);
    }
    if (node.tag == "swiper") {
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 300.0f * rpxScale);
    }
    if (node.tag == "swiper-item") {
        if (!style.hasWidth) YGNodeStyleSetWidthPercent(node.yoga, 100.0f);
        if (!style.hasHeight) YGNodeStyleSetHeightPercent(node.yoga, 100.0f);
    }
    if (node.tag == "input" || node.tag == "navigator") {
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 80.0f * rpxScale);
    }
    if (node.tag == "textarea" && !style.hasHeight) {
        YGNodeStyleSetHeight(node.yoga, 160.0f * rpxScale);
    }
    if (node.tag == "progress") {
        if (!style.hasWidth) YGNodeStyleSetWidthPercent(node.yoga, 100.0f);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 24.0f * rpxScale);
    }
    if (node.tag == "icon") {
        if (!style.hasWidth) YGNodeStyleSetWidth(node.yoga, 48.0f * rpxScale);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 48.0f * rpxScale);
    }
    if (node.tag == "rich-text" && !style.hasWidth) {
        YGNodeStyleSetWidthPercent(node.yoga, 100.0f);
    }
    if (node.tag == "navigator" && !style.hasWidth) {
        YGNodeStyleSetWidthPercent(node.yoga, 100.0f);
    }
    if (node.tag == "switch") {
        if (!style.hasWidth) YGNodeStyleSetWidth(node.yoga, 96.0f * rpxScale);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 56.0f * rpxScale);
    }
    if (node.tag == "checkbox" || node.tag == "radio") {
        if (!style.hasWidth) YGNodeStyleSetWidth(node.yoga, 48.0f * rpxScale);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 48.0f * rpxScale);
    }
    if (node.tag == "slider") {
        if (!style.hasWidth) YGNodeStyleSetWidthPercent(node.yoga, 100.0f);
        if (!style.hasHeight) YGNodeStyleSetHeight(node.yoga, 80.0f * rpxScale);
    }
    if (node.isText() || node.isButton() || node.tag == "rich-text") {
        YGNodeSetMeasureFunc(node.yoga, measureTextNode);
    }

    for (size_t i = 0; i < node.children.size(); ++i) {
        auto& child = *node.children[i];
        // Yoga 1.19 没有 gap，用子节点外边距模拟列/行间距。
        if (style.gap > 0.0f && i > 0) {
            if (style.flexDirection == 1) {
                child.style.margin.left += style.gap;
            } else {
                child.style.margin.top += style.gap;
            }
        }
        setupNode(child, false, rootWidth, rootHeight, rpxScale);
        YGNodeInsertChild(node.yoga, child.yoga, i);
    }
}

}  // namespace

void buildYogaTree(RenderNode& root, float width, float height, float rpxScale) {
    gNodeOwners.clear();
    setupNode(root, true, width, height, rpxScale);
}

void applyRootSize(RenderNode& root, float width, float height) {
    if (!root.yoga) return;
    YGNodeStyleSetWidth(root.yoga, width);
    YGNodeStyleSetHeight(root.yoga, height);
    YGNodeMarkDirty(root.yoga);
}

void calculateYogaTree(RenderNode& root, float width, float height,
                       NVGcontext* vg, int fontId) {
    if (!root.yoga) return;
    MeasureContext measureContext;
    measureContext.vg = vg;
    measureContext.fontId = fontId;
    gMeasureContext = &measureContext;
    YGNodeCalculateLayout(root.yoga, width, height, YGDirectionLTR);
    gMeasureContext = nullptr;
}

LayoutBox layoutBox(const RenderNode& node) {
    LayoutBox box;
    if (!node.yoga) return box;
    box.left = YGNodeLayoutGetLeft(node.yoga);
    box.top = YGNodeLayoutGetTop(node.yoga);
    box.width = YGNodeLayoutGetWidth(node.yoga);
    box.height = YGNodeLayoutGetHeight(node.yoga);
    return box;
}

void freeYogaTree(RenderNode& root) {
    if (root.yoga) {
        gNodeOwners.clear();
        YGNodeFreeRecursive(root.yoga);
        root.yoga = nullptr;
    }
}

void updateGeometry(RenderNode& node, float originX, float originY) {
    LayoutBox box = layoutBox(node);
    node.absLeft = originX + box.left;
    node.absTop = originY + box.top;
    node.absWidth = box.width;
    node.absHeight = box.height;

    // scroll-view 的内容尺寸按子节点在 Yoga 中的原始位置统计。
    if (node.isScrollView()) {
        float contentBottom = 0.0f;
        for (const auto& child : node.children) {
            if (!child->yoga) continue;
            contentBottom = std::max(contentBottom,
                                    YGNodeLayoutGetTop(child->yoga) +
                                        YGNodeLayoutGetHeight(child->yoga));
        }
        node.contentHeight = contentBottom;
    }

    const float childOriginY =
        node.isScrollView() ? node.absTop - node.scrollOffset : node.absTop;
    const float childOriginX =
        node.isScrollView() && node.horizontal ? node.absLeft - node.scrollOffset
                                               : node.absLeft;
    for (auto& child : node.children) {
        updateGeometry(*child, childOriginX, childOriginY);
    }
}

}  // namespace miniapp
