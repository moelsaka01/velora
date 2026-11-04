#include "switch/restore.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <thread>

#include "core/log.h"
#include "core/runtime.h"
#include "core/state_store.h"
#include "switch/appbar.h"
#include "switch/taskbar.h"
#include "theming/window_theme.h"
#include "wallpaper/wallpaper.h"

namespace velora::sw {

bool IsRecordedShell(HANDLE process, const SwitchState& state) {
    if (state.shellStartTime != 0 && ProcessStartTime(process) != state.shellStartTime) return false;

    wchar_t path[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process, 0, path, &n)) return false;
    std::wstring name(path, n);
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name.erase(0, slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name == L"velora-shell.exe";
}

namespace {

bool TerminateRecordedShell(const SwitchState& state) {
    if (state.shellPid == 0 || state.shellPid == GetCurrentProcessId()) return false;
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, state.shellPid);
    if (!h) return false;  // already gone, or not ours to touch
    bool killed = false;
    if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT && IsRecordedShell(h, state)) {
        log::Warn("terminating velora-shell (pid " + std::to_string(state.shellPid) + ") before restore");
        if (TerminateProcess(h, 1)) {
            WaitForSingleObject(h, 500);
            killed = true;
        }
    }
    CloseHandle(h);
    return killed;
}

}  // namespace

RestoreResult RestoreWindowsDesktop(const RestoreOptions& opt) {
    RestoreResult r;
    ULONGLONG t0 = GetTickCount64();
    log::Info(std::string("restore begin: ") + opt.reason);

    // state.json is only meaningful while the dirty marker exists (the marker is the
    // single source of truth for "a session is, or crashed while, active").
    const auto markerPath = DirtyMarkerPath();
    const auto statePath = StatePath();
    const bool dirty = DirtyMarkerExists(markerPath);
    std::optional<SwitchState> state;
    if (dirty) {
        state = LoadState(statePath);
        if (!state) log::Warn("restore: no readable state.json; applying safe defaults");
    } else {
        log::Info("restore: no dirty marker; ignoring any state.json");
    }
    r.hadState = state.has_value();

    // The wallpaper restore is slow (~300 ms of Explorer round trips) and independent of the taskbar steps:
    // run it alongside them and join before the marker goes. Only on a normal exit, where the shell is still
    // alive and nothing it touches is being torn down; recovery keeps the old sequential order.
    std::thread wallpaperThread;
    if (!CurrentProfile().IsTest() && !opt.terminateShell) wallpaperThread = std::thread([] { wallpaper::RestoreOriginal(); });

    // 1. Taskbar first (never wrong, and the user sees it back at once); then the shell,
    //    which must not be alive to re-hide it behind us. Step 3 re-asserts visibility.
    if (opt.terminateShell) SetTaskbarsVisible(true);
    if (opt.terminateShell && state) {
        r.terminatedShell = TerminateRecordedShell(*state);
        RemoveRecordedAppbars(*state);  // Explorer may not have noticed the dead appbar yet
    }

    // 2. Auto-hide back to what it was (persistent Explorer setting!).
    if (state) SetTaskbarAutoHide(state->taskbarAutoHide);

    // 3. Taskbars visible. Done even without state: showing a taskbar is never wrong.
    SetTaskbarsVisible(true);
    r.taskbarsVisible = WaitTaskbarsVisible(true, 800);
    r.taskbarsMs = static_cast<DWORD>(GetTickCount64() - t0);
    if (!r.taskbarsVisible) log::Error("restore: taskbar windows did not become visible within 800 ms");
    if (opt.requireTaskbar && !r.taskbarsVisible) {
        // Pending, not done: keep the marker and state.json so the retry can finish the job.
        r.pending = true;
        if (wallpaperThread.joinable()) wallpaperThread.join();
        r.totalMs = static_cast<DWORD>(GetTickCount64() - t0);
        log::Warn("restore pending: no taskbar window yet; marker and state kept");
        return r;
    }

    // 4. Work areas: Explorer drops dead appbars itself, but give it a moment, correct
    //    what is still wrong, and (recovery) verify again for up to 500 ms.
    if (state) {
        Sleep(50);
        r.workAreasReclaimed = ReclaimWorkAreas(*state);
        r.workAreasOk = WorkAreasMatch(*state);
        if (!r.workAreasOk && opt.terminateShell) {
            for (int i = 0; i < 10 && !r.workAreasOk; ++i) {
                Sleep(50);
                r.workAreasOk = WorkAreasMatch(*state);
            }
            if (!r.workAreasOk) {
                r.workAreasReclaimed += ReclaimWorkAreas(*state);
                r.workAreasOk = WorkAreasMatch(*state);
            }
            if (!r.workAreasOk) log::Error("restore: work area still differs from the recorded one");
        }
    }

    // 5. Focus (normal exit only).
    if (opt.restoreForeground && state && state->foregroundHwnd) {
        HWND fg = reinterpret_cast<HWND>(static_cast<UINT_PTR>(state->foregroundHwnd));
        if (IsWindow(fg) && IsWindowVisible(fg) && !IsIconic(fg)) SetForegroundWindow(fg);
    }

    // 5a. Windows themed through DWM attributes: put each attribute back (a no-op when nothing was themed). Also done in the
    //     test profile (its own data dir), so the gate sees the same recovery path.
    theming::RestoreFromBackup();

    // 5b. Wallpaper: put the user's own back (no-op unless Velora changed it and a backup exists).
    if (wallpaperThread.joinable()) wallpaperThread.join();
    else if (!CurrentProfile().IsTest()) wallpaper::RestoreOriginal();

    // 6. Last: the marker. Until it is gone, recovery stays armed.
    r.markerRemoved = RemoveDirtyMarker(markerPath);
    if (!r.markerRemoved) log::Error("restore: could not remove dirty marker");
    // Consumed: with no marker it must never be applied again.
    if (r.markerRemoved && !statePath.empty() && !DeleteFileW(statePath.c_str()) &&
        GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        log::Warn("restore: could not delete state.json");

    r.totalMs = static_cast<DWORD>(GetTickCount64() - t0);
    log::Info("restore done in " + std::to_string(r.totalMs) + " ms (taskbars visible after " +
              std::to_string(r.taskbarsMs) + " ms)");
    return r;
}

}  // namespace velora::sw
