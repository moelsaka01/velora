// Reverses everything Enter did, from state.json (CLAUDE.md §4). Owner: A.
// Shared by the shell (normal exit), the watchdog (crash/hang/hotkey/login)
// and `velora-watchdog --restore`. Idempotent and safe to run with no state.
#pragma once

#include "core/contracts/state.h"
#include "core/win.h"

namespace velora::sw {

struct RestoreOptions {
    // Recovery paths: terminate the recorded shell process first (only if it is
    // verifiably velora-shell.exe with the recorded start time). It is Velora's
    // own process; nothing else is ever terminated.
    bool terminateShell = false;
    // Normal exit only: give focus back to the window that had it before Enter.
    bool restoreForeground = false;
    const char* reason = "restore";
};

struct RestoreResult {
    bool hadState = false;
    bool terminatedShell = false;
    bool taskbarsVisible = false;
    bool markerRemoved = false;
    int workAreasReclaimed = 0;
    DWORD taskbarsMs = 0;  // until taskbars were visible again
    DWORD totalMs = 0;
};

RestoreResult RestoreWindowsDesktop(const RestoreOptions& options);

// Returns true if `pid` is the recorded Velora shell (start time + image name).
bool IsRecordedShell(HANDLE process, const SwitchState& state);

}  // namespace velora::sw
