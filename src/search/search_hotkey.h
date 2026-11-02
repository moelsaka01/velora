// The global hotkey of the Search panel: RegisterHotKey only (no keyboard hook, no key injection). Owner: C.
#pragma once

#include <string>

#include "core/win.h"
#include "search/search_core.h"

namespace velora::search {

struct HotkeyChoice {
    Hotkey key;
    bool registered = false;  // false: every candidate was taken; Search stays reachable from the Velora menu and `--open-search`
    bool configured = false;  // the user's configured hotkey is the one in use
};

// Tries the configured hotkey (settings.json "search.hotkey", "" = none), then the defaults in order: Alt+Space "only if free", then
// Ctrl+Space. A candidate that RegisterHotKey refuses (another program owns it) is skipped.
HotkeyChoice RegisterSearchHotkey(HWND hwnd, int id, const std::wstring& configured);
void UnregisterSearchHotkey(HWND hwnd, int id);

// settings.json -> {"search": {"hotkey": "Ctrl+Alt+Space"}}; "" when absent or not a string.
std::wstring HotkeyFromSettingsJson(const std::string& json);
// Reads settings.json in the data directory ("" when it cannot be read).
std::wstring ConfiguredHotkey();

}  // namespace velora::search
