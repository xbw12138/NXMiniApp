//
// miniapp: 极简 WXSS 解析实现。
//

#include "miniapp/wxss.hpp"

#include <cctype>
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

std::string stripComments(const std::string& source) {
    std::string result;
    result.reserve(source.size());
    size_t pos = 0;
    while (pos < source.size()) {
        if (source.compare(pos, 2, "/*") == 0) {
            size_t end = source.find("*/", pos + 2);
            pos = end == std::string::npos ? source.size() : end + 2;
            continue;
        }
        result.push_back(source[pos++]);
    }
    return result;
}

bool simpleMatches(const std::string& selector, const SelectorNode& node) {
    if (selector.empty()) return false;
    if (selector[0] == '.') {
        std::string name = selector.substr(1);
        for (const auto& cls : node.classes) {
            if (cls == name) return true;
        }
        return false;
    }
    if (selector[0] == '#') {
        return node.id == selector.substr(1);
    }
    return node.tag == selector;
}

bool selectorMatches(const WxssRule& rule, const SelectorNode& node,
                     const std::vector<SelectorNode>& ancestors) {
    if (rule.selectors.empty()) return false;
    if (!simpleMatches(rule.selectors.back(), node)) return false;

    size_t ancestorIndex = ancestors.size();
    for (size_t i = rule.selectors.size() - 1; i > 0; --i) {
        bool found = false;
        while (ancestorIndex > 0) {
            --ancestorIndex;
            if (simpleMatches(rule.selectors[i - 1], ancestors[ancestorIndex])) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

}  // namespace

std::vector<Declaration> parseDeclarations(const std::string& source) {
    std::vector<Declaration> declarations;
    std::stringstream stream(source);
    std::string part;
    while (std::getline(stream, part, ';')) {
        size_t colon = part.find(':');
        if (colon == std::string::npos) continue;
        Declaration declaration;
        declaration.property = trim(part.substr(0, colon));
        declaration.value = trim(part.substr(colon + 1));
        size_t important = declaration.value.find("!important");
        if (important != std::string::npos) {
            declaration.value = trim(declaration.value.substr(0, important));
        }
        if (!declaration.property.empty() && !declaration.value.empty()) {
            declarations.push_back(std::move(declaration));
        }
    }
    return declarations;
}

WxssStylesheet WxssStylesheet::parse(const std::string& source,
                                     std::string* error) {
    WxssStylesheet sheet;
    std::string text = stripComments(source);
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[pos])) || text[pos] == '}')) {
            ++pos;
        }
        if (pos >= text.size()) break;

        size_t open = text.find('{', pos);
        if (open == std::string::npos) {
            if (error) *error = "missing '{' in WXSS";
            break;
        }
        std::string selectorText = trim(text.substr(pos, open - pos));
        size_t close = text.find('}', open + 1);
        if (close == std::string::npos) {
            if (error) *error = "missing '}' in WXSS";
            break;
        }
        std::string body = text.substr(open + 1, close - open - 1);
        pos = close + 1;

        // 暂不支持 @media 等块，直接跳过。
        if (!selectorText.empty() && selectorText[0] == '@') continue;

        WxssRule rule;
        std::istringstream selectorStream(selectorText);
        std::string simple;
        while (selectorStream >> simple) {
            rule.selectors.push_back(simple);
        }
        if (rule.selectors.empty()) continue;
        rule.declarations = parseDeclarations(body);
        if (!rule.declarations.empty()) sheet.rules.push_back(std::move(rule));
    }
    return sheet;
}

void WxssStylesheet::apply(const SelectorNode& node,
                           const std::vector<SelectorNode>& ancestors,
                           Style& style, float rpxScale) const {
    for (const auto& rule : rules) {
        if (selectorMatches(rule, node, ancestors)) {
            applyDeclarations(style, rule.declarations, rpxScale);
        }
    }
}

}  // namespace miniapp
