//
// miniapp: 极简 WXSS 解析与选择器匹配。
//

#pragma once

#include <string>
#include <vector>

#include "miniapp/style.hpp"

namespace miniapp {

/// 一个元素的标签、id、class，供选择器匹配使用。
struct SelectorNode {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
};

struct WxssRule {
    std::vector<std::string> selectors;  // 空格分隔的后代选择器
    std::vector<Declaration> declarations;
};

class WxssStylesheet {
public:
    static WxssStylesheet parse(const std::string& source,
                                std::string* error = nullptr);

    /// 按源码顺序应用命中的规则。
    void apply(const SelectorNode& node,
               const std::vector<SelectorNode>& ancestors, Style& style,
               float rpxScale) const;

    bool empty() const { return rules.empty(); }

private:
    std::vector<WxssRule> rules;
};

/// 解析内联 style 属性中的声明列表。
std::vector<Declaration> parseDeclarations(const std::string& source);

}  // namespace miniapp
