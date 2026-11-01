// Dock data model: pinned apps, running windows grouped per app, click behaviour, and the
// persisted pinned list. Pure logic (no windows.h), unit-tested. Owner: B.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace velora::dock {

struct DockApp {
    std::wstring name;
    std::wstring launch;   // ShellExecute target; empty for the internal Apps item
    std::wstring icon;     // shell parsing name for the icon
    std::wstring aumid;    // AppUserModelID ("" for classic apps)
    std::wstring exePath;  // for matching running windows ("" for MSIX)
    bool internalApps = false;  // opens the apps grid
    friend bool operator==(const DockApp&, const DockApp&) = default;
};

struct DockWindow {
    std::uint64_t hwnd = 0;
    std::wstring title;
    std::wstring aumid;    // from the window's property store, "" if none
    std::wstring exePath;  // process image of the window ("" if unknown)
    bool minimized = false;
    bool controllable = true;  // false: elevated owner, cannot be minimized or closed by us
    bool foreground = false;
};

struct DockItem {
    DockApp app;
    bool pinned = false;
    std::vector<DockWindow> windows;
    bool Running() const { return !windows.empty(); }
    bool Active() const;  // owns the foreground window
    bool AnyUncontrollable() const;
};

// Does a window belong to this app? AUMID match wins; otherwise the exe path (case-insensitive).
bool Matches(const DockApp& app, const DockWindow& w);

// Pinned apps the Dock shows: the Apps item, shell:/URI targets, and files that exist - minus Velora's own
// apps that have not reached their G1 gate yet (their executables are empty stubs). The saved pinned list is
// not changed; a hidden entry reappears once the file exists / the module is ready.
std::vector<DockApp> VisiblePinned(const std::vector<DockApp>& pinned,
                                   const std::function<bool(const std::wstring&)>& fileExists);

// Pinned apps first (in order), then one item per running app that is not pinned (in window order).
// `describe` builds the app info for an unpinned running window.
std::vector<DockItem> BuildItems(const std::vector<DockApp>& pinned, const std::vector<DockWindow>& windows,
                                 const std::function<DockApp(const DockWindow&)>& describe);

enum class ClickKind { Launch, Activate, Minimize, None };
struct ClickAction {
    ClickKind kind = ClickKind::None;
    std::uint64_t hwnd = 0;
};
// Not running: launch. Running and inactive: activate the first visible window (else restore the first one).
// Active with one controllable window: minimize it. Active with several: activate the next one.
ClickAction DecideClick(const DockItem& item);

// Persisted pinned list (the "dock" section of settings.json).
std::string PinnedToJson(const std::vector<DockApp>& pinned);
// nullopt-like: returns false when the text has no valid dock section.
bool PinnedFromJson(const std::string& settingsJson, std::vector<DockApp>* out);

// Case-insensitive path/ID comparison.
bool Ieq(const std::wstring& a, const std::wstring& b);

}  // namespace velora::dock
