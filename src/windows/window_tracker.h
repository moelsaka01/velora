// Top-level window tracking via SetWinEventHook (out-of-context: no DLL is ever
// injected into another process). Owner: A.
//
// Tracks "app windows" (what Alt+Tab would list): visible, not cloaked, not
// owned, not a tool window. Handles cloaked (UWP / other-desktop) windows,
// marks windows of elevated processes as not controllable, and detects
// fullscreen so the bars can hide.
//
// Threading: Start()/Stop() and all callbacks run on the calling thread, which
// must pump messages (WinEvent out-of-context delivery uses its queue).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/contracts/events.h"
#include "core/event_bus.h"
#include "core/win.h"

namespace velora::win {

struct TrackedWindow {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
    std::wstring className;
    bool minimized = false;
    bool cloaked = false;
    bool controllable = true;  // false: elevated/protected owner, show a badge, don't act on it
};

struct TrackerOptions {
    bool includeOwnProcess = false;  // tests only; the shell never tracks its own windows
};

// Pure helpers (unit-tested).
bool IsFullscreenRect(const RECT& window, const RECT& monitor);
// True if a window of `pid` cannot be controlled from this process. `selfElevated`
// is this process' own elevation (an elevated shell controls elevated windows).
struct Elevation {
    bool elevated = false;
    bool inaccessible = false;  // could not even be queried (protected/system)
    bool Uncontrollable(bool selfElevated) const { return inaccessible || (elevated && !selfElevated); }
};
Elevation QueryElevation(DWORD pid);
bool IsSelfElevated();

class WindowTracker {
public:
    WindowTracker(EventBus& bus, TrackerOptions options = {});
    WindowTracker(const WindowTracker&) = delete;
    WindowTracker& operator=(const WindowTracker&) = delete;
    ~WindowTracker() { Stop(); }

    bool Start();  // installs hooks and enumerates existing windows
    void Stop();

    const std::map<HWND, TrackedWindow>& Windows() const { return windows_; }
    const TrackedWindow* Find(HWND h) const;
    HWND Foreground() const { return foreground_; }
    bool FullscreenActive() const { return fullscreenWindow_ != nullptr; }
    HWND FullscreenWindow() const { return fullscreenWindow_; }

    // Re-read one window / re-check fullscreen. Cheap; also used by callers on a timer
    // as a safety net for events WinEvents can miss.
    void Refresh(HWND h);
    void EvaluateFullscreen();

private:
    static void CALLBACK HookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD thread, DWORD time);
    void OnEvent(DWORD event, HWND hwnd);
    bool IsAppWindow(HWND h) const;
    TrackedWindow Read(HWND h, const TrackedWindow* previous) const;
    void Post(EventType type, const TrackedWindow& w);
    void HookLocationFor(HWND foreground);

    EventBus& bus_;
    TrackerOptions options_;
    std::map<HWND, TrackedWindow> windows_;
    std::vector<HWINEVENTHOOK> hooks_;
    HWINEVENTHOOK locationHook_ = nullptr;
    HWND foreground_ = nullptr;
    HWND fullscreenWindow_ = nullptr;
    bool selfElevated_ = false;
};

}  // namespace velora::win
