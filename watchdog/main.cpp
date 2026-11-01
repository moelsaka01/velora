// velora-watchdog: restores Windows if the shell dies or hangs (CLAUDE.md §4).
//
//   --watch <shellPid>  normal mode, started by velora-shell
//   --restore           restore Windows now (Start menu "Restore Windows Desktop")
//   --login-check       at login: restore only if a dirty marker is left over.
//                       Exit 0 = restored or nothing to do, 1 = failed, 3 = pending
//                       (no taskbar window appeared; marker kept for the next try)
//
// The watchdog is *armed* from the shell's hello (pid must match --watch) until
// the shell's synchronous "disarm". It restores Windows and terminates the shell
// only when it is armed AND the dirty marker exists; in every other state it
// exits quietly and touches nothing.
// Watch mode ends when: the shell disarms, the shell exits, the heartbeat times
// out, the emergency hotkey Ctrl+Alt+Shift+Esc is pressed, or the shell never
// said hello.
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

constexpr int kExitPending = 3;

bool g_emergency = false;
bool g_retryNow = false;       // login check: Explorer announced a new taskbar
UINT g_taskbarCreated = 0;

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY && wp == kHotkeyId) {
        g_emergency = true;
        return 0;
    }
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        g_retryNow = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Hidden *top-level* window: broadcast messages (TaskbarCreated) skip message-only windows.
HWND MakeWindow() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VeloraWatchdogWnd";
    RegisterClassExW(&wc);
    return CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Velora Watchdog", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                           inst, nullptr);
}

int Restore(const char* reason, bool terminateShell) {
    sw::RestoreOptions o;
    o.terminateShell = terminateShell;
    o.reason = reason;
    auto r = sw::RestoreWindowsDesktop(o);
    return r.markerRemoved && r.taskbarsVisible ? 0 : 1;
}

// Test profile only: shorten the login-check waits (VELORA_TEST_LOGIN_<name>_MS).
DWORD LoginMs(const wchar_t* testVar, DWORD production) {
    if (!CurrentProfile().IsTest()) return production;
    wchar_t buf[16] = {};
    if (!GetEnvironmentVariableW(testVar, buf, 16)) return production;
    return static_cast<DWORD>(_wtoi(buf));
}

int LoginCheck() {
    if (!DirtyMarkerExists(DirtyMarkerPath())) return 0;
    log::Warn("login check: dirty marker found");
    const DWORD initialWaitMs = LoginMs(L"VELORA_TEST_LOGIN_WAIT_MS", 10000);
    const DWORD retryMs = LoginMs(L"VELORA_TEST_LOGIN_RETRY_MS", 5000);
    const DWORD giveUpMs = LoginMs(L"VELORA_TEST_LOGIN_MAX_MS", 120000);

    HWND window = MakeWindow();  // receives TaskbarCreated while we wait
    g_taskbarCreated = RegisterWindowMessageW(CurrentProfile().taskbarCreatedMessage.c_str());

    // Explorer may still be starting; give it a few seconds to create its tray.
    ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < initialWaitMs && sw::FindTrayWindows().empty()) Sleep(50);

    sw::RestoreOptions o;
    o.terminateShell = true;
    o.requireTaskbar = true;
    o.reason = "login restore";
    auto r = sw::RestoreWindowsDesktop(o);

    // No taskbar yet: keep the marker, retry on TaskbarCreated or every retryMs, up to giveUpMs.
    ULONGLONG start = GetTickCount64(), nextTry = start + retryMs;
    while (r.pending && GetTickCount64() - start < giveUpMs) {
        ULONGLONG now = GetTickCount64();
        DWORD wait = nextTry > now ? static_cast<DWORD>(std::min<ULONGLONG>(nextTry - now, 250)) : 0;
        MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!DirtyMarkerExists(DirtyMarkerPath())) {
            log::Info("login check: marker removed by someone else; done");
            r.pending = false;
            r.markerRemoved = true;
            r.taskbarsVisible = true;
            break;
        }
        if (g_retryNow || GetTickCount64() >= nextTry) {
            g_retryNow = false;
            nextTry = GetTickCount64() + retryMs;
            log::Info("login check: retrying restore");
            r = sw::RestoreWindowsDesktop(o);
        }
    }
    if (window) DestroyWindow(window);
    if (r.pending) {
        log::Error("login check: still pending after " + std::to_string(giveUpMs) + " ms; marker kept for the next attempt");
        return kExitPending;
    }
    return r.markerRemoved && r.taskbarsVisible ? 0 : 1;
}

// Test hook (test profile only, like the shell's debug_hang): a hello is rejected
// while this file exists, to exercise the shell's "watchdog restart failed" path.
bool TestRejectsHello() {
    if (!CurrentProfile().IsTest()) return false;
    wchar_t path[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"VELORA_TEST_WATCHDOG_REJECT_HELLO_FILE", path, MAX_PATH)) return false;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

std::int64_t PayloadPid(const json::Value& m) {
    const auto* p = m.Find("payload");
    const auto* pid = p ? p->Find("pid") : nullptr;
    return pid ? pid->AsInt(-1) : -1;
}

int Watch(DWORD shellPid) {
    auto state = LoadState(StatePath());

    HANDLE shell = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shellPid);
    if (!shell) {
        // Never armed, so nothing was hidden on this watchdog's strength: do not restore on a guess.
        log::Error("cannot open shell process; exiting without touching anything");
        return 1;
    }
    if (state && state->shellPid == shellPid && !sw::IsRecordedShell(shell, *state)) {
        log::Error("pid " + std::to_string(shellPid) + " is not the recorded shell; refusing to watch it");
        CloseHandle(shell);
        return 1;
    }

    ipc::PipeServer server;
    bool armed = false;     // hello accepted, not yet disarmed
    bool disarmed = false;  // shell is leaving Velora Mode: answer nothing new
    bool hotkeyOk = false;  // emergency hotkey registered (set before the first message is pumped)
    ULONGLONG lastBeat = GetTickCount64();
    const ULONGLONG started = lastBeat;

    if (!server.Start(WatchdogPipeName(), [&](std::string_view raw) -> std::string {
            auto m = json::Parse(raw);
            if (!m || !m->IsObject()) return {};
            const auto* t = m->Find("type");
            const auto* k = m->Find("kind");
            if (!t || !k) return {};
            const std::string& type = t->AsString();
            const std::int64_t id = m->Find("id") ? m->Find("id")->AsInt(-1) : -1;
            auto reply = [&](int status, const std::string& error = {}, const json::Value& p = {}) {
                return ipc::MakeMessage("response", type.c_str(), id, p, status, error);
            };
            if (type == ipc::msg::kHeartbeat) {
                if (armed) lastBeat = GetTickCount64();
                return {};
            }
            if (type == ipc::msg::kExit) {  // legacy goodbye event: same meaning as disarm
                if (armed) disarmed = true;
                armed = false;
                return {};
            }
            if (type == ipc::msg::kDisarm && k->AsString() == "request") {
                if (PayloadPid(*m) != static_cast<std::int64_t>(shellPid)) return reply(400, "pid does not match the watched shell");
                disarmed = true;
                armed = false;
                return reply(0);
            }
            if (type == ipc::msg::kHello && k->AsString() == "request") {
                // A goodbye/disarm always wins over a later hello: never arm a dying watchdog.
                if (disarmed) return reply(500, "watchdog is shutting down");
                if (PayloadPid(*m) != static_cast<std::int64_t>(shellPid)) return reply(400, "hello pid does not match the watched shell");
                if (TestRejectsHello()) return reply(500, "hello rejected by test hook");
                armed = true;
                lastBeat = GetTickCount64();
                json::Value p;
                p.Set("pid", static_cast<std::int64_t>(GetCurrentProcessId()));
                p.Set("protocol", static_cast<int>(ipc::kProtocolVersion));
                p.Set("hotkey", hotkeyOk);  // false: the shell must not enter Velora Mode
                return reply(0, {}, p);
            }
            return {};
        })) {
        log::Warn("another watchdog is already running; exiting");
        CloseHandle(shell);
        return 0;
    }

    HWND window = MakeWindow();
    // Emergency hotkey lives here, not in the shell, so it works with a frozen shell.
    // A previous watchdog that has just exited can still hold the hotkey for a few ms while the
    // system tears down its window: retry briefly before treating it as taken by someone else.
    for (int attempt = 0; window && !hotkeyOk && attempt < 20; ++attempt) {
        hotkeyOk = RegisterHotKey(window, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_ESCAPE) != FALSE;
        if (hotkeyOk || GetLastError() != ERROR_HOTKEY_ALREADY_REGISTERED) break;
        Sleep(50);
    }
    if (!hotkeyOk)
        log::Warn("emergency hotkey Ctrl+Alt+Shift+Esc could not be registered: error " + std::to_string(GetLastError()));

    log::Info("watching shell pid " + std::to_string(shellPid));

    // Restore + terminate only when armed AND the marker exists (CLAUDE.md §4).
    auto shouldRecover = [&] { return armed && DirtyMarkerExists(DirtyMarkerPath()); };

    int exitCode = 0;
    for (;;) {
        std::vector<HANDLE> handles = server.WaitHandles();
        handles.push_back(shell);
        // Sleep until the nearest deadline (heartbeat timeout / startup grace), not on a fixed tick.
        ULONGLONG due = armed ? lastBeat + ipc::kHeartbeatTimeoutMs : started + kStartupGraceMs;
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

        if (disarmed) {
            // The ack was written inside Pump(); the pipe buffers it, so closing now cannot lose it.
            log::Info("shell disarmed the watchdog; exiting");
            break;
        }
        if (g_emergency) {
            g_emergency = false;
            if (shouldRecover()) {
                log::Warn("emergency hotkey pressed");
                exitCode = Restore("emergency hotkey", true);
                break;
            }
            log::Warn("emergency hotkey pressed but the watchdog is not armed with a dirty marker; ignoring");
        }
        if (r == WAIT_OBJECT_0 + handles.size() - 1 || WaitForSingleObject(shell, 0) == WAIT_OBJECT_0) {
            if (shouldRecover()) {
                log::Warn("shell died with the dirty marker set (crash)");
                exitCode = Restore("shell crashed", true);
            } else {
                log::Info("shell exited; not armed or no dirty marker: nothing to restore");
            }
            break;
        }
        ULONGLONG now = GetTickCount64();
        if (armed && now - lastBeat >= ipc::kHeartbeatTimeoutMs) {
            if (shouldRecover()) {
                log::Error("shell heartbeat timed out (" + std::to_string(now - lastBeat) + " ms); restoring");
                exitCode = Restore("shell hung", true);
            } else {
                log::Info("heartbeats stopped but there is no dirty marker: exiting quietly");
            }
            break;
        }
        if (!armed && now - started >= kStartupGraceMs) {
            // Never armed: nothing was hidden on this watchdog's strength. Do not restore, do not terminate.
            log::Warn("shell never said hello (or hello was rejected); exiting quietly");
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
