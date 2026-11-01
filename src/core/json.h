// Minimal JSON value, parser and writer for state.json and IPC messages.
// Owner: A. Parser is depth-limited because IPC input is not trusted.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace velora::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;  // insertion order kept

class Value {
public:
    Value() = default;
    Value(bool b) : v_(b) {}
    Value(int i) : v_(static_cast<std::int64_t>(i)) {}
    Value(std::int64_t i) : v_(i) {}
    Value(std::uint32_t i) : v_(static_cast<std::int64_t>(i)) {}
    Value(double d) : v_(d) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(Array a) : v_(std::move(a)) {}
    Value(Object o) : v_(std::move(o)) {}

    bool IsNull() const { return std::holds_alternative<std::monostate>(v_); }
    bool IsObject() const { return std::holds_alternative<Object>(v_); }
    bool IsArray() const { return std::holds_alternative<Array>(v_); }
    bool IsString() const { return std::holds_alternative<std::string>(v_); }

    // Typed reads never throw; a type mismatch yields the default.
    bool AsBool(bool def = false) const;
    std::int64_t AsInt(std::int64_t def = 0) const;
    double AsDouble(double def = 0.0) const;
    const std::string& AsString() const;
    const Array& AsArray() const;
    const Object& AsObject() const;

    const Value* Find(std::string_view key) const;  // objects only
    void Set(std::string key, Value value);         // turns a null into an object

    std::string Dump() const;

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> v_;
};

// Returns nullopt on malformed input, trailing garbage, or nesting over 64.
std::optional<Value> Parse(std::string_view text);

}  // namespace velora::json
