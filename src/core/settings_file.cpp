#include "core/settings_file.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "core/json.h"
#include "core/runtime.h"

namespace velora {

namespace {

const json::Value* Lookup(const json::Value& root, const std::string& key) {
    const json::Value* v = &root;
    size_t pos = 0;
    while (v) {
        const size_t dot = key.find('.', pos);
        v = v->Find(std::string_view(key).substr(pos, dot == std::string::npos ? std::string::npos : dot - pos));
        if (dot == std::string::npos) return v;
        pos = dot + 1;
    }
    return nullptr;
}

std::optional<json::Value> Load() {
    const auto dir = DataDir();
    if (dir.empty()) return std::nullopt;
    std::ifstream f(dir / L"settings.json", std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    auto doc = json::Parse(ss.str());
    if (!doc || !doc->IsObject()) return std::nullopt;
    return doc;
}

}  // namespace

bool SettingBool(const char* key, bool def) {
    auto doc = Load();
    const json::Value* v = doc ? Lookup(*doc, key) : nullptr;
    return v ? v->AsBool(def) : def;
}

std::string SettingString(const char* key, const std::string& def) {
    auto doc = Load();
    const json::Value* v = doc ? Lookup(*doc, key) : nullptr;
    return v && v->IsString() ? v->AsString() : def;
}

bool SetSettingBool(const char* key, bool value) {
    const auto dir = DataDir();
    if (dir.empty()) return false;
    json::Value root;
    if (auto doc = Load()) root = std::move(*doc);
    else root = json::Value(json::Object{});
    const std::string k = key;
    std::vector<std::string> parts;
    for (size_t pos = 0;;) {
        const size_t dot = k.find('.', pos);
        parts.push_back(k.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos));
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    std::vector<json::Value> chain;
    const json::Value* cur = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const json::Value* child = cur ? cur->Find(parts[i]) : nullptr;
        chain.push_back(child && child->IsObject() ? *child : json::Value(json::Object{}));
        cur = child;
    }
    json::Value v(value);
    for (size_t i = parts.size(); i-- > 1;) {
        chain[i - 1].Set(parts[i], std::move(v));
        v = std::move(chain[i - 1]);
    }
    root.Set(parts[0], std::move(v));
    const auto path = dir / L"settings.json";
    auto tmp = path;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        const std::string text = root.Dump();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

}  // namespace velora
