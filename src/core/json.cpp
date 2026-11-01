#include "core/json.h"

#include <charconv>
#include <cmath>

namespace velora::json {

namespace {

const std::string kEmptyString;
const Array kEmptyArray;
const Object kEmptyObject;

constexpr int kMaxDepth = 64;

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void DumpString(std::string& out, const std::string& s) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    std::optional<Value> ParseDocument() {
        auto v = ParseValue(0);
        if (!v) return std::nullopt;
        SkipWs();
        if (pos_ != t_.size()) return std::nullopt;
        return v;
    }

private:
    void SkipWs() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_;
    }
    bool Eat(char c) {
        SkipWs();
        if (pos_ < t_.size() && t_[pos_] == c) { ++pos_; return true; }
        return false;
    }
    bool EatWord(std::string_view w) {
        if (t_.substr(pos_, w.size()) == w) { pos_ += w.size(); return true; }
        return false;
    }

    std::optional<Value> ParseValue(int depth) {
        if (depth > kMaxDepth) return std::nullopt;
        SkipWs();
        if (pos_ >= t_.size()) return std::nullopt;
        switch (t_[pos_]) {
            case '{': return ParseObject(depth);
            case '[': return ParseArray(depth);
            case '"': {
                auto s = ParseString();
                if (!s) return std::nullopt;
                return Value(std::move(*s));
            }
            case 't': if (EatWord("true")) return Value(true); return std::nullopt;
            case 'f': if (EatWord("false")) return Value(false); return std::nullopt;
            case 'n': if (EatWord("null")) return Value(); return std::nullopt;
            default: return ParseNumber();
        }
    }

    std::optional<Value> ParseObject(int depth) {
        ++pos_;  // {
        Object obj;
        if (Eat('}')) return Value(std::move(obj));
        for (;;) {
            SkipWs();
            if (pos_ >= t_.size() || t_[pos_] != '"') return std::nullopt;
            auto key = ParseString();
            if (!key || !Eat(':')) return std::nullopt;
            auto val = ParseValue(depth + 1);
            if (!val) return std::nullopt;
            obj.emplace_back(std::move(*key), std::move(*val));
            if (Eat(',')) continue;
            if (Eat('}')) return Value(std::move(obj));
            return std::nullopt;
        }
    }

    std::optional<Value> ParseArray(int depth) {
        ++pos_;  // [
        Array arr;
        if (Eat(']')) return Value(std::move(arr));
        for (;;) {
            auto val = ParseValue(depth + 1);
            if (!val) return std::nullopt;
            arr.push_back(std::move(*val));
            if (Eat(',')) continue;
            if (Eat(']')) return Value(std::move(arr));
            return std::nullopt;
        }
    }

    bool ParseHex4(std::uint32_t& out) {
        if (pos_ + 4 > t_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    std::optional<std::string> ParseString() {
        ++pos_;  // opening quote
        std::string out;
        while (pos_ < t_.size()) {
            unsigned char c = static_cast<unsigned char>(t_[pos_++]);
            if (c == '"') return out;
            if (c < 0x20) return std::nullopt;
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ >= t_.size()) return std::nullopt;
            char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp;
                    if (!ParseHex4(cp)) return std::nullopt;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        std::uint32_t lo;
                        if (!EatWord("\\u") || !ParseHex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return std::nullopt;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return std::nullopt;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;  // unterminated
    }

    std::optional<Value> ParseNumber() {
        size_t start = pos_;
        bool isFloat = false;
        if (pos_ < t_.size() && t_[pos_] == '-') ++pos_;
        size_t digits = pos_;
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        if (pos_ == digits) return std::nullopt;
        if (pos_ < t_.size() && t_[pos_] == '.') {
            isFloat = true;
            ++pos_;
            size_t f = pos_;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
            if (pos_ == f) return std::nullopt;
        }
        if (pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-')) ++pos_;
            size_t x = pos_;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
            if (pos_ == x) return std::nullopt;
        }
        const char* b = t_.data() + start;
        const char* e = t_.data() + pos_;
        if (!isFloat) {
            std::int64_t i = 0;
            auto r = std::from_chars(b, e, i);
            if (r.ec == std::errc() && r.ptr == e) return Value(i);
        }
        double d = 0;
        auto r = std::from_chars(b, e, d);
        if (r.ec != std::errc() || r.ptr != e) return std::nullopt;
        return Value(d);
    }

    std::string_view t_;
    size_t pos_ = 0;
};

void DumpValue(std::string& out, const Value& v);

void DumpValue(std::string& out, const Value& v) {
    if (v.IsNull()) { out += "null"; return; }
    if (v.IsString()) { DumpString(out, v.AsString()); return; }
    if (v.IsArray()) {
        out += '[';
        bool first = true;
        for (const auto& e : v.AsArray()) {
            if (!first) out += ',';
            first = false;
            DumpValue(out, e);
        }
        out += ']';
        return;
    }
    if (v.IsObject()) {
        out += '{';
        bool first = true;
        for (const auto& [k, e] : v.AsObject()) {
            if (!first) out += ',';
            first = false;
            DumpString(out, k);
            out += ':';
            DumpValue(out, e);
        }
        out += '}';
        return;
    }
    out += v.Dump();  // bool / number; Dump handles these without recursing back here
}

}  // namespace

bool Value::AsBool(bool def) const { return std::holds_alternative<bool>(v_) ? std::get<bool>(v_) : def; }

std::int64_t Value::AsInt(std::int64_t def) const {
    if (auto* i = std::get_if<std::int64_t>(&v_)) return *i;
    if (auto* d = std::get_if<double>(&v_)) return static_cast<std::int64_t>(*d);
    return def;
}

double Value::AsDouble(double def) const {
    if (auto* d = std::get_if<double>(&v_)) return *d;
    if (auto* i = std::get_if<std::int64_t>(&v_)) return static_cast<double>(*i);
    return def;
}

const std::string& Value::AsString() const {
    auto* s = std::get_if<std::string>(&v_);
    return s ? *s : kEmptyString;
}
const Array& Value::AsArray() const {
    auto* a = std::get_if<Array>(&v_);
    return a ? *a : kEmptyArray;
}
const Object& Value::AsObject() const {
    auto* o = std::get_if<Object>(&v_);
    return o ? *o : kEmptyObject;
}

const Value* Value::Find(std::string_view key) const {
    auto* o = std::get_if<Object>(&v_);
    if (!o) return nullptr;
    for (const auto& [k, v] : *o)
        if (k == key) return &v;
    return nullptr;
}

void Value::Set(std::string key, Value value) {
    if (IsNull()) v_ = Object{};
    auto* o = std::get_if<Object>(&v_);
    if (!o) return;
    for (auto& [k, v] : *o) {
        if (k == key) { v = std::move(value); return; }
    }
    o->emplace_back(std::move(key), std::move(value));
}

std::string Value::Dump() const {
    std::string out;
    if (auto* b = std::get_if<bool>(&v_)) {
        out = *b ? "true" : "false";
    } else if (auto* i = std::get_if<std::int64_t>(&v_)) {
        out = std::to_string(*i);
    } else if (auto* d = std::get_if<double>(&v_)) {
        if (!std::isfinite(*d)) return "null";
        char buf[64];
        auto r = std::to_chars(buf, buf + sizeof buf, *d);
        out.assign(buf, r.ptr);
    } else {
        DumpValue(out, *this);
    }
    return out;
}

std::optional<Value> Parse(std::string_view text) { return Parser(text).ParseDocument(); }

}  // namespace velora::json
