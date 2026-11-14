//
// miniapp: 把渲染树交给 Yoga 布局，并读取盒模型结果。
//

#pragma once

#include "miniapp/render_node.hpp"

struct NVGcontext;

namespace miniapp {

struct LayoutBox {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

void buildYogaTree(RenderNode& root, float width, float height, float rpxScale);
/// 增量同步：只为新增/被标记 layoutDirty 的节点重建 Yoga 属性，其余节点复用。
void syncYogaTree(RenderNode& root, float width, float height, float rpxScale);
void applyRootSize(RenderNode& root, float width, float height);
void calculateYogaTree(RenderNode& root, float width, float height,
                       NVGcontext* vg, int fontId);
LayoutBox layoutBox(const RenderNode& node);
void freeYogaTree(RenderNode& root);

/// 依据 Yoga 结果填充每个节点的绝对矩形（考虑 scroll-view 的滚动偏移）。
void updateGeometry(RenderNode& root, float originX, float originY);

}  // namespace miniapp
