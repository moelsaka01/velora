// velora-shell: resident process. Day 2 scope: switching (enter/exit/recovery
// hooks), IPC endpoint and the heartbeat to the watchdog. Dock, menu bar and the
// other modules plug in here on their own branches.
#include "core/win.h"

#include <ShellScalingApi.h>
#include <shellapi.h>

#include <tlhelp32.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/contracts/ipc.h"
#include "core/debounce.h"
#include "core/event_bus.h"
#include "core/ipc_pipe.h"
#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/state_store.h"
#include "core/text.h"
#include "apps/app_index.h"
#include "apps/apps_grid.h"
#include "dock/dock.h"
#include "menubar/menubar.h"
#include "switch/restore.h"
#include "switch/switch_manager.h"
#include "ui/ui_common.h"
#include "wallpaper/wallpaper.h"
#include "windows/window_tracker.h"

namespace {

using namespace velora;

constexpr UINT_PTR kTimerTick = 1;          // heartbeat + reconcile, 1 Hz
constexpr UINT_PTR kTimerRehideEarly = 2;   // after TaskbarCreated
constexpr UINT_PTR kTimerRehideLate = 3;
constexpr UINT_PTR kTimerTaskbarTrailing = 4;  // trailing run of a TaskbarCreated burst

UINT g_taskbarCreated = 0;
sw::SwitchManager* g_manager = nullptr;
HWND g_window = nullptr;
apps::AppsGrid* g_grid = nullptr;
constexpr UINT kMsgAppsChanged = WM_APP + 42;  // the app index was refreshed on its worker thread
BurstDebounce g_taskbarDebounce(500);  // Explorer may broadcast TaskbarCreated several times in a burst

std::string Reply(const json::Value& req, int status, const json::Value& payload = {}, const std::string& error = {}) {
    std::string type = "hello";
    if (const auto* t = req.Find("type"); t && t->IsString()) type = t->AsString();
    std::int64_t id = -1;
    if (const auto* i = req.Find("id")) id = i->AsInt(-1);
    return ipc::MakeMessage("response", type.c_str(), id, payload, status, error);
}

std::string HandleRequest(std::string_view raw) {
    auto doc = json::Parse(raw);
    if (!doc || !doc->IsObject()) return Reply(json::Value{}, 400, {}, "malformed json");
    const json::Value& req = *doc;

    const auto* v = req.Find("v");
    if (!v || v->AsInt() != ipc::kProtocolVersion) return Reply(req, 505, {}, "unsupported protocol version");
    const auto* kind = req.Find("kind");
    const auto* type = req.Find("type");
    if (!kind || !type || !type->IsString()) return Reply(req, 400, {}, "missing kind/type");
    if (kind->AsString() == "event") return {};  // events get no response
    if (kind->AsString() != "request") return Reply(req, 400, {}, "bad kind");

    const std::string& t = type->AsString();
    json::Value self;
    self.Set("pid", static_cast<std::int64_t>(GetCurrentProcessId()));

    if (t == ipc::msg::kHello) {
        self.Set("protocol", static_cast<int>(ipc::kProtocolVersion));
        return Reply(req, 0, self);
    }
    if (t == ipc::msg::kGetState) return Reply(req, 0, g_manager->Status());
    if (t == ipc::msg::kEnter) return g_manager->Enter() ? Reply(req, 0, g_manager->Status()) : Reply(req, 500, {}, "enter failed");
    if (t == ipc::msg::kExit || t == ipc::msg::kRestore) {
        PostMessageW(g_window, WM_CLOSE, 0, 0);  // exit runs after this reply is sent
        return Reply(req, 0, self);
    }
#ifdef VELORA_TESTING
    if (t == "debug_hang" && CurrentProfile().IsTest()) {
        // Test hook (never in production profile): freeze the UI thread so the
        // watchdog's heartbeat timeout can be exercised.
        const auto* p = req.Find("payload");
        const auto* ms = p ? p->Find("ms") : nullptr;
        Sleep(static_cast<DWORD>(ms ? ms->AsInt(60000) : 60000));
        return Reply(req, 0);
    }
#endif
    return Reply(req, 501, {}, "unsupported request type");
}

void HandleTaskbarCreated(HWND h) {
    if (g_manager) g_manager->OnTaskbarCreated();
    // Explorer keeps creating things for a moment after the broadcast.
    SetTimer(h, kTimerRehideEarly, 250, nullptr);
    SetTimer(h, kTimerRehideLate, 1000, nullptr);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        ULONGLONG now = GetTickCount64();
        switch (g_taskbarDebounce.OnEvent(now)) {
            case BurstDebounce::Action::RunNow:
                HandleTaskbarCreated(h);
                break;
            case BurstDebounce::Action::Schedule:
                SetTimer(h, kTimerTaskbarTrailing,
                         static_cast<UINT>(std::max<std::uint64_t>(1, g_taskbarDebounce.TrailingDelayMs(now))), nullptr);
                break;
            case BurstDebounce::Action::Drop:
                break;
        }
        return 0;
    }
    if (msg == kMsgAppsChanged) {
        if (g_grid) g_grid->OnDataChanged();
        return 0;
    }
    if (msg == ui::kMsgIconsLoaded) {
        ui::IconStore::Get().Dispatch();
        return 0;
    }
    switch (msg) {
        case WM_TIMER:
            if (wp == kTimerTaskbarTrailing) {
                KillTimer(h, wp);
                if (g_taskbarDebounce.OnTimer(GetTickCount64())) HandleTaskbarCreated(h);
                return 0;
            }
            if (!g_manager) break;
            if (wp == kTimerTick) {
                g_manager->Heartbeat();
                g_manager->Reconcile();
            } else if (wp == kTimerRehideEarly || wp == kTimerRehideLate) {
                KillTimer(h, wp);
                g_manager->Reconcile();
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (g_manager) g_manager->Reconcile();
            break;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            // Log off / shutdown: leave Velora Mode cleanly so auto-hide is not persisted.
            if (wp && g_manager) g_manager->Exit();
            return 0;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Other resident Velora processes (shell, watchdog) besides this one. On-demand apps (files, settings)
// may legitimately outlive the shell and are not counted.
std::vector<std::wstring> OtherResidentProcesses() {
    std::vector<std::wstring> found;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == GetCurrentProcessId()) continue;
        std::wstring name = pe.szExeFile;
        if (_wcsicmp(name.c_str(), L"velora-shell.exe") == 0 || _wcsicmp(name.c_str(), L"velora-watchdog.exe") == 0)
            found.push_back(name + L":" + std::to_wstring(pe.th32ProcessID));
    }
    CloseHandle(snap);
    return found;
}

// `velora-shell --exit`: ask the running shell to leave Velora Mode and wait for it. Exit check: no
// resident Velora process may remain afterwards (a leftover watchdog or shell is a defect, exit code 4).
int ExitRunningShell() {
    std::string resp;
    if (!ipc::Call(ShellPipeName(), ipc::MakeMessage("request", ipc::msg::kExit, 1), &resp, 3000)) return 1;
    auto r = json::Parse(resp);
    const json::Value* payload = r ? r->Find("payload") : nullptr;
    const json::Value* pid = payload ? payload->Find("pid") : nullptr;
    if (pid) {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid->AsInt()));
        if (h) {
            WaitForSingleObject(h, 10000);
            CloseHandle(h);
        }
    }
    std::vector<std::wstring> left;
    for (int i = 0; i < 20; ++i) {  // the watchdog disarms and exits just after the shell; allow 2 s
        left = OtherResidentProcesses();
        if (left.empty()) break;
        Sleep(100);
    }
    if (!left.empty()) {
        std::wstring names;
        for (const auto& n : left) names += (names.empty() ? L"" : L", ") + n;
        log::Error("exit check: Velora process still running after exit: " + WideToUtf8(names));
        return 4;
    }
    log::Info("exit check: no resident Velora process remains");
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    log::Init("velora-shell");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    bool exitRequest = false, recoverOnly = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--exit") exitRequest = true;
            else if (std::wstring(argv[i]) == L"--recover-only") recoverOnly = true;
        }
        LocalFree(argv);
    }
    if (exitRequest) return ExitRunningShell();

    if (CurrentProfile().IsTest() && DataDir().empty()) {
        log::Error("test profile without VELORA_DATA_DIR; refusing to start");
        return 3;
    }

    // Single instance: a named mutex (per user session, per test profile) turns a second launch away
    // before it touches anything. The pipe check below stays as the second line of defence.
    // --recover-only is exempt: it must still be able to run beside a live shell's startup.
    HANDLE instanceMutex = nullptr;
    if (!recoverOnly) {
        std::wstring name = L"Local\\Velora.Shell.SingleInstance";
        if (CurrentProfile().IsTest()) name += L"." + CurrentProfile().id;
        instanceMutex = CreateMutexW(nullptr, FALSE, name.c_str());
        if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            log::Warn("second velora-shell instance refused (mutex held); exiting");
            CloseHandle(instanceMutex);
            return 0;
        }
    }

    // Single instance: the first pipe instance is ours or nobody's.
    ipc::PipeServer server;
    if (!recoverOnly && !server.Start(ShellPipeName(), HandleRequest)) {
        log::Warn("velora-shell already running");
        return 0;
    }

    // A dirty marker means a previous session did not exit cleanly: restore
    // Windows before anything else (CLAUDE.md §4).
    if (DirtyMarkerExists(DirtyMarkerPath())) {
        log::Warn("startup recovery: dirty marker found; restoring Windows before anything else");
        sw::RestoreOptions o;
        o.terminateShell = true;
        o.reason = "startup recovery";
        sw::RestoreWindowsDesktop(o);
    }
    if (recoverOnly) return 0;

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"VeloraShellMain";
    RegisterClassExW(&wc);
    // A hidden *top-level* window: broadcast messages (TaskbarCreated) skip message-only windows.
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Velora", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                               instance, nullptr);
    if (!g_window) {
        log::Error("cannot create main window");
        return 2;
    }
    g_taskbarCreated = RegisterWindowMessageW(CurrentProfile().taskbarCreatedMessage.c_str());

    EventBus bus;
    sw::SwitchManager manager(bus, instance);
    manager.SetFatalHandler([] { PostMessageW(g_window, WM_CLOSE, 0, 0); });
    g_manager = &manager;
    ui::IconStore::Get().SetNotifyWindow(g_window);

    win::WindowTracker tracker(bus);
    menubar::MenuBar::Actions menuActions;
    menuActions.exitVelora = [] { PostMessageW(g_window, WM_CLOSE, 0, 0); };
    menuActions.openSettings = [] {
        // velora-settings.exe is a stub until Settings reaches G1 (docs/PROGRESS.md): open Windows Settings meanwhile.
        // Switch back to ExeDir() / L"velora-settings.exe" when the app ships.
        ShellExecuteW(nullptr, L"open", L"ms-settings:", nullptr, nullptr, SW_SHOWNORMAL);
    };
    apps::AppsGrid grid(instance);
    g_grid = &grid;
    menuActions.openApps = [] { if (g_grid) g_grid->Toggle(); };
    // Cached index first (instant), then a background refresh. Skipped in the test profile: the gate
    // measures switching, and a real-machine scan per spawned shell would only add noise.
    apps::AppIndex::Get().LoadCache();
    if (!CurrentProfile().IsTest()) apps::AppIndex::Get().StartBackground(g_window, kMsgAppsChanged);
    menubar::MenuBar menuBar(bus, tracker, menuActions);
    manager.SetMenuBarOptions(menuBar.Options());
    dock::Dock::Actions dockActions;
    dockActions.openApps = menuActions.openApps;
    dock::Dock dock(bus, tracker, dockActions);
    manager.SetDockOptions(dock.Options(), dock.ReservedDip());
    bus.Subscribe(EventType::Entered, [&](const Event&) {
        menuBar.Attach(manager.MenuBarBar().Hwnd());
        dock.Attach(&manager.DockBar());
        // Original Velora wallpaper per monitor (reversible: the original is backed up first and restored on
        // exit and by crash recovery). Never in the test profile: tests must not touch the real desktop.
        if (!CurrentProfile().IsTest() && wallpaper::Enabled()) wallpaper::ApplyVelora(ui::SystemTheme() == tokens::Theme::Dark);
    });
    bus.Subscribe(EventType::Exiting, [&](const Event&) {
        dock.Detach();
        menuBar.Detach();
    });

    if (!manager.Enter()) {
        log::Error("enter failed; Windows left untouched");
        if (!manager.LastError().empty() && !CurrentProfile().IsTest())
            MessageBoxW(nullptr, manager.LastError().c_str(), L"Velora", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        g_manager = nullptr;
        return 2;
    }
    SetTimer(g_window, kTimerTick, ipc::kHeartbeatIntervalMs, nullptr);
    tracker.Start();

    bool quit = false;
    while (!quit) {
        auto handles = server.WaitHandles();
        MsgWaitForMultipleObjectsEx(static_cast<DWORD>(handles.size()), handles.data(), INFINITE, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
        server.Pump();
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    KillTimer(g_window, kTimerTick);
    tracker.Stop();
    manager.Exit();
    g_manager = nullptr;
    grid.Hide();
    g_grid = nullptr;
    ui::IconStore::Get().Shutdown();
    DestroyWindow(g_window);
    return 0;
}
