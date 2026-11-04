#include "search/search_hotkey.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"

namespace velora::search {

namespace {
bool TryRegister(HWND hwnd, int id, const Hotkey& k) {
    return RegisterHotKey(hwnd, id, k.mods | MOD_NOREPEAT, k.vk) != FALSE;
}
}  // namespace

HotkeyChoice RegisterSearchHotkey(HWND hwnd, int id, const std::wstring& configured) {
    HotkeyChoice choice;
    std::vector<std::pair<Hotkey, bool>> candidates;
    Hotkey custom;
    if (configured == L"none") {  // the user turned the hotkey off in Settings; Search stays reachable from the Velora menu
        log::Info("search: hotkey disabled in settings");
        return choice;
    }
    if (!configured.empty()) {
        if (ParseHotkey(configured, &custom)) candidates.push_back({custom, true});
        else log::Warn("search: ignoring the configured hotkey \"" + WideToUtf8(configured) + "\" (not a modifier + key combination)");
    }
    candidates.push_back({Hotkey{kModAlt, 0x20}, false});      // Alt+Space, only if free
    candidates.push_back({Hotkey{kModControl, 0x20}, false});  // Ctrl+Space
    for (const auto& [key, isConfigured] : candidates) {
        if (TryRegister(hwnd, id, key)) {
            choice.key = key;
            choice.registered = true;
            choice.configured = isConfigured;
            log::Info("search: hotkey " + WideToUtf8(FormatHotkey(key)) + " registered");
            return choice;
        }
        log::Info("search: hotkey " + WideToUtf8(FormatHotkey(key)) + " is not available (error " + std::to_string(GetLastError()) + ")");
    }
    log::Warn("search: no hotkey could be registered; Search opens from the Velora menu only");
    return choice;
}

void UnregisterSearchHotkey(HWND hwnd, int id) { UnregisterHotKey(hwnd, id); }

std::wstring HotkeyFromSettingsJson(const std::string& text) {
    auto root = json::Parse(text);
    if (!root || !root->IsObject()) return {};
    const json::Value* search = root->Find("search");
    const json::Value* hot = search && search->IsObject() ? search->Find("hotkey") : nullptr;
    return hot && hot->IsString() ? Utf8ToWide(hot->AsString()) : std::wstring{};
}

std::wstring ConfiguredHotkey() {
    const auto dir = DataDir();
    if (dir.empty()) return {};
    std::ifstream f(dir / L"settings.json", std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return HotkeyFromSettingsJson(ss.str());
}

}  // namespace velora::search
