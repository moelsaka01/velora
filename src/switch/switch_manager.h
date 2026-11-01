// Enter/exit of Velora Mode (CLAUDE.md §4). Owner: A.
#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "core/contracts/state.h"
#include "core/event_bus.h"
#include "core/ipc_pipe.h"
#include "core/json.h"
#include "switch/appbar.h"

namespace velora::sw {

class SwitchManager {
public:
    SwitchManager(EventBus& bus, HINSTANCE instance);
    ~SwitchManager();

    // Fatal = the watchdog is gone and could not be replaced: the shell must leave
    // Velora Mode rather than run unprotected.
    void SetFatalHandler(std::function<void()> fn) { onFatal_ = std::move(fn); }

    // UI owner hooks (menu bar / Dock, owner B). Must be set before Enter(). The menu bar
    // appbar always exists; the Dock appbar only when SetDockOptions was called.
    void SetMenuBarOptions(AppBarOptions o) { menuBarOptions_ = std::move(o); }
    void SetDockOptions(AppBarOptions o, float reservedDip) { dockOptions_ = std::move(o); dockReservedDip_ = reservedDip; dockEnabled_ = true; }
    AppBar& MenuBarBar() { return bar_; }
    AppBar& DockBar() { return dock_; }

    // false => nothing was changed (everything rolled back).
    bool Enter();
    void Exit();  // idempotent

    void OnTaskbarCreated();  // Explorer restarted
    void Reconcile();         // ~1 Hz: re-hide taskbars Explorer re-showed, watch the watchdog
    void Heartbeat();         // ~1 Hz: tell the watchdog the UI thread is alive

    bool Entered() const { return entered_; }
    // Why the last Enter() failed, for a user-facing message ("" = no specific reason).
    const std::wstring& LastError() const { return lastError_; }
    json::Value Status() const;

private:
    static constexpr DWORD kDisarmTimeoutMs = 1000;
    static constexpr DWORD kWatchdogExitWaitMs = 2000;

    void RememberTrays();
    bool StartWatchdog();
    bool Disarm();
    void StopWatchdog(DWORD waitMs);
    void Rollback();

    EventBus& bus_;
    HINSTANCE instance_;
    AppBar bar_;   // top: menu bar
    AppBar dock_;  // bottom: Dock (optional)
    AppBarOptions menuBarOptions_, dockOptions_;
    float dockReservedDip_ = 0;
    bool dockEnabled_ = false;
    bool entered_ = false;
    bool entering_ = false;                 // inside Enter(): TaskbarCreated is queued
    bool pendingTaskbarCreated_ = false;
    std::wstring lastError_;
    std::vector<HWND> knownTrays_;          // sorted; a change means Explorer recreated its taskbar
    SwitchState state_;

    HANDLE watchdogProcess_ = nullptr;
    ipc::PipeClient watchdog_;
    int watchdogRestarts_ = 0;
    std::uint64_t heartbeatSeq_ = 0;

    int explorerRestarts_ = 0;
    int rehides_ = 0;
    bool autoHideFallback_ = false;
    std::function<void()> onFatal_;
};

}  // namespace velora::sw
