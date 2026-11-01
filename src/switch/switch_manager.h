// Enter/exit of Velora Mode (CLAUDE.md §4). Owner: A.
#pragma once

#include <functional>

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

    // false => nothing was changed (everything rolled back).
    bool Enter();
    void Exit();  // idempotent

    void OnTaskbarCreated();  // Explorer restarted
    void Reconcile();         // ~1 Hz: re-hide taskbars Explorer re-showed, watch the watchdog
    void Heartbeat();         // ~1 Hz: tell the watchdog the UI thread is alive

    bool Entered() const { return entered_; }
    json::Value Status() const;

private:
    bool StartWatchdog();
    void StopWatchdog(bool graceful);
    void Rollback();

    EventBus& bus_;
    HINSTANCE instance_;
    AppBar bar_;
    bool entered_ = false;
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
