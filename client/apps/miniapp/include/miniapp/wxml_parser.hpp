//
// miniapp: 极简 WXML 解析器（元素、属性、自闭合标签、文本节点）。
//

#pragma once

#include <map>
#include <string>
#include <vector>

namespace miniapp {

struct WxmlNode {
    std::string tag;
    std::map<std::string, std::string> attributes;
    std::string text;  // 仅 #text 节点使用
    std::vector<WxmlNode> children;

    const std::string* attr(const std::string& name) const {
        auto it = attributes.find(name);
        return it == attributes.end() ? nullptr : &it->second;
    }

    bool isText() const { return tag == "#text"; }
};

/// 解析 WXML；失败时返回空根节点并写入 error。
WxmlNode parseWxml(const std::string& source, std::string* error = nullptr);

}  // namespace miniapp
