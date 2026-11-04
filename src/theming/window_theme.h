// Applies and restores window theming through documented DWM attributes. Owner: B.
//
// - Only DwmSetWindowAttribute from the shell process: no injection, no subclassing, no reparenting.
// - An attribute is changed only when its current value can be read first (so it can be put back) and only when the
//   change is visible when read back; otherwise it is left alone and logged as "ignored" / "refused".
//   Exception: the three colour attributes (border, caption, text) cannot be read on Windows 11 builds seen so far; their
//   original is assumed to be DWMWA_COLOR_DEFAULT and they are restored to it without comparing ("blind").
// - Every change is recorded in %LOCALAPPDATA%\Velora\theming_backup.json before the call returns, so a crash of the
//   shell is repaired by RestoreFromBackup() (watchdog, `--restore`, login recovery) exactly like the taskbar.
// - Restore puts an attribute back only if it still has the value Velora set (an app that changed it itself wins).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "core/win.h"
#include "theming/theme_policy.h"

namespace velora::theming {

std::filesystem::path BackupPath();

// Crash / `--restore` / login recovery. Idempotent; deletes the backup. Returns the number of windows restored.
int RestoreFromBackup();

WindowFacts GatherFacts(HWND hwnd, bool uncontrollable, bool fullscreen);

class WindowThemer {
public:
    WindowThemer() = default;
    WindowThemer(const WindowThemer&) = delete;
    WindowThemer& operator=(const WindowThemer&) = delete;
    ~WindowThemer() = default;  // does not restore: the owner calls RestoreAll() on exit

    // New settings and/or system theme. Windows already themed are re-synced (or restored when no longer wanted).
    void Configure(const Config& config, bool dark);
    const Config& GetConfig() const { return config_; }

    // Evaluates the policy and themes (or leaves) the window. Returns why a window was skipped (Skip::None = themed).
    Skip Apply(HWND hwnd, bool uncontrollable, bool fullscreen);
    void Forget(HWND hwnd);
    // Restores every themed window and removes the backup. Returns the number of windows restored.
    int RestoreAll();

    size_t Count() const { return entries_.size(); }
    bool IsThemed(HWND hwnd) const { return entries_.count(hwnd) != 0; }

    struct Stats {
        int themed = 0;
        int attributesChanged = 0;
        int attributesIgnored = 0;  // accepted by the call but not visible when read back
        int attributesRefused = 0;  // the call failed
        int attributesUnreadable = 0;  // original could not be read, so not touched
    };
    const Stats& GetStats() const { return stats_; }

    struct Attr {
        bool recorded = false;
        bool blind = false;  // colour attributes cannot be read back: assumed default before, restored without comparing
        std::uint32_t original = 0;
        std::uint32_t applied = 0;
    };
    enum AttrId { kCorner, kDark, kBorder, kCaption, kText, kBackdrop, kAttrCount };
    struct Entry {
        std::uint32_t pid = 0;
        std::uint64_t startTime = 0;
        std::wstring className;
        std::wstring exe;
        Attr attr[kAttrCount];
    };

private:
    void Sync(HWND hwnd, Entry& e);
    void Save() const;

    Config config_;
    bool dark_ = true;
    std::map<HWND, Entry> entries_;
    Stats stats_;
};

}  // namespace velora::theming
