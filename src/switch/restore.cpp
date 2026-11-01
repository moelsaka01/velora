#include "switch/restore.h"

#include <algorithm>
#include <cwctype>
#include <string>

#include "core/log.h"
#include "core/runtime.h"
#include "core/state_store.h"
#include "switch/taskbar.h"

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
            WaitForSingleObject(h, 1000);
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

    auto state = LoadState(StatePath());
    r.hadState = state.has_value();
    if (!state) log::Warn("restore: no readable state.json; applying safe defaults");

    // 1. The shell must not be alive to re-hide the taskbar behind us.
    if (opt.terminateShell && state) r.terminatedShell = TerminateRecordedShell(*state);

    // 2. Auto-hide back to what it was (persistent Explorer setting!).
    if (state) SetTaskbarAutoHide(state->taskbarAutoHide);

    // 3. Taskbars visible. Done even without state: showing a taskbar is never wrong.
    SetTaskbarsVisible(true);
    r.taskbarsVisible = WaitTaskbarsVisible(true, 800);
    r.taskbarsMs = static_cast<DWORD>(GetTickCount64() - t0);
    if (!r.taskbarsVisible) log::Error("restore: taskbar windows did not become visible within 800 ms");

    // 4. Work areas: Explorer drops dead appbars itself, but give it a moment
    //    and only then correct what is still wrong.
    if (state) {
        Sleep(50);
        r.workAreasReclaimed = ReclaimWorkAreas(*state);
    }

    // 5. Focus (normal exit only).
    if (opt.restoreForeground && state && state->foregroundHwnd) {
        HWND fg = reinterpret_cast<HWND>(static_cast<UINT_PTR>(state->foregroundHwnd));
        if (IsWindow(fg) && IsWindowVisible(fg) && !IsIconic(fg)) SetForegroundWindow(fg);
    }

    // 6. Last: the marker. Until it is gone, recovery stays armed.
    r.markerRemoved = RemoveDirtyMarker(DirtyMarkerPath());
    if (!r.markerRemoved) log::Error("restore: could not remove dirty marker");

    r.totalMs = static_cast<DWORD>(GetTickCount64() - t0);
    log::Info("restore done in " + std::to_string(r.totalMs) + " ms (taskbars visible after " +
              std::to_string(r.taskbarsMs) + " ms)");
    return r;
}

}  // namespace velora::sw
