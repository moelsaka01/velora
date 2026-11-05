#include "core/settings_file.h"

#include <fstream>
#include <optional>
#include <sstream>

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

}  // namespace velora
