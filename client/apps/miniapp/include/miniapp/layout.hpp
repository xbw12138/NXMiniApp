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
void applyRootSize(RenderNode& root, float width, float height);
void calculateYogaTree(RenderNode& root, float width, float height,
                       NVGcontext* vg, int fontId);
LayoutBox layoutBox(const RenderNode& node);
void freeYogaTree(RenderNode& root);

}  // namespace miniapp
