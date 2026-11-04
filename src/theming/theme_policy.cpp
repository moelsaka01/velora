#include "theming/theme_policy.h"

#include <algorithm>
#include <cwctype>

#include "core/runtime.h"
#include "core/text.h"
#include "core/win.h"

namespace velora::theming {

std::wstring Lower(std::wstring_view s) {
    std::wstring r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return r;
}

const char* SkipName(Skip s) {
    switch (s) {
        case Skip::None: return "themed";
        case Skip::Disabled: return "disabled";
        case Skip::OwnWindow: return "velora-window";
        case Skip::Uncontrollable: return "elevated";
        case Skip::Fullscreen: return "fullscreen";
        case Skip::Cloaked: return "cloaked";
        case Skip::Hidden: return "hidden";
        case Skip::ShellWindow: return "shell-window";
        case Skip::ToolWindow: return "tool-window";
        case Skip::NoFrame: return "no-frame";
        case Skip::Game: return "game";
        case Skip::AntiCheat: return "anti-cheat";
        case Skip::Excluded: return "excluded";
    }
    return "?";
}

namespace {

bool Contains(std::wstring_view hay, std::wstring_view needle) { return hay.find(needle) != std::wstring_view::npos; }

constexpr const wchar_t* kAntiCheat[] = {
    L"easyanticheat.exe",       L"easyanticheat_eos.exe", L"easyanticheat_launcher.exe", L"eac_launcher.exe",
    L"beservice.exe",           L"beservice_x64.exe",     L"battleye.exe",               L"vgc.exe",
    L"vgtray.exe",              L"faceit.exe",            L"faceitclient.exe",           L"faceit-ac.exe",
    L"eaanticheat.gameservice.exe", L"punkbuster.exe",    L"pnkbstra.exe",               L"pnkbstrb.exe",
    L"gameguard.des",           L"xigncode3.exe",         L"nprotect.exe",               L"equ8.exe",
    L"ricochet.exe",
};

// Install locations of game launchers' libraries: a window running from there is treated as a game.
constexpr const wchar_t* kGamePaths[] = {
    L"\\steamapps\\common\\", L"\\epic games\\",       L"\\gog galaxy\\games\\", L"\\gog games\\",
    L"\\riot games\\",        L"\\ubisoft game launcher\\games\\",  L"\\ea games\\", L"\\origin games\\",
    L"\\xboxgames\\",         L"\\battle.net\\games\\", L"\\rockstar games\\",
};

constexpr const wchar_t* kGameClasses[] = {
    L"UnityWndClass", L"UnrealWindow", L"GLFW30", L"SDL_app", L"Valve001", L"CryENGINE", L"FNA_WINDOW", L"LWJGL",
};

constexpr const wchar_t* kShellClasses[] = {
    L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW", L"NotifyIconOverflowWindow",
    L"TopLevelWindowForOverflowXamlIsland", L"XamlExplorerHostIslandWindow", L"#32768", L"tooltips_class32",
    L"SysShadow", L"ForegroundStaging", L"MultitaskingViewFrame", L"Windows.UI.Core.CoreWindow",
};

}  // namespace

bool IsAntiCheatExe(std::wstring_view n) {
    for (const wchar_t* a : kAntiCheat)
        if (n == a) return true;
    return false;
}

bool IsGameLibraryPath(std::wstring_view p) {
    for (const wchar_t* g : kGamePaths)
        if (Contains(p, g)) return true;
    return false;
}

bool IsGameWindowClass(std::wstring_view c) {
    for (const wchar_t* g : kGameClasses)
        if (c == g) return true;
    return false;
}

bool IsShellWindowClass(std::wstring_view c) {
    for (const wchar_t* s : kShellClasses)
        if (c == s) return true;
    return false;
}

bool IsExcluded(const Config& c, std::wstring_view n) {
    if (n.empty()) return false;
    for (const auto& e : c.excludedApps)
        if (e == n) return true;
    return false;
}

Skip Evaluate(const WindowFacts& w, const Config& c) {
    if (!c.enabled) return Skip::Disabled;
    if (w.ownProcess) return Skip::OwnWindow;
    if (w.uncontrollable) return Skip::Uncontrollable;
    if (w.fullscreen) return Skip::Fullscreen;
    if (w.cloaked) return Skip::Cloaked;
    if (!w.visible) return Skip::Hidden;
    if (IsShellWindowClass(w.className)) return Skip::ShellWindow;
    if (w.exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return Skip::ToolWindow;
    if (!(w.style & WS_CAPTION) && !(w.style & WS_THICKFRAME)) return Skip::NoFrame;
    if (IsAntiCheatExe(w.exeName)) return Skip::AntiCheat;
    if (IsGameWindowClass(w.className) || IsGameLibraryPath(w.exePath)) return Skip::Game;
    if (IsExcluded(c, w.exeName)) return Skip::Excluded;
    return Skip::None;
}

Colours NeutralColours(bool dark) {
    auto rgb = [](int r, int g, int b) { return static_cast<unsigned long>(r | (g << 8) | (b << 16)); };
    if (dark) return {rgb(32, 32, 34), rgb(240, 240, 242), rgb(58, 58, 61)};
    return {rgb(243, 243, 245), rgb(30, 30, 32), rgb(214, 214, 217)};
}

namespace {

const char* BackdropName(Backdrop b) {
    switch (b) {
        case Backdrop::None: return "none";
        case Backdrop::Mica: return "mica";
        case Backdrop::Acrylic: return "acrylic";
        case Backdrop::Tabbed: return "tabbed";
    }
    return "mica";
}

Backdrop BackdropFrom(const std::string& s, Backdrop def) {
    if (s == "none") return Backdrop::None;
    if (s == "mica") return Backdrop::Mica;
    if (s == "acrylic") return Backdrop::Acrylic;
    if (s == "tabbed") return Backdrop::Tabbed;
    return def;
}

}  // namespace

Config ConfigFromSettings(const json::Value& root) {
    Config c;
    const json::Value* t = root.IsObject() ? root.Find("theming") : nullptr;
    if (!t || !t->IsObject()) return c;
    if (const auto* v = t->Find("enabled")) c.enabled = v->AsBool(c.enabled);
    if (const auto* v = t->Find("corners")) c.corners = v->AsBool(c.corners);
    if (const auto* v = t->Find("caption")) c.caption = v->AsBool(c.caption);
    if (const auto* v = t->Find("backdrop"); v && v->IsString()) c.backdrop = BackdropFrom(v->AsString(), c.backdrop);
    if (const auto* v = t->Find("exclude"); v && v->IsArray()) {
        for (const auto& e : v->AsArray()) {
            if (!e.IsString()) continue;
            std::wstring n = Lower(Utf8ToWide(e.AsString()));
            if (!n.empty() && std::find(c.excludedApps.begin(), c.excludedApps.end(), n) == c.excludedApps.end())
                c.excludedApps.push_back(std::move(n));
            if (c.excludedApps.size() >= 256) break;
        }
    }
    return c;
}

Config LoadConfig() {
    auto dir = DataDir();
    if (dir.empty()) return {};
    HANDLE h = CreateFileW((dir / L"settings.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (4 << 20)) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr)) got = 0;
        text.resize(got);
    }
    CloseHandle(h);
    auto doc = json::Parse(text);
    return doc ? ConfigFromSettings(*doc) : Config{};
}

json::Value ConfigToJson(const Config& c) {
    json::Value o;
    o.Set("enabled", c.enabled);
    o.Set("corners", c.corners);
    o.Set("caption", c.caption);
    o.Set("backdrop", BackdropName(c.backdrop));
    json::Array ex;
    for (const auto& e : c.excludedApps) ex.emplace_back(WideToUtf8(e));
    o.Set("exclude", json::Value(std::move(ex)));
    return o;
}

}  // namespace velora::theming
