//
// miniapp: 极简 WXML 解析器实现。
//

#include "miniapp/wxml_parser.hpp"

#include <cctype>
#include <utility>

namespace miniapp {

namespace {

std::string decodeEntities(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            result.push_back(text[i]);
            continue;
        }
        if (text.compare(i, 5, "&amp;") == 0) {
            result.push_back('&');
            i += 4;
        } else if (text.compare(i, 4, "&lt;") == 0) {
            result.push_back('<');
            i += 3;
        } else if (text.compare(i, 4, "&gt;") == 0) {
            result.push_back('>');
            i += 3;
        } else if (text.compare(i, 6, "&quot;") == 0) {
            result.push_back('"');
            i += 5;
        } else if (text.compare(i, 6, "&apos;") == 0) {
            result.push_back('\'');
            i += 5;
        } else if (text.compare(i, 6, "&#39;") == 0) {
            result.push_back('\'');
            i += 5;
        } else if (text.compare(i, 6, "&nbsp;") == 0) {
            result.push_back(' ');
            i += 5;
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

class Parser {
public:
    explicit Parser(const std::string& source) : source(source), pos(0) {}

    bool parse(WxmlNode& root, std::string& error) {
        root.tag = "#root";
        if (!parseChildren(root)) {
            error = errorMessage;
            return false;
        }
        return true;
    }

private:
    const std::string& source;
    size_t pos;
    std::string errorMessage;

    void fail(const std::string& message) {
        if (errorMessage.empty()) {
            errorMessage = message + " at offset " + std::to_string(pos);
        }
    }

    bool startsWith(const char* text) const {
        return source.compare(pos, std::char_traits<char>::length(text), text) == 0;
    }

    void skipUntil(const char* terminator) {
        size_t end = source.find(terminator, pos);
        pos = end == std::string::npos ? source.size() : end + std::char_traits<char>::length(terminator);
    }

    bool parseChildren(WxmlNode& parent) {
        while (pos < source.size()) {
            if (source[pos] == '<') {
                if (startsWith("<!--")) {
                    skipUntil("-->");
                    continue;
                }
                if (startsWith("</")) {
                    // 结束标签由调用者消费，忽略标签名不匹配。
                    size_t end = source.find('>', pos);
                    pos = end == std::string::npos ? source.size() : end + 1;
                    return true;
                }

                WxmlNode node;
                bool selfClosing = false;
                if (!parseTag(node, selfClosing)) return false;
                parent.children.push_back(std::move(node));
                if (!selfClosing && !parseChildren(parent.children.back())) return false;
                continue;
            }

            size_t next = source.find('<', pos);
            if (next == std::string::npos) next = source.size();
            std::string text = decodeEntities(source.substr(pos, next - pos));
            pos = next;
            bool onlySpace = true;
            for (char c : text) {
                if (!isSpace(c)) {
                    onlySpace = false;
                    break;
                }
            }
            if (!onlySpace) {
                WxmlNode node;
                node.tag = "#text";
                node.text = std::move(text);
                parent.children.push_back(std::move(node));
            }
        }
        return true;
    }

    bool parseTag(WxmlNode& node, bool& selfClosing) {
        ++pos;  // '<'
        std::string name;
        while (pos < source.size() && !isSpace(source[pos]) &&
               source[pos] != '>' && source[pos] != '/') {
            name.push_back(source[pos++]);
        }
        if (name.empty()) {
            fail("empty tag name");
            return false;
        }
        node.tag = name;

        while (pos < source.size()) {
            while (pos < source.size() && isSpace(source[pos])) ++pos;
            if (pos >= source.size()) {
                fail("unterminated tag");
                return false;
            }
            if (source[pos] == '/') {
                if (pos + 1 < source.size() && source[pos + 1] == '>') {
                    pos += 2;
                    selfClosing = true;
                    return true;
                }
                ++pos;
                continue;
            }
            if (source[pos] == '>') {
                ++pos;
                return true;
            }

            std::string attrName;
            while (pos < source.size() && !isSpace(source[pos]) &&
                   source[pos] != '=' && source[pos] != '>' && source[pos] != '/') {
                attrName.push_back(source[pos++]);
            }
            if (attrName.empty()) {
                fail("invalid attribute");
                return false;
            }
            while (pos < source.size() && isSpace(source[pos])) ++pos;

            std::string attrValue;
            if (pos < source.size() && source[pos] == '=') {
                ++pos;
                while (pos < source.size() && isSpace(source[pos])) ++pos;
                if (pos >= source.size()) {
                    fail("missing attribute value");
                    return false;
                }
                char quote = 0;
                if (source[pos] == '"' || source[pos] == '\'') {
                    quote = source[pos++];
                }
                while (pos < source.size() && source[pos] != quote &&
                       (quote != 0 || (!isSpace(source[pos]) && source[pos] != '>' && source[pos] != '/'))) {
                    attrValue.push_back(source[pos++]);
                }
                if (quote != 0) {
                    if (pos >= source.size() || source[pos] != quote) {
                        fail("unterminated attribute value");
                        return false;
                    }
                    ++pos;
                }
            }
            node.attributes[attrName] = decodeEntities(attrValue);
        }
        fail("unterminated tag");
        return false;
    }
};

}  // namespace

WxmlNode parseWxml(const std::string& source, std::string* error) {
    Parser parser(source);
    WxmlNode root;
    std::string message;
    if (!parser.parse(root, message)) {
        if (error) *error = message;
        return WxmlNode{};
    }
    return root;
}

}  // namespace miniapp
