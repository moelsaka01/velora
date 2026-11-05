// Installed-app index for the apps grid, Dock and (later) search. Owner: B.
//
// Sources: the shell AppsFolder (what Start lists: Win32 shortcuts and MSIX packages, with
// AppUserModelIDs) plus Start-menu .lnk files (both Programs folders). The index is cached
// in %LOCALAPPDATA%\Velora\apps_index.json, loaded at start, and refreshed in the background
// when a Start-menu folder changes or every 30 minutes. Opening the grid never scans.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/win.h"

namespace velora::apps {

struct AppEntry {
    std::wstring name;
    std::wstring aumid;    // AppUserModelID when known ("" otherwise)
    std::wstring exePath;  // resolved target for Win32 apps ("" for MSIX)
    std::wstring launch;   // ShellExecute target ("shell:AppsFolder\\<id>" or a .lnk path)
    std::wstring icon;     // shell parsing name the icon is loaded from
    friend bool operator==(const AppEntry&, const AppEntry&) = default;
};

// ---- pure helpers (unit-tested) ----
// 0 = no match; higher = better (prefix > word prefix > substring > subsequence). Empty query matches all.
int MatchScore(const std::wstring& query, const std::wstring& name);
// Shortcuts that are not apps (uninstallers, readmes, help).
bool IsNoiseShortcut(const std::wstring& name);
// A Start-menu Internet Shortcut (.url) that starts an app through a protocol (steam://rungameid/..., com.epicgames.launcher://...).
// Web links (http, https, ftp, file, mailto) are not apps. The icon is the shortcut's IconFile when it exists, else the shortcut itself.
std::optional<AppEntry> ParseUrlShortcut(const std::filesystem::path& file);
// The program a shortcut really starts: the App-V launcher of Click-to-Run Office (AppVLP.exe) is only a trampoline
// whose first quoted argument is the tool; everything else is returned as is.
std::wstring RealProgram(const std::wstring& exe, const std::wstring& args);
// A classic (non-MSIX) entry whose executable is gone: a shortcut left behind by an uninstalled app. It cannot be
// launched and has no icon to show, so the index drops it. Packaged apps (no exe path), unknown and unexpanded targets are never stale.
bool IsStaleEntry(const AppEntry& entry, const std::function<bool(const std::wstring&)>& exists);
// Merge sources in priority order: the first entry per case-insensitive name wins, missing exePath is filled
// from later duplicates; the result is sorted by name.
std::vector<AppEntry> MergeEntries(std::vector<std::vector<AppEntry>> sources);
std::string EntriesToJson(const std::vector<AppEntry>& entries);
std::vector<AppEntry> EntriesFromJson(const std::string& text);
// Indices into `entries` matching `query`, best first (stable by name).
std::vector<size_t> Filter(const std::vector<AppEntry>& entries, const std::wstring& query);

// ---- system ----
std::vector<AppEntry> ScanInstalledApps();  // needs COM initialised on the calling thread
bool Launch(const AppEntry& app);

class AppIndex {
public:
    static AppIndex& Get();  // leaked on purpose: the scan thread may outlive main()

    void LoadCache();
    // Starts the background scan/watch thread once; `notify` gets `message` when the index changed.
    void StartBackground(HWND notify, UINT message);
    std::vector<AppEntry> Snapshot() const;
    // Snapshot() through the launcher transform (the Apple group, when enabled); the Dock keeps using Snapshot().
    std::vector<AppEntry> SnapshotForLauncher() const;
    // Gaming mode: no scans (and no change notifications) while paused; the cached list stays usable.
    void SetPaused(bool paused) { paused_ = paused; }
    void SetLauncherTransform(std::function<std::vector<AppEntry>(std::vector<AppEntry>)> fn);
    unsigned Generation() const;

private:
    AppIndex() = default;
    void Run();
    void Set(std::vector<AppEntry> fresh, bool save);

    mutable std::mutex mutex_;
    std::vector<AppEntry> entries_;
    unsigned generation_ = 0;
    bool started_ = false;
    std::atomic<bool> paused_{false};
    HWND notify_ = nullptr;
    UINT message_ = 0;
    std::function<std::vector<AppEntry>(std::vector<AppEntry>)> launcherTransform_;
};

}  // namespace velora::apps
