//
// miniapp: 样式值、颜色和 WXSS 声明的解析。
//

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace miniapp {

struct Rgba {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct Edges {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};

struct Style {
    bool hasBackground = false;
    Rgba background;

    bool hasColor = true;
    Rgba color;

    float fontSize = 28.0f;
    bool bold = false;

    bool hasWidth = false;
    float width = 0.0f;
    bool widthPercent = false;

    bool hasHeight = false;
    float height = 0.0f;
    bool heightPercent = false;

    Edges padding;
    Edges margin;

    float borderRadius = 0.0f;

    // 0 = column, 1 = row.
    int flexDirection = 0;
    // 0 flex-start, 1 center, 2 flex-end, 3 space-between, 4 space-around.
    int justifyContent = 0;
    // 0 stretch, 1 flex-start, 2 center, 3 flex-end.
    int alignItems = 0;
    float gap = 0.0f;

    bool hasLineHeight = false;
    float lineHeight = 1.4f;
    bool lineHeightFactor = true;

    // 0 left, 1 center, 2 right.
    int textAlign = 0;

    bool hasBorder = false;
    Rgba borderColor{0.2f, 0.2f, 0.22f, 1.0f};
    float borderWidth = 1.0f;
};

struct Declaration {
    std::string property;
    std::string value;
};

/// 解析 #RGB/#RRGGBB/#RRGGBBAA、rgb()/rgba() 和少量命名颜色。
bool parseColor(const std::string& text, Rgba& color);

/// 解析长度，支持 px、rpx 和 %。
bool parseLength(const std::string& text, float rpxScale, float& value,
                 bool* percent = nullptr);

void applyDeclaration(Style& style, const std::string& property,
                      const std::string& value, float rpxScale);
void applyDeclarations(Style& style, const std::vector<Declaration>& declarations,
                       float rpxScale);

/// 只把可继承的文本属性复制给子节点。
Style inheritStyle(const Style& parent);

float effectiveLineHeight(const Style& style);

}  // namespace miniapp
