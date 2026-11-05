// velora-shell: resident process. Day 2 scope: switching (enter/exit/recovery
// hooks), IPC endpoint and the heartbeat to the watchdog. Dock, menu bar and the
// other modules plug in here on their own branches.
#include "core/win.h"

#include <ShellScalingApi.h>
#include <shellapi.h>

#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/contracts/ipc.h"
#include "core/debounce.h"
#include "core/event_bus.h"
#include "core/ipc_pipe.h"
#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/settings_file.h"
#include "core/state_store.h"
#include "core/text.h"
#include "apps/app_index.h"
#include "apps/apps_grid.h"
#include "apps/files_launch.h"
#include "dock/dock.h"
#include "menubar/menubar.h"
#include "control/control_center.h"
#include "overview/overview.h"
#include "search/search_core.h"
#include "search/search_hotkey.h"
#include "search/search_panel.h"
#include "switch/restore.h"
#include "theming/window_theme.h"
#include "switch/switch_manager.h"
#include "ui/ui_common.h"
#include "wallpaper/wallpaper.h"
#include "windows/window_tracker.h"

namespace {

using namespace velora;

constexpr UINT_PTR kTimerTick = 1;          // heartbeat + reconcile, 1 Hz
constexpr UINT_PTR kTimerRehideEarly = 2;   // after TaskbarCreated
constexpr UINT_PTR kTimerRehideLate = 3;
constexpr UINT_PTR kTimerHotCorner = 6;  // polls the pointer for the Overview hot corner (only while enabled)
constexpr UINT_PTR kTimerTrim = 5;  // memory give-back, a moment after the Apps panel closed
constexpr UINT_PTR kTimerTaskbarTrailing = 4;  // trailing run of a TaskbarCreated burst

UINT g_taskbarCreated = 0;
sw::SwitchManager* g_manager = nullptr;
HWND g_window = nullptr;
apps::AppsGrid* g_grid = nullptr;
search::SearchPanel* g_search = nullptr;
constexpr int kHotkeySearch = 1;  // RegisterHotKey id of the Search hotkey
constexpr int kHotkeyOverview = 2;  // RegisterHotKey id of the Overview hotkey
dock::Dock* g_dock = nullptr;
menubar::MenuBar* g_menuBar = nullptr;
overview::Overview* g_overview = nullptr;
control::ControlCenter* g_control = nullptr;
overview::HotCorner g_hotCorner;
EventBus* g_bus = nullptr;
constexpr UINT kMsgTrim = WM_APP + 43;  // the Apps panel closed: schedule the memory give-back
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
    if (t == ipc::msg::kSettingsChanged) {  // the Settings app wrote settings.json: modules re-read what they use
        if (g_bus) g_bus->Post(EventType::SettingsChanged);
        return Reply(req, 0, self);
    }
    if (t == ipc::msg::kOpenControl || t == ipc::msg::kCloseControl) {
        if (!g_control) return Reply(req, 501, {}, "control center unavailable");
        if (t == ipc::msg::kOpenControl) g_control->Show();
        else g_control->Hide();
        json::Value p;
        p.Set("visible", g_control->Visible());
        return Reply(req, 0, p);
    }
    if (t == ipc::msg::kOpenOverview || t == ipc::msg::kCloseOverview) {
        if (!g_overview) return Reply(req, 501, {}, "overview unavailable");
        if (t == ipc::msg::kOpenOverview) g_overview->Show();
        else g_overview->Hide();
        json::Value p;
        p.Set("visible", g_overview->Visible());
        return Reply(req, 0, p);
    }
    if (t == ipc::msg::kOpenApps || t == ipc::msg::kCloseApps) {
        // The pipe is pumped on the UI thread, so the panel can be driven directly.
        if (!g_grid) return Reply(req, 501, {}, "apps panel unavailable");
        if (t == ipc::msg::kOpenApps) { if (!g_grid->Visible()) g_grid->Show(); }
        else g_grid->Hide();
        json::Value p;
        p.Set("visible", g_grid->Visible());
        return Reply(req, 0, p);
    }
    if (t == ipc::msg::kOpenSearch || t == ipc::msg::kCloseSearch) {
        if (!g_search) return Reply(req, 501, {}, "search panel unavailable");
        if (t == ipc::msg::kOpenSearch) {
            if (!g_search->Visible()) g_search->Show();
            const auto* p = req.Find("payload");
            const auto* q = p ? p->Find("query") : nullptr;
            if (q && q->IsString() && g_search->Visible()) g_search->SetQuery(Utf8ToWide(q->AsString()));
        } else {
            g_search->Hide();
        }
        json::Value p;
        p.Set("visible", g_search->Visible());
        return Reply(req, 0, p);
    }
    if (t == ipc::msg::kOpenFiles || t == ipc::msg::kOpenApp) {
        // Files is an on-demand app: start it (or hand the folder to the window that is open).
        const auto* p = req.Find("payload");
        if (t == ipc::msg::kOpenApp) {
            const auto* app = p ? p->Find("app") : nullptr;
            if (!app || !app->IsString() || app->AsString() != "files") return Reply(req, 501, {}, "only the files app can be opened");
        }
        auto text = [&](const char* key) {
            const auto* v = p ? p->Find(key) : nullptr;
            return v && v->IsString() ? Utf8ToWide(v->AsString()) : std::wstring{};
        };
        std::wstring error;
        if (!apps::LaunchFilesApp(text("path"), text("select"), &error)) return Reply(req, 501, {}, WideToUtf8(error));
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
    if (msg == kMsgTrim) {
        SetTimer(h, kTimerTrim, 1500, nullptr);  // after the close settled; a reopen in between restarts the wait
        return 0;
    }
    if (msg == ui::kMsgIconsLoaded) {
        ui::IconStore::Get().Dispatch();
        return 0;
    }
    switch (msg) {
        case WM_HOTKEY:
            if (wp == static_cast<WPARAM>(kHotkeySearch) && g_search) g_search->Toggle();
            if (wp == static_cast<WPARAM>(kHotkeyOverview) && g_overview) g_overview->Toggle();
            return 0;
        case WM_TIMER:
            if (wp == kTimerTrim) {
                KillTimer(h, wp);
                if ((g_grid && g_grid->Visible()) || (g_search && g_search->Visible())) return 0;  // reopened meanwhile; the next close schedules again
                // Keep only what the Dock draws; the launcher's icons are decoded again on the next open.
                const size_t freed = g_dock ? ui::IconStore::Get().Trim(g_dock->IconSources()) : 0;
                if (g_dock) g_dock->Trim();
                if (g_menuBar) g_menuBar->Trim();
                log::Info("icon cache trimmed: " + std::to_string(freed >> 10) + " KB of decoded pixels released");
                ui::TrimProcessMemory("apps panel closed");
                return 0;
            }
            if (wp == kTimerHotCorner) {
                if (!g_overview || g_overview->Visible()) return 0;
                POINT pt{};
                bool inCorner = false;
                if (GetCursorPos(&pt)) {
                    MONITORINFO mi{sizeof mi};
                    const HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
                    inCorner = mon && GetMonitorInfoW(mon, &mi) && pt.x <= mi.rcMonitor.left + 1 && pt.y <= mi.rcMonitor.top + 1;
                }
                if (g_hotCorner.Feed(inCorner, GetTickCount64())) g_overview->Show();  // top-left corner of any monitor
                return 0;
            }
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
            if (msg == WM_SETTINGCHANGE && lp && g_bus && std::wstring_view(reinterpret_cast<const wchar_t*>(lp)) == L"ImmersiveColorSet")
                g_bus->Post(EventType::ThemeChanged);
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

// `velora-shell --open-apps` / `--close-apps` / `--open-files [folder]`: drive the running shell (tests, no mouse).
int SendCommand(const char* type, const json::Value& payload = {}) {
    std::string resp;
    if (!ipc::Call(ShellPipeName(), ipc::MakeMessage("request", type, 1, payload), &resp, 3000)) return 1;
    auto r = json::Parse(resp);
    const json::Value* s = r ? r->Find("status") : nullptr;
    return s && s->AsInt(-1) == 0 ? 0 : 5;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    log::Init("velora-shell");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    bool exitRequest = false, recoverOnly = false;
    const char* appsCommand = nullptr;
    json::Value commandPayload;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--exit") exitRequest = true;
            else if (std::wstring(argv[i]) == L"--recover-only") recoverOnly = true;
            else if (std::wstring(argv[i]) == L"--open-apps") appsCommand = ipc::msg::kOpenApps;
            else if (std::wstring(argv[i]) == L"--close-apps") appsCommand = ipc::msg::kCloseApps;
            else if (std::wstring(argv[i]) == L"--open-control") appsCommand = ipc::msg::kOpenControl;
            else if (std::wstring(argv[i]) == L"--close-control") appsCommand = ipc::msg::kCloseControl;
            else if (std::wstring(argv[i]) == L"--open-overview") appsCommand = ipc::msg::kOpenOverview;
            else if (std::wstring(argv[i]) == L"--close-overview") appsCommand = ipc::msg::kCloseOverview;
            else if (std::wstring(argv[i]) == L"--close-search") appsCommand = ipc::msg::kCloseSearch;
            else if (std::wstring(argv[i]) == L"--open-search") {
                appsCommand = ipc::msg::kOpenSearch;
                if (i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0) commandPayload.Set("query", WideToUtf8(argv[++i]));
            }
            else if (std::wstring(argv[i]) == L"--open-files") {
                appsCommand = ipc::msg::kOpenFiles;
                if (i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0) commandPayload.Set("path", WideToUtf8(argv[++i]));
            }
        }
        LocalFree(argv);
    }
    if (exitRequest) return ExitRunningShell();
    if (appsCommand) return SendCommand(appsCommand, commandPayload);

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
    g_bus = &bus;
    sw::SwitchManager manager(bus, instance);
    manager.SetFatalHandler([] { PostMessageW(g_window, WM_CLOSE, 0, 0); });
    g_manager = &manager;
#ifdef VELORA_TESTING
    if (CurrentProfile().IsTest()) {
        // A slow Enter between the watchdog's hello and the first heartbeat (cold-start stand-in), see switch_gate.
        wchar_t v[16] = {};
        if (GetEnvironmentVariableW(L"VELORA_TEST_ENTER_DELAY_MS", v, 16))
            manager.SetAfterWatchdogHook([ms = static_cast<DWORD>(_wtoi(v))] { Sleep(ms); });
    }
#endif
    ui::IconStore::Get().SetNotifyWindow(g_window);

    win::WindowTracker tracker(bus);
    // Window theming through documented DWM attributes (no injection). Never in the test profile: tests must not touch the
    // real desktop's windows. Everything it changed is restored on exit, and by recovery from theming_backup.json.
    theming::WindowThemer themer;
    bool themingActive = false;
    auto themeWindow = [&](HWND h, bool elevated) {
        if (themingActive && h) themer.Apply(h, elevated, tracker.FullscreenWindow() == h);
    };
    auto themeAll = [&] {
        for (const auto& [h, w] : tracker.Windows()) themeWindow(h, !w.controllable);
    };
    for (EventType t : {EventType::WindowCreated, EventType::WindowUncloaked, EventType::WindowRestored, EventType::WindowActivated})
        bus.Subscribe(t, [&](const Event& e) { themeWindow(reinterpret_cast<HWND>(static_cast<UINT_PTR>(e.window.hwnd)), e.window.elevated); });
    bus.Subscribe(EventType::WindowDestroyed, [&](const Event& e) { themer.Forget(reinterpret_cast<HWND>(static_cast<UINT_PTR>(e.window.hwnd))); });
    bus.Subscribe(EventType::ThemeChanged, [&](const Event&) {
        if (themingActive) themer.Configure(theming::LoadConfig(), ui::SystemTheme() == tokens::Theme::Dark);
    });
    bus.Subscribe(EventType::SettingsChanged, [&](const Event&) {
        if (!themingActive) return;
        themer.Configure(theming::LoadConfig(), ui::SystemTheme() == tokens::Theme::Dark);
        themeAll();  // windows that became eligible (setting switched on, exclusion removed)
    });
    menubar::MenuBar::Actions menuActions;
    menuActions.exitVelora = [] { PostMessageW(g_window, WM_CLOSE, 0, 0); };
    menuActions.openSettings = [] {
        // Velora Settings (on demand, single instance). Windows' own Settings only when the app is missing (a broken install).
        const auto exe = ExeDir() / L"velora-settings.exe";
        std::error_code ec;
        if (std::filesystem::exists(exe, ec)) ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else ShellExecuteW(nullptr, L"open", L"ms-settings:", nullptr, nullptr, SW_SHOWNORMAL);
    };
    apps::AppsGrid grid(instance);
    g_grid = &grid;
    menuActions.openApps = [] { if (g_grid) g_grid->Toggle(); };
    search::SearchPanel searchPanel(instance);
    g_search = &searchPanel;
    menuActions.openSearch = [] { if (g_search) g_search->Toggle(); };
    menuActions.openOverview = [] { if (g_overview) g_overview->Toggle(); };
    menuActions.openControl = [] { if (g_control) g_control->Toggle(); };
    // Cached index first (instant), then a background refresh. Skipped in the test profile: the gate
    // measures switching, and a real-machine scan per spawned shell would only add noise.
    apps::AppIndex::Get().LoadCache();
    if (!CurrentProfile().IsTest()) apps::AppIndex::Get().StartBackground(g_window, kMsgAppsChanged);
    menubar::MenuBar menuBar(bus, tracker, menuActions);
    manager.SetMenuBarOptions(menuBar.Options());
    dock::Dock::Actions dockActions;
    dockActions.openApps = menuActions.openApps;
    dock::Dock dock(bus, tracker, dockActions);
    g_dock = &dock;
    g_menuBar = &menuBar;
    grid.SetOnHidden([] { PostMessageW(g_window, kMsgTrim, 0, 0); });
    searchPanel.SetOnHidden([] { PostMessageW(g_window, kMsgTrim, 0, 0); });
    // The global hotkey belongs to the real session only: the automated gate must not grab keys on the developer's machine.
    auto registerSearchHotkey = [&] {
        search::UnregisterSearchHotkey(g_window, kHotkeySearch);
        const search::HotkeyChoice hk = search::RegisterSearchHotkey(g_window, kHotkeySearch, search::ConfiguredHotkey());
        searchPanel.SetHotkeyHint(hk.registered ? search::FormatHotkey(hk.key) : std::wstring{});
    };
    if (!CurrentProfile().IsTest()) {
        registerSearchHotkey();
        // Settings changed the hotkey: apply it now.
        bus.Subscribe(EventType::SettingsChanged, [&](const Event&) { registerSearchHotkey(); });
    }
    overview::Overview overviewPanel(instance, tracker);
    g_overview = &overviewPanel;
    control::ControlCenter controlCenter(instance);
    g_control = &controlCenter;
    controlCenter.SetOnHidden([] { PostMessageW(g_window, kMsgTrim, 0, 0); });
    overviewPanel.SetOnHidden([] { PostMessageW(g_window, kMsgTrim, 0, 0); });
    // Overview: hotkey (settings.json overview.hotkey, default Ctrl+Alt+Up) and the optional hot corner (overview.hotCorner).
    auto configureOverview = [&] {
        UnregisterHotKey(g_window, kHotkeyOverview);
        KillTimer(g_window, kTimerHotCorner);
        if (CurrentProfile().IsTest()) return;
        const std::string cfg = SettingString("overview.hotkey", "Ctrl+Alt+Up");
        search::Hotkey k;
        if (cfg != "none" && search::ParseHotkey(Utf8ToWide(cfg), &k)) {
            if (RegisterHotKey(g_window, kHotkeyOverview, k.mods | MOD_NOREPEAT, k.vk)) log::Info("overview: hotkey " + cfg + " registered");
            else log::Info("overview: hotkey " + cfg + " is not available (error " + std::to_string(GetLastError()) + ")");
        }
        if (SettingBool("overview.hotCorner", false)) SetTimer(g_window, kTimerHotCorner, 100, nullptr);
    };
    configureOverview();
    bus.Subscribe(EventType::SettingsChanged, [&](const Event&) { configureOverview(); });
    manager.SetDockOptions(dock.Options(), dock.ReservedDip());
    bus.Subscribe(EventType::Entered, [&](const Event&) {
        menuBar.Attach(manager.MenuBarBar().Hwnd());
        dock.Attach(&manager.DockBar());
        // Original Velora wallpaper per monitor (reversible: the original is backed up first and restored on
        // exit and by crash recovery). Never in the test profile: tests must not touch the real desktop.
        if (!CurrentProfile().IsTest() && wallpaper::Enabled()) wallpaper::ApplyVelora(ui::SystemTheme() == tokens::Theme::Dark);
        if (!CurrentProfile().IsTest()) {
            themingActive = true;
            themer.Configure(theming::LoadConfig(), ui::SystemTheme() == tokens::Theme::Dark);
        }
    });
    bus.Subscribe(EventType::Exiting, [&](const Event&) {
        overviewPanel.Hide();
        controlCenter.Hide();
        themingActive = false;
        themer.RestoreAll();
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
    themeAll();  // windows that already existed when Velora started

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
    g_bus = nullptr;
    g_dock = nullptr;
    g_menuBar = nullptr;
    grid.Hide();
    g_grid = nullptr;
    search::UnregisterSearchHotkey(g_window, kHotkeySearch);
    UnregisterHotKey(g_window, kHotkeyOverview);
    KillTimer(g_window, kTimerHotCorner);
    overviewPanel.Hide();
    g_overview = nullptr;
    controlCenter.Hide();
    g_control = nullptr;
    searchPanel.Hide();
    g_search = nullptr;
    ui::IconStore::Get().Shutdown();
    DestroyWindow(g_window);
    return 0;
}
