// Taskbar and desktop-environment helpers for switching. Owner: A.
//
// Hiding strategy is ported from cairoshell/ManagedShell ExplorerHelper
// (Apache-2.0, see THIRD_PARTY_NOTICES.md): SetWindowPos(SWP_HIDEWINDOW) on the
// primary and secondary tray windows, ABM_SETSTATE auto-hide, and a periodic
// re-check. Explorer is never terminated, subclassed or injected into.
//
// In a test profile the tray window classes are fake ones owned by the test, and
// the auto-hide calls are no-ops, so tests cannot touch the real taskbar.
#pragma once

#include <vector>

#include "core/contracts/state.h"
#include "core/win.h"

namespace velora::sw {

// Primary + secondary tray windows (class names come from CurrentProfile()).
std::vector<HWND> FindTrayWindows();
bool AnyTaskbarVisible();
// True if a real Explorer taskbar (not a test fake) is present.
bool HasRealTaskbar();

// Non-blocking (SWP_ASYNCWINDOWPOS): a hung Explorer cannot hang the caller.
void SetTaskbarsVisible(bool visible);
bool WaitTaskbarsVisible(bool visible, DWORD timeoutMs);

// ABM_GETSTATE / ABM_SETSTATE ABS_AUTOHIDE. No-ops in a test profile.
bool GetTaskbarAutoHide();
void SetTaskbarAutoHide(bool autoHide);

std::vector<MonitorState> SnapshotMonitors();
// Puts a monitor's work area back to the recorded value if it differs and the
// recorded rect still fits the monitor. Returns the number of monitors changed.
int ReclaimWorkAreas(const SwitchState& state);

std::wstring CurrentWallpaper();
std::uint64_t ProcessStartTime(HANDLE process);  // FILETIME ticks, 0 on failure

}  // namespace velora::sw
