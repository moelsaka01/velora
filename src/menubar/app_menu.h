// Global app menus (CLAUDE.md sec. 5 "Global menu track", tiers 1 and 2). Owner: B (built with C on Day 4).
//
//  Tier 1  classic Win32 HMENU: GetMenu on the foreground window, WM_INITMENUPOPUP replayed so the app refreshes its items (what its
//          own menu bar does when you open a menu), invoked with WM_COMMAND. Nothing is injected into the app.
//  Tier 2  UI Automation (MenuBar / MenuItem controls; Invoke and ExpandCollapse patterns) for WinForms, WPF, Qt, XAML, Chromium
//          and Electron windows that have no HMENU. Opening a menu through UIA makes the app open its own drop-down for a moment.
//
// Everything that talks to another process runs on a worker thread (app_menu_provider); this file is the synchronous layer.
// The pure helpers (labels, gating) are unit-tested without any window.
#pragma once

#include <string>
#include <vector>

#include "core/win.h"

namespace velora::menubar {

enum class MenuTier { None, Win32, Uia };

struct AppMenuItem {
    std::wstring label, shortcut;
    bool separator = false, enabled = true, checked = false;
    bool submenu = false;  // opens a submenu (Win32: `children` holds it; UIA: read when the user opens it)
    unsigned id = 0;       // Win32: the WM_COMMAND id
    int uiaIndex = -1;     // UIA: position among the children of its menu (separators count), what `path` arguments refer to
    std::vector<AppMenuItem> children;
};

struct AppMenuTitle {
    std::wstring label;
    bool enabled = true;
    bool direct = false;  // Win32 only: a command in the bar itself, no drop-down
    unsigned id = 0;      // Win32 direct command id
    int index = 0;        // what ReadWin32Items / ReadUiaItems / InvokeUia take as `titleIndex`
};

struct AppMenuBar {
    MenuTier tier = MenuTier::None;
    HWND hwnd = nullptr;
    std::vector<AppMenuTitle> titles;
};

// ---- pure helpers ----
struct MenuText {
    std::wstring label, shortcut;
};
// "&File" -> "File"; "Save &As...\tCtrl+Shift+S" -> label "Save As...", shortcut "Ctrl+Shift+S". "&&" is a literal ampersand.
// `mnemonic` is the mnemonic marker ('&' for Win32 / WinForms / Qt, '_' for WPF headers).
MenuText SplitMenuText(const std::wstring& raw, wchar_t mnemonic = L'&');
// Window classes of toolkits whose accessibility is switched on by the first UI Automation client (Chromium family): querying them
// makes the app pay for accessibility for the rest of its life, so they are only probed when the user opts in.
bool IsChromiumClass(const std::wstring& windowClass);
// Whether the automatic tier 2 probe may run on a window of this class.
bool MayProbeUia(const std::wstring& windowClass, bool probeChromium);
// A whole drop-down for the Velora popup: the titles of the bar as "File Edit ..." for logs and tests.
std::wstring TitlesToString(const AppMenuBar& bar);

// ---- tier 1 (needs no COM; safe on any thread) ----
// The classic menu bar of `hwnd`; false when the window has none (or only owner-drawn items that expose no text).
bool ReadWin32Bar(HWND hwnd, AppMenuBar* out);
// The items under title `titleIndex`. Sends WM_INITMENUPOPUP to the window first (timeout, aborted when the app hangs). Submenus are
// read to `maxDepth` levels, each one initialised the same way.
bool ReadWin32Items(HWND hwnd, int titleIndex, std::vector<AppMenuItem>* out, int maxDepth = 3, unsigned timeoutMs = 300);
// Posts WM_COMMAND. False when the window is gone.
bool InvokeWin32(HWND hwnd, unsigned id);

// ---- tier 2 (COM must be initialised on the calling thread; the multithreaded apartment is expected) ----
bool ReadUiaBar(HWND hwnd, AppMenuBar* out);
// Opens title `titleIndex` (and the submenus along `path`, child indexes), reads the items of the last menu, closes it again.
bool ReadUiaItems(HWND hwnd, int titleIndex, const std::vector<int>& path, std::vector<AppMenuItem>* out);
// Opens title `titleIndex`, follows `path` (child indexes) and invokes the item there; `label` is checked against the item found.
bool InvokeUia(HWND hwnd, int titleIndex, const std::vector<int>& path, const std::wstring& label);
// Direct UIA invoke of a title that is itself a command (no drop-down).
bool InvokeUiaTitle(HWND hwnd, int titleIndex);
// Diagnostics (MENU_COMPAT work, tests): the UIA subtree under title `titleIndex` after opening it, and the popup windows of the process.
std::wstring DescribeUiaMenu(HWND hwnd, int titleIndex);

}  // namespace velora::menubar
