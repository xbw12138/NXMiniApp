//
// miniapp: 样式值解析实现。
//

#include "miniapp/style.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace miniapp {

namespace {

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool splitComma(const std::string& body, std::vector<std::string>& parts) {
    std::string current;
    for (char c : body) {
        if (c == ',') {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(trim(current));
    return !parts.empty();
}

float componentValue(const std::string& text, float maxValue) {
    std::string value = trim(text);
    if (!value.empty() && value.back() == '%') {
        float percent = std::atof(value.substr(0, value.size() - 1).c_str());
        return maxValue * percent / 100.0f;
    }
    return std::atof(value.c_str());
}

bool parseColorToken(const std::string& token, Rgba& color) {
    std::string text = lower(trim(token));
    if (text.empty()) return false;

    if (text == "transparent") {
        color = Rgba{0.0f, 0.0f, 0.0f, 0.0f};
        return true;
    }
    if (text == "white") {
        color = Rgba{1.0f, 1.0f, 1.0f, 1.0f};
        return true;
    }
    if (text == "black") {
        color = Rgba{0.0f, 0.0f, 0.0f, 1.0f};
        return true;
    }
    if (text == "red") {
        color = Rgba{1.0f, 0.0f, 0.0f, 1.0f};
        return true;
    }
    if (text == "green") {
        color = Rgba{0.0f, 0.8f, 0.0f, 1.0f};
        return true;
    }
    if (text == "blue") {
        color = Rgba{0.0f, 0.0f, 1.0f, 1.0f};
        return true;
    }
    if (text == "gray" || text == "grey") {
        color = Rgba{0.5f, 0.5f, 0.5f, 1.0f};
        return true;
    }
    if (text == "orange") {
        color = Rgba{1.0f, 0.6f, 0.0f, 1.0f};
        return true;
    }
    if (text == "yellow") {
        color = Rgba{1.0f, 0.9f, 0.1f, 1.0f};
        return true;
    }
    if (text == "purple") {
        color = Rgba{0.6f, 0.3f, 0.85f, 1.0f};
        return true;
    }
    if (text == "pink") {
        color = Rgba{1.0f, 0.45f, 0.7f, 1.0f};
        return true;
    }
    if (text == "cyan") {
        color = Rgba{0.1f, 0.85f, 0.9f, 1.0f};
        return true;
    }
    if (text == "teal") {
        color = Rgba{0.0f, 0.6f, 0.55f, 1.0f};
        return true;
    }
    if (text == "brown") {
        color = Rgba{0.55f, 0.35f, 0.2f, 1.0f};
        return true;
    }
    if (text == "lightgray" || text == "lightgrey") {
        color = Rgba{0.82f, 0.82f, 0.84f, 1.0f};
        return true;
    }
    if (text == "darkgray" || text == "darkgrey") {
        color = Rgba{0.25f, 0.25f, 0.27f, 1.0f};
        return true;
    }

    if (text[0] == '#') {
        std::string hex = text.substr(1);
        if (hex.size() == 3 || hex.size() == 4) {
            std::string expanded;
            for (char c : hex) {
                expanded.push_back(c);
                expanded.push_back(c);
            }
            hex = expanded;
        }
        if (hex.size() != 6 && hex.size() != 8) return false;
        int values[4] = {255, 255, 255, 255};
        for (size_t i = 0; i < hex.size() / 2; ++i) {
            int high = hexDigit(hex[i * 2]);
            int low = hexDigit(hex[i * 2 + 1]);
            if (high < 0 || low < 0) return false;
            values[i] = high * 16 + low;
        }
        color = Rgba{values[0] / 255.0f, values[1] / 255.0f,
                     values[2] / 255.0f, values[3] / 255.0f};
        return true;
    }

    if (text.rfind("rgb(", 0) == 0 || text.rfind("rgba(", 0) == 0) {
        size_t open = text.find('(');
        size_t close = text.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) {
            return false;
        }
        std::vector<std::string> parts;
        splitComma(text.substr(open + 1, close - open - 1), parts);
        if (parts.size() < 3) return false;
        color.r = componentValue(parts[0], 255.0f) / 255.0f;
        color.g = componentValue(parts[1], 255.0f) / 255.0f;
        color.b = componentValue(parts[2], 255.0f) / 255.0f;
        color.a = parts.size() >= 4 ? componentValue(parts[3], 1.0f) : 1.0f;
        color.r = std::min(1.0f, std::max(0.0f, color.r));
        color.g = std::min(1.0f, std::max(0.0f, color.g));
        color.b = std::min(1.0f, std::max(0.0f, color.b));
        color.a = std::min(1.0f, std::max(0.0f, color.a));
        return true;
    }
    return false;
}

std::vector<std::string> splitTokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

void applyEdges(Edges& edges, const std::vector<std::string>& values, float rpxScale) {
    float parsed[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    size_t count = std::min<size_t>(values.size(), 4);
    for (size_t i = 0; i < count; ++i) {
        parseLength(values[i], rpxScale, parsed[i]);
    }
    if (count == 1) {
        edges.top = edges.right = edges.bottom = edges.left = parsed[0];
    } else if (count == 2) {
        edges.top = edges.bottom = parsed[0];
        edges.right = edges.left = parsed[1];
    } else if (count == 3) {
        edges.top = parsed[0];
        edges.right = edges.left = parsed[1];
        edges.bottom = parsed[2];
    } else if (count == 4) {
        edges.top = parsed[0];
        edges.right = parsed[1];
        edges.bottom = parsed[2];
        edges.left = parsed[3];
    }
}

void applyEdgeSide(float& target, const std::string& value, float rpxScale) {
    float parsed = 0.0f;
    if (parseLength(value, rpxScale, parsed)) target = parsed;
}

int parseJustify(const std::string& value) {
    if (value == "center") return 1;
    if (value == "flex-end" || value == "end") return 2;
    if (value == "space-between") return 3;
    if (value == "space-around" || value == "space-evenly") return 4;
    return 0;
}

int parseAlign(const std::string& value) {
    if (value == "flex-start" || value == "start") return 1;
    if (value == "center") return 2;
    if (value == "flex-end" || value == "end") return 3;
    return 0;
}

float parseFontSize(const std::string& value, float rpxScale) {
    float parsed = 28.0f;
    parseLength(value, rpxScale, parsed);
    return parsed;
}

void applyShadow(Style& style, const std::vector<std::string>& tokens, float rpxScale) {
    if (tokens.size() == 1 && lower(trim(tokens[0])) == "none") {
        style.hasShadow = false;
        return;
    }
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int count = 0;
    Rgba color{0.0f, 0.0f, 0.0f, 0.45f};
    for (const auto& token : tokens) {
        Rgba parsedColor;
        if (parseColorToken(token, parsedColor)) {
            color = parsedColor;
            continue;
        }
        float parsed = 0.0f;
        if (count < 4 && parseLength(token, rpxScale, parsed)) {
            values[count++] = parsed;
        }
    }
    if (count == 0) return;
    style.hasShadow = true;
    style.shadowX = values[0];
    style.shadowY = values[1];
    style.shadowBlur = values[2];
    style.shadowColor = color;
}

}  // namespace

bool parseColor(const std::string& text, Rgba& color) {
    return parseColorToken(text, color);
}

bool parseLength(const std::string& text, float rpxScale, float& value,
                 bool* percent) {
    std::string token = lower(trim(text));
    if (percent) *percent = false;
    if (token.empty()) return false;

    bool isPercent = false;
    float number = 0.0f;
    if (token.size() > 3 && token.substr(token.size() - 3) == "rpx") {
        number = static_cast<float>(std::atof(token.substr(0, token.size() - 3).c_str()));
        number *= rpxScale;
    } else if (token.size() > 2 && token.substr(token.size() - 2) == "px") {
        number = static_cast<float>(std::atof(token.substr(0, token.size() - 2).c_str()));
    } else if (token.size() > 1 && token.back() == '%') {
        number = static_cast<float>(std::atof(token.substr(0, token.size() - 1).c_str()));
        isPercent = true;
    } else {
        char* end = nullptr;
        number = std::strtof(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0') return false;
    }
    value = number;
    if (percent) *percent = isPercent;
    return true;
}

void applyDeclaration(Style& style, const std::string& property,
                      const std::string& value, float rpxScale) {
    std::string prop = lower(trim(property));
    std::string rawValue = trim(value);
    std::string val = lower(rawValue);

    if (prop == "background-color" || prop == "background") {
        Rgba color;
        if (parseColorToken(rawValue, color)) {
            style.hasBackground = true;
            style.background = color;
        }
    } else if (prop == "color") {
        Rgba color;
        if (parseColorToken(rawValue, color)) {
            style.hasColor = true;
            style.color = color;
        }
    } else if (prop == "font-size") {
        style.fontSize = parseFontSize(val, rpxScale);
    } else if (prop == "font-weight") {
        if (val == "bold" || val == "bolder") {
            style.bold = true;
        } else {
            int weight = std::atoi(val.c_str());
            style.bold = weight >= 600;
        }
    } else if (prop == "padding") {
        applyEdges(style.padding, splitTokens(val), rpxScale);
    } else if (prop == "padding-top") {
        applyEdgeSide(style.padding.top, val, rpxScale);
    } else if (prop == "padding-right") {
        applyEdgeSide(style.padding.right, val, rpxScale);
    } else if (prop == "padding-bottom") {
        applyEdgeSide(style.padding.bottom, val, rpxScale);
    } else if (prop == "padding-left") {
        applyEdgeSide(style.padding.left, val, rpxScale);
    } else if (prop == "margin") {
        applyEdges(style.margin, splitTokens(val), rpxScale);
    } else if (prop == "margin-top") {
        applyEdgeSide(style.margin.top, val, rpxScale);
    } else if (prop == "margin-right") {
        applyEdgeSide(style.margin.right, val, rpxScale);
    } else if (prop == "margin-bottom") {
        applyEdgeSide(style.margin.bottom, val, rpxScale);
    } else if (prop == "margin-left") {
        applyEdgeSide(style.margin.left, val, rpxScale);
    } else if (prop == "width" || prop == "height") {
        float parsed = 0.0f;
        bool isPercent = false;
        if (parseLength(val, rpxScale, parsed, &isPercent)) {
            if (prop == "width") {
                style.hasWidth = true;
                style.width = parsed;
                style.widthPercent = isPercent;
            } else {
                style.hasHeight = true;
                style.height = parsed;
                style.heightPercent = isPercent;
            }
        }
    } else if (prop == "border-radius") {
        float parsed = 0.0f;
        if (parseLength(val, rpxScale, parsed)) style.borderRadius = parsed;
    } else if (prop == "flex-direction") {
        style.flexDirection = val == "row" ? 1 : 0;
    } else if (prop == "justify-content") {
        style.justifyContent = parseJustify(val);
    } else if (prop == "align-items") {
        style.alignItems = parseAlign(val);
    } else if (prop == "gap" || prop == "column-gap") {
        float parsed = 0.0f;
        if (parseLength(val, rpxScale, parsed)) style.gap = parsed;
    } else if (prop == "line-height") {
        std::string number = val;
        bool relative = number.find_first_not_of("0123456789.") == std::string::npos;
        if (relative) {
            style.hasLineHeight = true;
            style.lineHeightFactor = true;
            style.lineHeight = static_cast<float>(std::atof(number.c_str()));
        } else {
            float parsed = 0.0f;
            if (parseLength(val, rpxScale, parsed)) {
                style.hasLineHeight = true;
                style.lineHeightFactor = false;
                style.lineHeight = parsed;
            }
        }
    } else if (prop == "text-align") {
        if (val == "center") {
            style.textAlign = 1;
        } else if (val == "right") {
            style.textAlign = 2;
        } else {
            style.textAlign = 0;
        }
    } else if (prop == "border") {
        for (const auto& token : splitTokens(val)) {
            Rgba color;
            if (parseColorToken(token, color)) {
                style.hasBorder = true;
                style.borderColor = color;
                continue;
            }
            float parsed = 0.0f;
            if (parseLength(token, rpxScale, parsed)) {
                style.hasBorder = true;
                style.borderWidth = parsed;
            }
        }
    } else if (prop == "border-color") {
        Rgba color;
        if (parseColorToken(rawValue, color)) {
            style.hasBorder = true;
            style.borderColor = color;
        }
    } else if (prop == "border-width") {
        float parsed = 0.0f;
        if (parseLength(val, rpxScale, parsed)) {
            style.hasBorder = true;
            style.borderWidth = parsed;
        }
    } else if (prop == "opacity") {
        float parsed = 1.0f;
        if (parseLength(val, rpxScale, parsed)) {
            style.opacity = std::min(1.0f, std::max(0.0f, parsed));
        }
    } else if (prop == "display") {
        style.displayNone = (val == "none");
    } else if (prop == "overflow" || prop == "overflow-x" || prop == "overflow-y") {
        style.overflowHidden = (val == "hidden" || val == "scroll" || val == "auto");
    } else if (prop == "position") {
        style.absolute = (val == "absolute" || val == "fixed");
    } else if (prop == "top" || prop == "right" || prop == "bottom" || prop == "left") {
        float parsed = 0.0f;
        if (parseLength(val, rpxScale, parsed)) {
            if (prop == "top") {
                style.hasTop = true;
                style.top = parsed;
            } else if (prop == "right") {
                style.hasRight = true;
                style.right = parsed;
            } else if (prop == "bottom") {
                style.hasBottom = true;
                style.bottom = parsed;
            } else {
                style.hasLeft = true;
                style.left = parsed;
            }
        }
    } else if (prop == "align-self") {
        style.alignSelf = parseAlign(val);
    } else if (prop == "flex-grow") {
        style.flexGrow = static_cast<float>(std::atof(val.c_str()));
    } else if (prop == "flex-shrink") {
        style.flexShrink = static_cast<float>(std::atof(val.c_str()));
    } else if (prop == "flex-basis") {
        float parsed = 0.0f;
        bool isPercent = false;
        if (parseLength(val, rpxScale, parsed, &isPercent)) {
            style.hasFlexBasis = true;
            style.flexBasis = parsed;
            style.flexBasisPercent = isPercent;
        }
    } else if (prop == "flex") {
        auto tokens = splitTokens(val);
        if (!tokens.empty() && tokens[0] != "none" && tokens[0] != "auto") {
            style.flexGrow = static_cast<float>(std::atof(tokens[0].c_str()));
            style.flexShrink = tokens.size() > 1
                                   ? static_cast<float>(std::atof(tokens[1].c_str()))
                                   : 1.0f;
            style.hasFlexBasis = true;
            style.flexBasis = 0.0f;
            style.flexBasisPercent = false;
            if (tokens.size() > 2) {
                bool isPercent = false;
                float parsed = 0.0f;
                if (parseLength(tokens[2], rpxScale, parsed, &isPercent)) {
                    style.flexBasis = parsed;
                    style.flexBasisPercent = isPercent;
                }
            }
        }
    } else if (prop == "min-width" || prop == "min-height" || prop == "max-width" ||
               prop == "max-height") {
        float parsed = 0.0f;
        bool isPercent = false;
        if (parseLength(val, rpxScale, parsed, &isPercent)) {
            if (prop == "min-width") {
                style.hasMinWidth = true;
                style.minWidth = parsed;
                style.minWidthPercent = isPercent;
            } else if (prop == "min-height") {
                style.hasMinHeight = true;
                style.minHeight = parsed;
                style.minHeightPercent = isPercent;
            } else if (prop == "max-width") {
                style.hasMaxWidth = true;
                style.maxWidth = parsed;
                style.maxWidthPercent = isPercent;
            } else {
                style.hasMaxHeight = true;
                style.maxHeight = parsed;
                style.maxHeightPercent = isPercent;
            }
        }
    } else if (prop == "box-shadow") {
        applyShadow(style, splitTokens(rawValue), rpxScale);
    }
}

void applyDeclarations(Style& style, const std::vector<Declaration>& declarations,
                       float rpxScale) {
    for (const auto& declaration : declarations) {
        applyDeclaration(style, declaration.property, declaration.value, rpxScale);
    }
}

Style inheritStyle(const Style& parent) {
    Style style;
    style.hasColor = parent.hasColor;
    style.color = parent.color;
    style.fontSize = parent.fontSize;
    style.bold = parent.bold;
    style.hasLineHeight = parent.hasLineHeight;
    style.lineHeight = parent.lineHeight;
    style.lineHeightFactor = parent.lineHeightFactor;
    style.textAlign = parent.textAlign;
    return style;
}

float effectiveLineHeight(const Style& style) {
    if (style.hasLineHeight) {
        return style.lineHeightFactor ? style.fontSize * style.lineHeight : style.lineHeight;
    }
    return style.fontSize * 1.4f;
}

}  // namespace miniapp
