// velora-watchdog: restores Windows if the shell dies or hangs (CLAUDE.md §4).
//
//   --watch <shellPid>  normal mode, started by velora-shell
//   --restore           restore Windows now (Start menu "Restore Windows Desktop")
//   --login-check       at login: restore only if a dirty marker is left over
//
// Watch mode ends when: the shell exits (restore if the marker is still there),
// the heartbeat times out (restore + terminate the shell), the emergency hotkey
// Ctrl+Alt+Shift+Esc is pressed (same), or the shell says goodbye.
// The only process this program ever terminates is the recorded velora-shell.
#include "core/win.h"

#include <shellapi.h>

#include <algorithm>
#include <string>

#include "core/contracts/ipc.h"
#include "core/ipc_pipe.h"
#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/state_store.h"
#include "switch/restore.h"
#include "switch/taskbar.h"

namespace {

using namespace velora;

constexpr int kHotkeyId = 1;
constexpr DWORD kStartupGraceMs = 5000;  // shell must say hello within this
constexpr DWORD kMaxWaitMs = 250;        // upper bound between wake-ups; deadlines wake us exactly

bool g_emergency = false;

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY && wp == kHotkeyId) {
        g_emergency = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int Restore(const char* reason, bool terminateShell) {
    sw::RestoreOptions o;
    o.terminateShell = terminateShell;
    o.reason = reason;
    auto r = sw::RestoreWindowsDesktop(o);
    return r.markerRemoved && r.taskbarsVisible ? 0 : 1;
}

int LoginCheck() {
    if (!DirtyMarkerExists(DirtyMarkerPath())) return 0;
    log::Warn("login check: dirty marker found");
    // Explorer may still be starting; give it a few seconds to create its tray
    // (test profile: the fakes are already there).
    for (int i = 0; i < 100 && sw::FindTrayWindows().empty(); ++i) Sleep(100);
    return Restore("login restore", true);
}

int Watch(DWORD shellPid) {
    auto state = LoadState(StatePath());

    HANDLE shell = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shellPid);
    if (!shell) {
        log::Error("cannot open shell process; restoring defensively");
        return Restore("shell not found", false);
    }
    if (state && state->shellPid == shellPid && !sw::IsRecordedShell(shell, *state)) {
        log::Error("pid " + std::to_string(shellPid) + " is not the recorded shell; refusing to watch it");
        CloseHandle(shell);
        return 1;
    }

    ipc::PipeServer server;
    bool hello = false, goodbye = false;
    ULONGLONG lastBeat = GetTickCount64();
    const ULONGLONG started = lastBeat;

    if (!server.Start(WatchdogPipeName(), [&](std::string_view raw) -> std::string {
            auto m = json::Parse(raw);
            if (!m || !m->IsObject()) return {};
            const auto* t = m->Find("type");
            const auto* k = m->Find("kind");
            if (!t || !k) return {};
            const std::string& type = t->AsString();
            if (type == ipc::msg::kHeartbeat) {
                lastBeat = GetTickCount64();
                return {};
            }
            if (type == ipc::msg::kExit) {
                goodbye = true;
                return {};
            }
            if (type == ipc::msg::kHello && k->AsString() == "request") {
                hello = true;
                lastBeat = GetTickCount64();
                std::int64_t id = m->Find("id") ? m->Find("id")->AsInt(-1) : -1;
                json::Value p;
                p.Set("pid", static_cast<std::int64_t>(GetCurrentProcessId()));
                p.Set("protocol", static_cast<int>(ipc::kProtocolVersion));
                return ipc::MakeMessage("response", ipc::msg::kHello, id, p, 0);
            }
            return {};
        })) {
        log::Warn("another watchdog is already running; exiting");
        CloseHandle(shell);
        return 0;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VeloraWatchdogWnd";
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Velora Watchdog", WS_POPUP, 0, 0, 0, 0, nullptr,
                                  nullptr, inst, nullptr);
    // Emergency hotkey lives here, not in the shell, so it works with a frozen shell.
    if (!window || !RegisterHotKey(window, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_ESCAPE))
        log::Warn("emergency hotkey Ctrl+Alt+Shift+Esc could not be registered: error " + std::to_string(GetLastError()));

    log::Info("watching shell pid " + std::to_string(shellPid));

    int exitCode = 0;
    for (;;) {
        std::vector<HANDLE> handles = server.WaitHandles();
        handles.push_back(shell);
        // Sleep until the nearest deadline (heartbeat timeout / startup grace), not on a fixed tick.
        ULONGLONG due = hello ? lastBeat + ipc::kHeartbeatTimeoutMs : started + kStartupGraceMs;
        ULONGLONG nowTick = GetTickCount64();
        DWORD wait = due > nowTick ? static_cast<DWORD>(std::min<ULONGLONG>(due - nowTick, kMaxWaitMs)) : 0;
        DWORD r = MsgWaitForMultipleObjectsEx(static_cast<DWORD>(handles.size()), handles.data(), wait, QS_ALLINPUT,
                                              MWMO_INPUTAVAILABLE);
        server.Pump();
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }

        if (g_emergency) {
            log::Warn("emergency hotkey pressed");
            exitCode = Restore("emergency hotkey", true);
            break;
        }
        if (r == WAIT_OBJECT_0 + handles.size() - 1 || WaitForSingleObject(shell, 0) == WAIT_OBJECT_0) {
            if (DirtyMarkerExists(DirtyMarkerPath())) {
                log::Warn("shell died with the dirty marker set (crash)");
                exitCode = Restore("shell crashed", true);
            } else {
                log::Info("shell exited cleanly");
            }
            break;
        }
        if (goodbye) {
            // Shell finished Exit() (marker is gone). If it is not, something is wrong: restore.
            if (DirtyMarkerExists(DirtyMarkerPath())) exitCode = Restore("goodbye with marker set", true);
            break;
        }
        ULONGLONG now = GetTickCount64();
        if (hello && now - lastBeat >= ipc::kHeartbeatTimeoutMs) {
            log::Error("shell heartbeat timed out (" + std::to_string(now - lastBeat) + " ms); restoring");
            exitCode = Restore("shell hung", true);
            break;
        }
        if (!hello && now - started >= kStartupGraceMs) {
            log::Error("shell never said hello; restoring");
            exitCode = Restore("shell never connected", true);
            break;
        }
    }

    UnregisterHotKey(window, kHotkeyId);
    if (window) DestroyWindow(window);
    CloseHandle(shell);
    return exitCode;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    log::Init("velora-watchdog");

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = 64;  // usage
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring a = argv[i];
            if (a == L"--restore") {
                rc = Restore("--restore", true);
                break;
            }
            if (a == L"--login-check") {
                rc = LoginCheck();
                break;
            }
            if (a == L"--watch" && i + 1 < argc) {
                rc = Watch(static_cast<DWORD>(_wtoi(argv[i + 1])));
                break;
            }
        }
        LocalFree(argv);
    }
    return rc;
}
