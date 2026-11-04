// Window theming policy (pure, unit-tested): which windows may be themed and with what. Owner: B.
//
// Theming only uses documented DWM window attributes set from the shell process (DwmSetWindowAttribute). Nothing is
// injected, subclassed or reparented. This file has no system calls so the rules can be tested exhaustively.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/json.h"

namespace velora::theming {

enum class Backdrop { None, Mica, Acrylic, Tabbed };

struct Config {
    bool enabled = true;
    bool corners = true;   // DWMWA_WINDOW_CORNER_PREFERENCE = round
    bool caption = true;   // dark/light caption, neutral border (and caption colour when there is no backdrop)
    Backdrop backdrop = Backdrop::Mica;
    std::vector<std::wstring> excludedApps;  // lower-case exe file names, e.g. "notepad++.exe"
};

// Parses the "theming" object of settings.json (any missing or malformed field keeps its default).
Config ConfigFromSettings(const json::Value& settingsRoot);
// Reads settings.json from the data directory (defaults when missing).
Config LoadConfig();
// Serialises back for the Settings app / tests.
json::Value ConfigToJson(const Config& c);

// Everything the policy needs to know about a window, gathered by the caller.
struct WindowFacts {
    std::wstring exeName;  // lower-case file name, empty when it could not be read
    std::wstring exePath;  // lower-case full path, empty when it could not be read
    std::wstring className;
    unsigned long style = 0;
    unsigned long exStyle = 0;
    bool visible = true;
    bool cloaked = false;
    bool fullscreen = false;
    bool uncontrollable = false;  // elevated or inaccessible owner
    bool ownProcess = false;      // Velora's own windows
};

enum class Skip {
    None,
    Disabled,
    OwnWindow,
    Uncontrollable,
    Fullscreen,
    Cloaked,
    Hidden,
    ShellWindow,
    ToolWindow,
    NoFrame,
    Game,
    AntiCheat,
    Excluded,
};

const char* SkipName(Skip s);

Skip Evaluate(const WindowFacts& w, const Config& c);

// Individual rules, exposed for tests.
bool IsAntiCheatExe(std::wstring_view lowerExeName);
bool IsGameLibraryPath(std::wstring_view lowerPath);
bool IsGameWindowClass(std::wstring_view className);
bool IsShellWindowClass(std::wstring_view className);
bool IsExcluded(const Config& c, std::wstring_view lowerExeName);

std::wstring Lower(std::wstring_view s);

// Neutral colours (COLORREF, 0x00BBGGRR) for the caption, its text and the border.
struct Colours {
    unsigned long caption, text, border;
};
Colours NeutralColours(bool dark);

}  // namespace velora::theming
