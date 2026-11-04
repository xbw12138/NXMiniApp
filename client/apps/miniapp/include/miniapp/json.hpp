//
// miniapp: 极简 JSON 值与小解析器（不做第三方依赖）。
//

#pragma once

#include <map>
#include <string>
#include <vector>

namespace miniapp {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;

    static JsonValue boolean(bool value);
    static JsonValue number(double value);
    static JsonValue string(std::string value);
    static JsonValue array(std::vector<JsonValue> values = {});
    static JsonValue object(std::map<std::string, JsonValue> values = {});

    Type type() const { return valueType; }

    bool isNull() const { return valueType == Type::Null; }
    bool isBool() const { return valueType == Type::Bool; }
    bool isNumber() const { return valueType == Type::Number; }
    bool isString() const { return valueType == Type::String; }
    bool isArray() const { return valueType == Type::Array; }
    bool isObject() const { return valueType == Type::Object; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    const std::string& asString() const;
    const std::vector<JsonValue>& asArray() const;
    const std::map<std::string, JsonValue>& asObject() const;

    size_t size() const;
    const JsonValue& at(size_t index) const;
    const JsonValue* find(const std::string& key) const;

    /// 以对象方式写入成员；非对象值会先转换成空对象。
    void setMember(const std::string& key, JsonValue value);

private:
    Type valueType = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;
};

/// 解析 JSON 文本；失败时返回 Null 并把错误写到 error。
JsonValue parseJson(const std::string& source, std::string* error = nullptr);

/// 序列化为紧凑 JSON 文本。
std::string toJsonString(const JsonValue& value);

}  // namespace miniapp
