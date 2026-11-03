//
// miniapp: 极简 JSON 解析器实现。
//

#include "miniapp/json.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace miniapp {

namespace {

const std::string kEmptyString;
const std::vector<JsonValue> kEmptyArray;
const std::map<std::string, JsonValue> kEmptyObject;
const JsonValue kNullValue;

void appendUtf8(std::string& out, unsigned codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(const std::string& text) : source(text), pos(0) {}

    JsonValue parse(std::string* error) {
        JsonValue result;
        skipWhitespace();
        if (!parseValue(result)) {
            if (error) *error = errorMessage;
            return JsonValue();
        }
        skipWhitespace();
        if (pos != source.size()) {
            fail("trailing characters");
            if (error) *error = errorMessage;
            return JsonValue();
        }
        return result;
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

    void skipWhitespace() {
        while (pos < source.size() &&
               (source[pos] == ' ' || source[pos] == '\t' ||
                source[pos] == '\r' || source[pos] == '\n')) {
            ++pos;
        }
    }

    bool consume(char expected) {
        if (pos < source.size() && source[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    bool parseValue(JsonValue& out) {
        if (pos >= source.size()) {
            fail("unexpected end of input");
            return false;
        }
        char c = source[pos];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string value;
            if (!parseString(value)) return false;
            out = JsonValue::string(std::move(value));
            return true;
        }
        if (c == 't' && source.compare(pos, 4, "true") == 0) {
            pos += 4;
            out = JsonValue::boolean(true);
            return true;
        }
        if (c == 'f' && source.compare(pos, 5, "false") == 0) {
            pos += 5;
            out = JsonValue::boolean(false);
            return true;
        }
        if (c == 'n' && source.compare(pos, 4, "null") == 0) {
            pos += 4;
            out = JsonValue();
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber(out);
        }
        fail("unexpected character");
        return false;
    }

    bool parseObject(JsonValue& out) {
        if (!consume('{')) return false;
        skipWhitespace();
        std::map<std::string, JsonValue> fields;
        if (consume('}')) {
            out = JsonValue::object(std::move(fields));
            return true;
        }
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) {
                fail("expected ':'");
                return false;
            }
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value)) return false;
            fields[key] = std::move(value);
            skipWhitespace();
            if (consume('}')) break;
            if (!consume(',')) {
                fail("expected ',' or '}'");
                return false;
            }
        }
        out = JsonValue::object(std::move(fields));
        return true;
    }

    bool parseArray(JsonValue& out) {
        if (!consume('[')) return false;
        skipWhitespace();
        std::vector<JsonValue> values;
        if (consume(']')) {
            out = JsonValue::array(std::move(values));
            return true;
        }
        while (true) {
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value)) return false;
            values.push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) break;
            if (!consume(',')) {
                fail("expected ',' or ']'");
                return false;
            }
        }
        out = JsonValue::array(std::move(values));
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            fail("expected string");
            return false;
        }
        out.clear();
        while (pos < source.size()) {
            char c = source[pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos >= source.size()) {
                fail("unterminated escape");
                return false;
            }
            char escaped = source[pos++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned codepoint = 0;
                    if (!parseHex4(codepoint)) return false;
                    // 处理代理对。
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                        pos + 1 < source.size() && source[pos] == '\\' &&
                        source[pos + 1] == 'u') {
                        pos += 2;
                        unsigned low = 0;
                        if (!parseHex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                                        (low - 0xDC00);
                        }
                    }
                    appendUtf8(out, codepoint);
                    break;
                }
                default:
                    fail("invalid escape");
                    return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    bool parseHex4(unsigned& value) {
        if (pos + 4 > source.size()) {
            fail("invalid unicode escape");
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = source[pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value += static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value += static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value += static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("invalid unicode escape");
                return false;
            }
        }
        return true;
    }

    bool parseNumber(JsonValue& out) {
        size_t start = pos;
        if (source[pos] == '-') ++pos;
        while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
        if (pos < source.size() && source[pos] == '.') {
            ++pos;
            while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
        }
        if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
            ++pos;
            if (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) ++pos;
            while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
        }
        std::string token = source.substr(start, pos - start);
        errno = 0;
        char* end = nullptr;
        double value = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0' || errno == ERANGE) {
            fail("invalid number");
            return false;
        }
        out = JsonValue::number(value);
        return true;
    }
};

}  // namespace

JsonValue JsonValue::boolean(bool value) {
    JsonValue result;
    result.valueType = Type::Bool;
    result.boolValue = value;
    return result;
}

JsonValue JsonValue::number(double value) {
    JsonValue result;
    result.valueType = Type::Number;
    result.numberValue = value;
    return result;
}

JsonValue JsonValue::string(std::string value) {
    JsonValue result;
    result.valueType = Type::String;
    result.stringValue = std::move(value);
    return result;
}

JsonValue JsonValue::array(std::vector<JsonValue> values) {
    JsonValue result;
    result.valueType = Type::Array;
    result.arrayValue = std::move(values);
    return result;
}

JsonValue JsonValue::object(std::map<std::string, JsonValue> values) {
    JsonValue result;
    result.valueType = Type::Object;
    result.objectValue = std::move(values);
    return result;
}

bool JsonValue::asBool(bool fallback) const {
    if (valueType == Type::Bool) return boolValue;
    if (valueType == Type::Number) return numberValue != 0.0;
    return fallback;
}

double JsonValue::asNumber(double fallback) const {
    return valueType == Type::Number ? numberValue : fallback;
}

const std::string& JsonValue::asString() const {
    return valueType == Type::String ? stringValue : kEmptyString;
}

const std::vector<JsonValue>& JsonValue::asArray() const {
    return valueType == Type::Array ? arrayValue : kEmptyArray;
}

const std::map<std::string, JsonValue>& JsonValue::asObject() const {
    return valueType == Type::Object ? objectValue : kEmptyObject;
}

size_t JsonValue::size() const {
    if (valueType == Type::Array) return arrayValue.size();
    if (valueType == Type::Object) return objectValue.size();
    return 0;
}

const JsonValue& JsonValue::at(size_t index) const {
    if (valueType == Type::Array && index < arrayValue.size()) return arrayValue[index];
    return kNullValue;
}

const JsonValue* JsonValue::find(const std::string& key) const {
    if (valueType != Type::Object) return nullptr;
    auto it = objectValue.find(key);
    return it == objectValue.end() ? nullptr : &it->second;
}

JsonValue parseJson(const std::string& source, std::string* error) {
    Parser parser(source);
    return parser.parse(error);
}

}  // namespace miniapp
