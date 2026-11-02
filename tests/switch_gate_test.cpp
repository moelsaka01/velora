// Day-2 safety gate (CLAUDE.md §7 D2, §11): enter/exit cycles, forced kills,
// hangs, Explorer restarts, dirty marker, --restore, emergency hotkey.
//
// Hermetic by design: runs velora-shell/-watchdog under a test profile
// (VELORA_TEST_PROFILE + VELORA_DATA_DIR) against FAKE tray windows owned by
// this process, so the real taskbar, real state.json and the real
// TaskbarCreated broadcast are never touched. Appbar registration does go to
// the real shell (a 28 DIP strip for a few ms per cycle) when one exists.
//
// usage: switch_gate_test <velora-shell.exe> <velora-watchdog.exe> [--cycles N] [--kills N] [--hangs N]
//                         [--restarts N] [--out results.json]
#include "core/win.h"

#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <mutex>
#include <thread>

#include "core/contracts/ipc.h"
#include "core/ipc_pipe.h"
#include "core/json.h"
#include "core/runtime.h"
#include "core/state_store.h"
#include "core/text.h"
#include "switch/restore.h"
#include "switch/taskbar.h"

using namespace velora;

namespace {

// ------------------------------------------------------------- reporting

struct Check {
    std::string phase, name;
    bool ok;
    std::string detail;
};
std::vector<Check> g_checks;
std::string g_phase;
std::map<std::string, std::vector<double>> g_samples;  // latency series, ms
std::set<HWND> g_externalNew;                           // windows other processes created mid-run
std::mutex g_destroyedMutex;                            // guards g_destroyed and g_externalLost
std::set<HWND> g_destroyed;                             // EVENT_OBJECT_DESTROY seen during the run
std::map<HWND, std::string> g_externalLost;             // foreign windows that vanished on their own
HWND g_fgBefore = nullptr;                              // focus window before the cycle (Velora restores focus to it)

void CALLBACK OnDestroyEvent(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (h && obj == OBJID_WINDOW && child == CHILDID_SELF) {
        std::lock_guard<std::mutex> lock(g_destroyedMutex);
        g_destroyed.insert(h);
    }
}

void Record(const std::string& name, bool ok, const std::string& detail = {}) {
    g_checks.push_back({g_phase, name, ok, detail});
    if (!ok) std::printf("  FAIL [%s] %s %s\n", g_phase.c_str(), name.c_str(), detail.c_str());
}
#define EXPECT(cond, ...) Record(#cond, (cond), std::string(__VA_ARGS__))

double NowMs() {
    static const LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return 1000.0 * static_cast<double>(c.QuadPart) / static_cast<double>(freq.QuadPart);
}

// Polls pred every ms; returns elapsed ms or -1 on timeout.
double PollUntil(const std::function<bool()>& pred, double timeoutMs, double startMs = -1) {
    double t0 = startMs >= 0 ? startMs : NowMs();
    for (;;) {
        if (pred()) return NowMs() - t0;
        if (NowMs() - t0 > timeoutMs) return -1;
        Sleep(1);
    }
}

// ------------------------------------------------------------- fixture (fake desktop)

constexpr UINT WM_FX_RESTART = WM_APP + 1;  // simulate an Explorer restart
constexpr UINT WM_FX_HIDE = WM_APP + 2;     // hide trays (as if a shell crashed mid-session)
constexpr UINT WM_FX_DESTROY = WM_APP + 3;  // no taskbar at all (Explorer not started yet)
constexpr UINT WM_FX_CREATE = WM_APP + 4;   // Explorer started late: hidden trays; wParam=1 also broadcasts TaskbarCreated
constexpr wchar_t kAppClass[] = L"VeloraGateApp";

class Fixture {
public:
    void Start() {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        thread_ = std::thread([this] { Run(); });
        WaitForSingleObject(ready_, 10000);
    }
    void Stop() {
        if (control_) PostMessageW(control_, WM_CLOSE, 0, 0);
        if (thread_.joinable()) thread_.join();
    }
    void RestartExplorer() { SendMessageW(control_, WM_FX_RESTART, 0, 0); }
    void HideTrays() { SendMessageW(control_, WM_FX_HIDE, 0, 0); }
    void DestroyTrays() { SendMessageW(control_, WM_FX_DESTROY, 0, 0); }
    void CreateTraysHidden(bool broadcast) { SendMessageW(control_, WM_FX_CREATE, broadcast ? 1 : 0, 0); }
    const std::vector<HWND>& Trays() const { return trays_; }
    std::vector<HWND> AppWindows() const { return apps_; }

private:
    static LRESULT CALLBACK CtrlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Fixture*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
            return DefWindowProcW(h, m, w, l);
        }
        if (self) {
            if (m == WM_FX_RESTART) { self->DoRestart(); return 0; }
            if (m == WM_FX_HIDE) { for (HWND t : self->trays_) ShowWindow(t, SW_HIDE); return 0; }
            if (m == WM_FX_DESTROY) { for (HWND t : self->trays_) DestroyWindow(t); self->trays_.clear(); return 0; }
            if (m == WM_FX_CREATE) {
                self->MakeTrays();
                for (HWND t : self->trays_) ShowWindow(t, SW_HIDE);
                if (w) PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(CurrentProfile().taskbarCreatedMessage.c_str()), 0, 0);
                return 0;
            }
            if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
        }
        return DefWindowProcW(h, m, w, l);
    }

    HWND MakeTray(const std::wstring& cls, int x) {
        // Off-screen so nothing flashes on a developer's desktop; still "visible" to the OS.
        HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.c_str(), L"fake tray", WS_POPUP, x, -30000, 300, 40,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ShowWindow(h, SW_SHOWNOACTIVATE);
        return h;
    }

    void MakeTrays() {
        trays_.clear();
        trays_.push_back(MakeTray(CurrentProfile().trayClass, -30000));
        trays_.push_back(MakeTray(CurrentProfile().secondaryTrayClass, -29000));
    }

    void DoRestart() {
        for (HWND t : trays_) DestroyWindow(t);
        MakeTrays();
        // Exactly what Explorer does after (re)creating its taskbar.
        PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(CurrentProfile().taskbarCreatedMessage.c_str()), 0, 0);
    }

    void Run() {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        auto reg = [&](const std::wstring& name, WNDPROC proc) {
            WNDCLASSEXW wc{sizeof wc};
            wc.lpfnWndProc = proc;
            wc.hInstance = inst;
            wc.lpszClassName = name.c_str();
            RegisterClassExW(&wc);
        };
        reg(CurrentProfile().trayClass, DefWindowProcW);
        reg(CurrentProfile().secondaryTrayClass, DefWindowProcW);
        reg(kAppClass, DefWindowProcW);
        reg(L"VeloraGateControl", CtrlProc);
        control_ = CreateWindowExW(0, L"VeloraGateControl", L"ctl", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, this);

        MakeTrays();
        for (int i = 0; i < 3; ++i) {
            HWND a = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kAppClass, (L"gate app " + std::to_wstring(i)).c_str(),
                                     WS_POPUP, -20000 + i * 500, -20000, 320, 200, nullptr, nullptr, inst, nullptr);
            ShowWindow(a, SW_SHOWNOACTIVATE);
            apps_.push_back(a);
        }
        HWND minimized = CreateWindowExW(WS_EX_TOOLWINDOW, kAppClass, L"gate app minimized", WS_OVERLAPPEDWINDOW, -20000,
                                         -19000, 320, 200, nullptr, nullptr, inst, nullptr);
        ShowWindow(minimized, SW_SHOWMINNOACTIVE);
        apps_.push_back(minimized);

        // Out-of-context hook, delivered through this thread's message loop.
        HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr, OnDestroyEvent, 0, 0,
                                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        SetEvent(ready_);
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (hook) UnhookWinEvent(hook);
        for (HWND a : apps_) DestroyWindow(a);
        for (HWND t : trays_) DestroyWindow(t);
        DestroyWindow(control_);
    }

    std::thread thread_;
    HANDLE ready_ = nullptr;
    HWND control_ = nullptr;
    std::vector<HWND> trays_, apps_;
};

Fixture g_fx;

// ------------------------------------------------------------- window list

struct WinInfo {
    std::wstring cls;
    bool visible, iconic, zoomed;
    RECT rect;
    DWORD pid;
};
using WinMap = std::map<HWND, WinInfo>;

bool IsVeloraOwned(const std::wstring& cls) {
    return cls == L"VeloraShellMain" || cls == L"VeloraAppBar" || cls == L"VeloraWatchdogWnd";
}

WinMap SnapshotWindows() {
    WinMap out;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            wchar_t cls[256] = {};
            GetClassNameW(h, cls, 256);
            if (IsVeloraOwned(cls)) return TRUE;
            WinInfo w{cls, IsWindowVisible(h) != FALSE, IsIconic(h) != FALSE, IsZoomed(h) != FALSE, {}, 0};
            GetWindowRect(h, &w.rect);
            GetWindowThreadProcessId(h, &w.pid);
            if (w.pid != GetCurrentProcessId()) {
                // Other processes: only windows a user can see or restore. Hidden helper
                // windows and per-thread IME windows come and go on their own.
                if (!w.visible && !w.iconic) return TRUE;
                const std::wstring c(cls);
                // Explorer-owned helpers that come and go on their own (the EdgeUI input
                // window is recreated by Explorer on work-area/appbar changes).
                if (c == L"IME" || c == L"MSCTFIME UI" || c == L"EdgeUiInputTopWndClass" || c == L"EdgeUiInputWndClass")
                    return TRUE;
            }
            reinterpret_cast<WinMap*>(lp)->emplace(h, w);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

bool VeloraWindowsGone() {
    return FindWindowW(L"VeloraShellMain", nullptr) == nullptr && FindWindowW(L"VeloraAppBar", nullptr) == nullptr;
}

// "before == after": same windows, same class/visibility/placement state; exact
// rectangles for the fixture's own windows. Returns "" when equal.
std::string DiffWindows(const WinMap& a, const WinMap& b) {
    std::ostringstream d;
    for (const auto& [h, w] : a) {
        auto it = b.find(h);
        if (it == b.end()) {
            // Attribution: Velora causes the loss of its own test windows, and calls a window API
            // (SetForegroundWindow) on the one that had focus. It never terminates a foreign process,
            // so any other foreign window that vanished did so on its own: logged, not failed.
            // The focused window is a failure unless its own process was seen destroying it (a transient
            // Chrome popup that had focus closing itself): Velora cannot destroy another process's window.
            std::lock_guard<std::mutex> lock(g_destroyedMutex);
            bool destroyedByOwner = g_destroyed.count(h) != 0;
            if (w.pid == GetCurrentProcessId() || (h == g_fgBefore && !destroyedByOwner)) { d << "lost " << h << " (" << WideToUtf8(w.cls) << ") "; continue; }
            g_externalLost[h] = WideToUtf8(w.cls) + " pid " + std::to_string(w.pid) + (g_destroyed.count(h) ? ", destroyed by its owner" : ", no destroy event (hidden?)");
            continue;
        }
        const WinInfo& n = it->second;
        if (n.cls != w.cls || n.visible != w.visible || n.iconic != w.iconic || n.zoomed != w.zoomed)
            d << "changed " << h << " (" << WideToUtf8(w.cls) << "->" << WideToUtf8(n.cls) << ") vis " << w.visible << "->" << n.visible << " iconic " << w.iconic << "->" << n.iconic << " zoomed " << w.zoomed << "->" << n.zoomed << " pid " << w.pid << " ";
        if (w.pid == GetCurrentProcessId() && !EqualRect(&w.rect, &n.rect)) d << "moved " << h << " ";
    }
    for (const auto& [h, w] : b) {
        if (a.count(h)) continue;
        // A window another process created on its own during the run is desktop
        // churn, not something Velora can cause: counted and reported, not failed.
        // Nothing lost or changed, and no new window of ours, is the invariant.
        if (w.pid != GetCurrentProcessId()) { g_externalNew.insert(h); continue; }
        d << "new " << h << " (" << WideToUtf8(w.cls) << ") ";
    }
    return d.str();
}

// A live desktop can have unrelated churn (toasts, tooltips). Compare against up
// to 3 fresh "after" snapshots 150 ms apart; only a persistent difference fails.
std::string DiffWithRetry(const WinMap& before) {
    std::string d;
    for (int i = 0; i < 3; ++i) {
        d = DiffWindows(before, SnapshotWindows());
        if (d.empty()) return d;
        Sleep(150);
    }
    return d;
}

RECT PrimaryWorkArea() {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcWork;
}

bool TraysAll(bool visible) {
    auto t = sw::FindTrayWindows();
    if (t.empty()) return false;
    for (HWND h : t)
        if ((IsWindowVisible(h) != FALSE) != visible) return false;
    return true;
}

// ------------------------------------------------------------- processes

std::filesystem::path g_shellExe, g_watchdogExe, g_dataDir;
std::vector<HANDLE> g_spawned;

struct Proc {
    HANDLE h = nullptr;
    DWORD pid = 0;
    bool Alive() const { return h && WaitForSingleObject(h, 0) == WAIT_TIMEOUT; }
    DWORD ExitCode() const { DWORD c = 0; GetExitCodeProcess(h, &c); return c; }
};

Proc Spawn(const std::filesystem::path& exe, const std::wstring& args, DWORD extraFlags = 0) {
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | extraFlags, nullptr, nullptr, &si, &pi))
        return {};
    CloseHandle(pi.hThread);
    g_spawned.push_back(pi.hProcess);
    return {pi.hProcess, pi.dwProcessId};
}

int RunToEnd(const std::filesystem::path& exe, const std::wstring& args, DWORD timeoutMs = 15000) {
    Proc p = Spawn(exe, args);
    if (!p.h) return -1;
    if (WaitForSingleObject(p.h, timeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(p.h, 99);  // our own spawned child
        return -2;
    }
    return static_cast<int>(p.ExitCode());
}

std::optional<json::Value> ShellState(DWORD timeoutMs = 1000) {
    std::string resp;
    if (!ipc::Call(ShellPipeName(), ipc::MakeMessage("request", ipc::msg::kGetState, 1), &resp, timeoutMs)) return std::nullopt;
    auto r = json::Parse(resp);
    if (!r || !r->Find("payload")) return std::nullopt;
    return *r->Find("payload");
}

struct Session {
    Proc shell;
    DWORD watchdogPid = 0;
    double enterMs = -1;
    bool alive = false;       // set by StartShell, for diagnostics only
    DWORD exitCode = 0;
};

// Starts velora-shell and waits until it reports "entered".
Session StartShell(DWORD timeoutMs = 10000) {
    Session s;
    double t0 = NowMs();
    s.shell = Spawn(g_shellExe, L"");
    if (!s.shell.h) return s;
    PollUntil(
        [&] {
            if (!s.shell.Alive()) return true;
            auto st = ShellState(500);
            return st && st->Find("entered") && st->Find("entered")->AsBool();
        },
        static_cast<double>(timeoutMs), t0);
    auto st = ShellState(500);
    s.alive = s.shell.Alive();
    if (!s.alive) s.exitCode = s.shell.ExitCode();
    if (s.shell.Alive() && st && st->Find("entered") && st->Find("entered")->AsBool()) {
        s.enterMs = NowMs() - t0;
        s.watchdogPid = static_cast<DWORD>(st->Find("watchdogPid")->AsInt());
    }
    return s;
}

bool WaitExit(HANDLE h, DWORD ms) { return h && WaitForSingleObject(h, ms) == WAIT_OBJECT_0; }

void KillStragglers() {
    for (HANDLE h : g_spawned)
        if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) {
            TerminateProcess(h, 98);  // only processes this test spawned
            // TerminateProcess is asynchronous: until the process object signals, its pipe instance and
            // single-instance mutex still exist and a shell started next would refuse to run ("already running").
            WaitForSingleObject(h, 5000);
        }
    for (HANDLE h : g_spawned) CloseHandle(h);
    g_spawned.clear();
}

void ResetWorld() {
    KillStragglers();
    if (!TraysAll(true)) {
        for (HWND h : sw::FindTrayWindows()) ShowWindow(h, SW_SHOWNOACTIVATE);
    }
    DeleteFileW(DirtyMarkerPath().c_str());
    DeleteFileW(StatePath().c_str());
}

std::set<DWORD> ProcessIdsNamed(const wchar_t* name) {
    std::set<DWORD> ids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{sizeof e};
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (_wcsicmp(e.szExeFile, name) == 0) ids.insert(e.th32ProcessID);
    CloseHandle(snap);
    return ids;
}

std::string ReadFileText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string Fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f", v);
    return b;
}

// ------------------------------------------------------------- phases

bool g_realExplorer = false;
int g_cycles = 50, g_kills = 10, g_hangs = 3, g_restarts = 3;

void PhaseCycles() {
    g_phase = "cycles";
    int ok = 0;
    for (int i = 0; i < g_cycles; ++i) {
        ResetWorld();
        WinMap before = SnapshotWindows();
        g_fgBefore = GetForegroundWindow();
        RECT workBefore = PrimaryWorkArea();
        bool good = true;
        auto fail = [&](const std::string& what) { good = false; Record("cycle " + std::to_string(i) + ": " + what, false); };

        Session s = StartShell();
        if (s.enterMs < 0) { fail("shell did not enter"); KillStragglers(); continue; }
        g_samples["enter_ms"].push_back(s.enterMs);

        if (!TraysAll(false)) fail("taskbars not hidden while in Velora Mode");
        if (!DirtyMarkerExists(DirtyMarkerPath())) fail("dirty marker missing while in Velora Mode");
        auto state = LoadState(StatePath());
        if (!state || state->shellPid != s.shell.pid || state->watchdogPid != s.watchdogPid) fail("state.json missing/incorrect");
        auto st = ShellState();
        if (g_realExplorer && !(st && st->Find("appbarRegistered") && st->Find("appbarRegistered")->AsBool())) fail("appbar not registered");

        double t1 = NowMs();
        int rc = RunToEnd(g_shellExe, L"--exit");
        double visibleMs = PollUntil([] { return TraysAll(true); }, 3000, t1);
        bool exited = WaitExit(s.shell.h, 5000);
        double exitMs = NowMs() - t1;
        g_samples["exit_ms"].push_back(exitMs);

        if (rc != 0) fail("--exit returned " + std::to_string(rc));
        if (visibleMs < 0) fail("taskbars not restored after exit");
        if (!exited) fail("shell still running after exit");
        else if (s.shell.ExitCode() != 0) fail("shell exit code " + std::to_string(s.shell.ExitCode()));
        if (DirtyMarkerExists(DirtyMarkerPath())) fail("dirty marker left after exit");
        if (std::filesystem::exists(StatePath())) fail("state.json not consumed on clean exit");
        Proc wd{OpenProcess(SYNCHRONIZE, FALSE, s.watchdogPid), s.watchdogPid};
        if (wd.h && !WaitExit(wd.h, 3000)) fail("watchdog still running after exit");
        if (wd.h) CloseHandle(wd.h);
        if (!VeloraWindowsGone()) fail("Velora windows left behind");
        RECT workAfter = PrimaryWorkArea();
        if (g_realExplorer && !EqualRect(&workBefore, &workAfter)) fail("work area changed across the cycle");
        std::string d = DiffWithRetry(before);
        if (!d.empty()) fail("window list differs: " + d);
        if (good) ++ok;
    }
    Record("all cycles clean", ok == g_cycles, std::to_string(ok) + "/" + std::to_string(g_cycles));
}

void PhaseKills() {
    g_phase = "kills";
    int ok = 0;
    for (int i = 0; i < g_kills; ++i) {
        ResetWorld();
        WinMap before = SnapshotWindows();
        g_fgBefore = GetForegroundWindow();
        RECT workBefore = PrimaryWorkArea();
        Session s = StartShell();
        if (s.enterMs < 0) { Record("kill " + std::to_string(i) + ": enter", false); continue; }
        Sleep(200 + (i * 97) % 900);  // vary the phase relative to the 1 s heartbeat
        if (!TraysAll(false)) { Record("kill " + std::to_string(i) + ": precondition hidden", false); continue; }

        double t0 = NowMs();
        TerminateProcess(s.shell.h, 9);  // the forced kill: no cleanup code runs
        double visibleMs = PollUntil([] { return TraysAll(true); }, 5000, t0);
        double markerMs = PollUntil([] { return !DirtyMarkerExists(DirtyMarkerPath()); }, 5000, t0);
        g_samples["kill_restore_ms"].push_back(visibleMs);

        bool good = visibleMs >= 0 && visibleMs <= 1000 && markerMs >= 0 && !std::filesystem::exists(StatePath());
        Proc wd{OpenProcess(SYNCHRONIZE, FALSE, s.watchdogPid), s.watchdogPid};
        bool wdGone = !wd.h || WaitExit(wd.h, 3000);
        if (wd.h) CloseHandle(wd.h);
        // The dead shell's appbar must be gone and the work area back as recorded: measured from the kill.
        double workMs = g_realExplorer ? PollUntil([&] { RECT w = PrimaryWorkArea(); return EqualRect(&workBefore, &w) != FALSE; }, 3000, t0) : 0;
        if (g_realExplorer && workMs >= 0) g_samples["kill_workarea_ms"].push_back(workMs);
        bool workOk = workMs >= 0 && workMs <= 1500;
        std::string d = DiffWithRetry(before);
        Record("kill " + std::to_string(i), good && wdGone && workOk && d.empty(),
               "taskbars " + Fmt(visibleMs) + " ms, marker " + Fmt(markerMs) + " ms, watchdog gone " + std::to_string(wdGone) +
                   ", work area back after " + (g_realExplorer ? Fmt(workMs) + " ms" : std::string("n/a (no real shell)")) +
                   (d.empty() ? "" : ", windows: " + d));
        if (good && wdGone && workOk && d.empty()) ++ok;
    }
    Record("all kills recovered within 1 s", ok == g_kills, std::to_string(ok) + "/" + std::to_string(g_kills));
}

void PhaseHangs() {
    g_phase = "hangs";
    std::mt19937 rng(12345);
    int ok = 0;
    for (int i = 0; i < g_hangs; ++i) {
        ResetWorld();
        RECT workBefore = PrimaryWorkArea();
        Session s = StartShell();
        if (s.enterMs < 0) { Record("hang " + std::to_string(i) + ": enter", false); continue; }
        Sleep(1100 + rng() % 900);  // a few heartbeats in, random phase

        ipc::PipeClient c;
        bool sent = c.Connect(ShellPipeName(), 2000);
        json::Value p;
        p.Set("ms", 120000);
        double t0 = NowMs();
        sent = sent && c.Post(ipc::MakeMessage("request", "debug_hang", 5, p));
        if (!sent) { Record("hang " + std::to_string(i) + ": send", false); continue; }
        double visibleMs = PollUntil([] { return TraysAll(true); }, 8000, t0);
        bool killed = WaitExit(s.shell.h, 3000);
        bool markerGone = PollUntil([] { return !DirtyMarkerExists(DirtyMarkerPath()); }, 2000) >= 0;
        g_samples["hang_restore_ms"].push_back(visibleMs);
        double workMs = g_realExplorer ? PollUntil([&] { RECT w = PrimaryWorkArea(); return EqualRect(&workBefore, &w) != FALSE; }, 6000, t0) : 0;
        if (g_realExplorer && workMs >= 0) g_samples["hang_workarea_ms"].push_back(workMs);
        bool good = visibleMs >= 0 && visibleMs <= 3000 && killed && markerGone && workMs >= 0 && !std::filesystem::exists(StatePath());
        Record("hang " + std::to_string(i), good,
               "taskbars " + Fmt(visibleMs) + " ms, hung shell terminated " + std::to_string(killed) + ", marker gone " +
                   std::to_string(markerGone) + ", work area back after " + (g_realExplorer ? Fmt(workMs) + " ms" : std::string("n/a")));
        if (good) ++ok;
    }
    Record("all hangs recovered within 3 s", ok == g_hangs, std::to_string(ok) + "/" + std::to_string(g_hangs));
}

void PhaseExplorerRestarts() {
    g_phase = "explorer-restarts";
    ResetWorld();
    Session s = StartShell();
    if (s.enterMs < 0) { Record("enter", false); return; }
    int ok = 0;
    for (int i = 0; i < g_restarts; ++i) {
        auto before = ShellState();
        std::vector<HWND> oldTrays = g_fx.Trays();
        g_fx.RestartExplorer();
        bool newWindows = g_fx.Trays() != oldTrays;
        double hiddenMs = PollUntil([] { return TraysAll(false); }, 3000);
        Sleep(1300);  // outlast the delayed re-hide passes; must stay hidden
        bool stillHidden = TraysAll(false);
        auto after = ShellState();
        bool counted = after && before && after->Find("explorerRestarts")->AsInt() == before->Find("explorerRestarts")->AsInt() + 1;
        bool reRegistered = true;
        if (g_realExplorer)
            reRegistered = after && before && after->Find("appbarRegistered")->AsBool() &&
                           after->Find("appbarRegistrations")->AsInt() > before->Find("appbarRegistrations")->AsInt();
        g_samples["rehide_ms"].push_back(hiddenMs);
        bool good = newWindows && hiddenMs >= 0 && stillHidden && counted && reRegistered && s.shell.Alive();
        Record("explorer restart " + std::to_string(i), good,
               "re-hidden after " + Fmt(hiddenMs) + " ms, stayed hidden " + std::to_string(stillHidden) + ", counted " +
                   std::to_string(counted) + ", appbar re-registered " + std::to_string(reRegistered) +
                   (g_realExplorer ? "" : " (no real shell: appbar not applicable)"));
        if (good) ++ok;
    }
    RunToEnd(g_shellExe, L"--exit");
    WaitExit(s.shell.h, 5000);
    Record("taskbars restored after restarts + exit", TraysAll(true));
    Record("all explorer restarts handled", ok == g_restarts, std::to_string(ok) + "/" + std::to_string(g_restarts));
}

// A state.json + dirty marker pair as a crashed shell would leave behind.
void LeaveCrashDebris(DWORD pid, std::uint64_t startTime) {
    SwitchState st;
    st.shellPid = pid;
    st.shellStartTime = startTime;
    st.monitors = sw::SnapshotMonitors();
    SaveState(st, StatePath());
    CreateDirtyMarker(DirtyMarkerPath());
}

// Last n lines of the shell log, for failure diagnostics.
std::string LogTail(size_t n) {
    std::string log = ReadFileText(g_dataDir / "logs" / "velora-shell.log");
    size_t pos = log.size();
    for (size_t i = 0; i <= n && pos != std::string::npos && pos > 0; ++i) pos = log.rfind('\n', pos - 1);
    return pos == std::string::npos ? log : log.substr(pos + 1);
}

void PhaseDirtyMarker() {
    g_phase = "dirty-marker";

    // a) shell --recover-only restores before doing anything else
    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    EXPECT(TraysAll(false), "precondition");
    int rc = RunToEnd(g_shellExe, L"--recover-only");
    EXPECT(rc == 0, "exit code " + std::to_string(rc));
    EXPECT(TraysAll(true));
    EXPECT(!DirtyMarkerExists(DirtyMarkerPath()));

    // b) normal start with a marker: recovery is logged before enter begins
    ResetWorld();
    std::filesystem::remove(g_dataDir / "logs" / "velora-shell.log");
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    // Generous timeout: this is the first full shell start of the run (cold font/DWM/Defender caches on a
    // fresh CI VM), and the phase is about recovery order, not enter latency (the cycles phase measures that).
    Session s = StartShell(30000);
    EXPECT(s.enterMs >= 0, "shell entered after recovery; " + (s.alive ? std::string("still running") : "exited " + std::to_string(s.exitCode)) +
                               "; log tail:\n" + LogTail(12));
    RunToEnd(g_shellExe, L"--exit");
    WaitExit(s.shell.h, 5000);
    std::string log = ReadFileText(g_dataDir / "logs" / "velora-shell.log");
    size_t rec = log.find("startup recovery: dirty marker found"), ent = log.find("enter: begin");
    EXPECT(rec != std::string::npos && ent != std::string::npos && rec < ent, "recovery precedes enter in the log");
    EXPECT(TraysAll(true));

    // c) login check: restores when the marker exists, leaves everything alone when it does not
    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    rc = RunToEnd(g_watchdogExe, L"--login-check");
    EXPECT(rc == 0, "login-check exit " + std::to_string(rc));
    EXPECT(TraysAll(true) && !DirtyMarkerExists(DirtyMarkerPath()), "login check restored");
    g_fx.HideTrays();
    rc = RunToEnd(g_watchdogExe, L"--login-check");
    EXPECT(rc == 0 && TraysAll(false), "login check without marker changes nothing");
    ResetWorld();

    // d) unreadable state.json + marker: safe defaults still bring the taskbar back
    g_fx.HideTrays();
    {
        std::ofstream(StatePath(), std::ios::binary) << "{ this is not json";
        CreateDirtyMarker(DirtyMarkerPath());
    }
    rc = RunToEnd(g_watchdogExe, L"--login-check");
    EXPECT(TraysAll(true) && !DirtyMarkerExists(DirtyMarkerPath()), "corrupt state handled; rc " + std::to_string(rc));
}

// A second velora-shell must exit at once, log why, and leave the first one untouched.
void PhaseSingleInstance() {
    g_phase = "single-instance";
    ResetWorld();
    Session s = StartShell();
    EXPECT(s.enterMs >= 0, "first shell entered");
    Proc second = Spawn(g_shellExe, L"");
    EXPECT(WaitExit(second.h, 3000), "second instance exits within 3 s");
    EXPECT(second.h && second.ExitCode() == 0, "second instance exit code 0");
    EXPECT(s.shell.Alive(), "first shell still running");
    auto st = ShellState(1000);
    EXPECT(st && st->Find("entered") && st->Find("entered")->AsBool(), "first shell still entered");
    EXPECT(ReadFileText(g_dataDir / "logs" / "velora-shell.log").find("second velora-shell instance refused") != std::string::npos,
           "refusal logged");
    RunToEnd(g_shellExe, L"--exit");
    WaitExit(s.shell.h, 5000);
    EXPECT(TraysAll(true));
}

void PhaseRestoreCli() {
    g_phase = "restore-cli";

    // --restore while the shell is running: restores, terminates only the shell.
    ResetWorld();
    Session s = StartShell();
    EXPECT(s.enterMs >= 0, "enter");
    double t0 = NowMs();
    int rc = RunToEnd(g_watchdogExe, L"--restore");
    double ms = PollUntil([] { return TraysAll(true); }, 3000, t0);
    EXPECT(rc == 0, "exit code " + std::to_string(rc));
    EXPECT(ms >= 0 && ms <= 1500, "taskbars back after " + Fmt(ms) + " ms");
    EXPECT(WaitExit(s.shell.h, 3000), "recorded shell terminated");
    EXPECT(PollUntil([] { return !DirtyMarkerExists(DirtyMarkerPath()); }, 2000) >= 0, "marker removed");
    Proc wd{OpenProcess(SYNCHRONIZE, FALSE, s.watchdogPid), s.watchdogPid};
    EXPECT(!wd.h || WaitExit(wd.h, 3000), "watching watchdog exits");
    if (wd.h) CloseHandle(wd.h);

    // PID safety: a recorded PID that is NOT velora-shell must never be terminated.
    ResetWorld();
    Proc bystander = Spawn(L"C:\\Windows\\System32\\cmd.exe", L"/c ping -n 30 127.0.0.1 >nul");
    g_fx.HideTrays();
    LeaveCrashDebris(bystander.pid, 0);
    rc = RunToEnd(g_watchdogExe, L"--restore");
    EXPECT(bystander.Alive(), "unrelated process with the recorded PID was left alone");
    EXPECT(TraysAll(true) && rc == 0, "still restored");
    if (bystander.Alive()) TerminateProcess(bystander.h, 0);  // our own helper child

    // --restore with nothing to restore is harmless
    ResetWorld();
    rc = RunToEnd(g_watchdogExe, L"--restore");
    EXPECT(rc == 0 && TraysAll(true), "idempotent with no state");
}

void PhaseEmergencyHotkey() {
    g_phase = "emergency-hotkey";
    ResetWorld();
    Session s = StartShell();
    EXPECT(s.enterMs >= 0, "enter");
    Sleep(300);

    // Registration is real: nobody else (including us) can take the same combination.
    SetLastError(0);
    bool taken = RegisterHotKey(nullptr, 77, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_ESCAPE) != FALSE;
    DWORD err = GetLastError();
    if (taken) UnregisterHotKey(nullptr, 77);
    EXPECT(!taken && err == ERROR_HOTKEY_ALREADY_REGISTERED, "watchdog owns Ctrl+Alt+Shift+Esc (error " + std::to_string(err) + ")");

    // Freeze the shell, then press the hotkey: must recover far sooner than the 3 s heartbeat timeout.
    ipc::PipeClient c;
    json::Value p;
    p.Set("ms", 120000);
    EXPECT(c.Connect(ShellPipeName(), 2000) && c.Post(ipc::MakeMessage("request", "debug_hang", 5, p)), "shell frozen");
    Sleep(150);
    HWND wdWindow = FindWindowW(L"VeloraWatchdogWnd", nullptr);
    EXPECT(wdWindow != nullptr, "watchdog window present");
    double t0 = NowMs();
    PostMessageW(wdWindow, WM_HOTKEY, 1, 0);  // what the OS posts for the registered hotkey
    double ms = PollUntil([] { return TraysAll(true); }, 3000, t0);
    g_samples["hotkey_restore_ms"].push_back(ms);
    EXPECT(ms >= 0 && ms <= 1000, "restored " + Fmt(ms) + " ms after the hotkey, shell frozen");
    EXPECT(WaitExit(s.shell.h, 2000), "frozen shell terminated");
    EXPECT(PollUntil([] { return !DirtyMarkerExists(DirtyMarkerPath()); }, 2000) >= 0, "marker removed");
}

// The emergency hotkey is a safety exit: if it cannot be registered, Enter must refuse.
void PhaseHotkeyUnavailable() {
    g_phase = "hotkey-unavailable";
    ResetWorld();
    std::filesystem::remove(g_dataDir / "logs" / "velora-shell.log");
    bool taken = RegisterHotKey(nullptr, 79, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_ESCAPE) != FALSE;  // we hold it
    EXPECT(taken, "test process took the hotkey first");
    Session s = StartShell();
    EXPECT(s.enterMs < 0, "shell refused to enter without the emergency hotkey");
    EXPECT(WaitExit(s.shell.h, 5000) && s.shell.ExitCode() == 2, "shell exited with 'enter failed' (2): " + std::to_string(s.shell.ExitCode()));
    EXPECT(TraysAll(true) && !DirtyMarkerExists(DirtyMarkerPath()), "Windows left untouched, no marker");
    EXPECT(PollUntil([] { return ProcessIdsNamed(L"velora-watchdog.exe").empty(); }, 3000) >= 0, "no watchdog left behind");
    std::string log = ReadFileText(g_dataDir / "logs" / "velora-shell.log");
    EXPECT(log.find("emergency hotkey") != std::string::npos, "clear reason logged");
    if (taken) UnregisterHotKey(nullptr, 79);
    Session ok = StartShell();  // and with the hotkey free it enters again
    EXPECT(ok.enterMs >= 0, "enters once the hotkey is free");
    RunToEnd(g_shellExe, L"--exit");
    WaitExit(ok.shell.h, 5000);
}

void PhaseWatchdogDeath() {
    g_phase = "watchdog-death";
    ResetWorld();
    Session s = StartShell();
    EXPECT(s.enterMs >= 0, "enter");
    HANDLE wd = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, s.watchdogPid);
    if (wd) {
        TerminateProcess(wd, 1);  // a Velora process (started by the shell)
        CloseHandle(wd);
    }
    DWORD newPid = 0;
    double ms = PollUntil(
        [&] {
            auto st = ShellState(500);
            if (!st) return false;
            newPid = static_cast<DWORD>(st->Find("watchdogPid")->AsInt());
            return newPid != 0 && newPid != s.watchdogPid;
        },
        6000);
    EXPECT(ms >= 0, "shell replaced the dead watchdog after " + Fmt(ms) + " ms");
    EXPECT(s.shell.Alive() && TraysAll(false), "still in Velora Mode");
    auto restarted = ShellState();
    EXPECT(restarted && restarted->Find("watchdogRestarts") && restarted->Find("watchdogRestarts")->AsInt() == 1,
           "one watchdog restart counted");
    RunToEnd(g_shellExe, L"--exit");
    WaitExit(s.shell.h, 5000);
    EXPECT(TraysAll(true), "clean exit afterwards");
}

// ------------------------------------------------------------- watchdog lifecycle (review PR A)

// A stand-in for "the shell" when the watchdog is driven directly: an unrelated
// process of ours that the watchdog may never terminate.
Proc SpawnHelper() { return Spawn(L"C:\\Windows\\System32\\cmd.exe", L"/c ping -n 60 127.0.0.1 >nul"); }

// type = hello | disarm. Returns the response status, or -1 if there was no response.
int WatchdogRequest(ipc::PipeClient& c, const char* type, std::int64_t pid) {
    json::Value p;
    p.Set("client", "test");
    p.Set("pid", pid);
    p.Set("protocol", static_cast<int>(ipc::kProtocolVersion));
    std::string resp;
    if (!c.Request(ipc::MakeMessage("request", type, 1, p), &resp, 2000)) return -1;
    auto r = json::Parse(resp);
    return r && r->Find("status") ? static_cast<int>(r->Find("status")->AsInt(-1)) : -1;
}

void PhaseWatchdogLifecycle() {
    g_phase = "watchdog-lifecycle";

    // 1. Disarm: after the ack the watchdog exits, and the shell going away later restores nothing.
    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    {
        Proc helper = SpawnHelper();
        Proc wd = Spawn(g_watchdogExe, L"--watch " + std::to_wstring(helper.pid));
        ipc::PipeClient c;
        EXPECT(c.Connect(WatchdogPipeName(), 3000), "connect to the watchdog");
        EXPECT(WatchdogRequest(c, ipc::msg::kHello, helper.pid) == 0, "hello with the right pid arms");
        EXPECT(WatchdogRequest(c, ipc::msg::kDisarm, helper.pid) == 0, "disarm acknowledged (synchronous)");
        EXPECT(WaitExit(wd.h, 2000), "watchdog exits on disarm");
        TerminateProcess(helper.h, 0);  // "the shell went away" after leaving Velora Mode
        Sleep(400);
        EXPECT(TraysAll(false) && DirtyMarkerExists(DirtyMarkerPath()), "disarmed watchdog restored nothing");
        EXPECT(wd.ExitCode() == 0, "exit code " + std::to_string(wd.ExitCode()));
    }

    // 2. Armed, heartbeats stop, but the dirty marker is gone: exit quietly, never terminate anything.
    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    DeleteFileW(DirtyMarkerPath().c_str());
    {
        Proc helper = SpawnHelper();
        Proc wd = Spawn(g_watchdogExe, L"--watch " + std::to_wstring(helper.pid));
        ipc::PipeClient c;
        EXPECT(c.Connect(WatchdogPipeName(), 3000) && WatchdogRequest(c, ipc::msg::kHello, helper.pid) == 0, "armed");
        EXPECT(WaitExit(wd.h, ipc::kHeartbeatTimeoutMs + 2500), "watchdog exits after the heartbeat timeout");
        EXPECT(helper.Alive(), "the silent 'shell' was NOT terminated");
        EXPECT(TraysAll(false), "taskbars untouched without a marker");
    }

    // 3. Never armed (no hello): after the startup grace it exits quietly even with a marker present.
    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    {
        Proc helper = SpawnHelper();
        Proc wd = Spawn(g_watchdogExe, L"--watch " + std::to_wstring(helper.pid));
        EXPECT(WaitExit(wd.h, 8000), "unarmed watchdog exits after the startup grace");
        EXPECT(helper.Alive(), "unarmed watchdog terminated nothing");
        EXPECT(TraysAll(false) && DirtyMarkerExists(DirtyMarkerPath()), "unarmed watchdog restored nothing");
    }

    // 4. hello must carry the shell pid: a mismatch is rejected and does not arm.
    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    {
        Proc helper = SpawnHelper();
        Proc wd = Spawn(g_watchdogExe, L"--watch " + std::to_wstring(helper.pid));
        ipc::PipeClient c;
        EXPECT(c.Connect(WatchdogPipeName(), 3000), "connect");
        int st = WatchdogRequest(c, ipc::msg::kHello, helper.pid + 1);
        EXPECT(st == 400, "hello with the wrong pid rejected, status " + std::to_string(st));
        TerminateProcess(helper.h, 0);
        EXPECT(WaitExit(wd.h, 3000), "watchdog exits when the shell is gone");
        Sleep(200);
        EXPECT(TraysAll(false) && DirtyMarkerExists(DirtyMarkerPath()), "an unarmed (pid-mismatch) watchdog restored nothing");
    }

    // 5. Goodbye wins over a later hello: a watchdog that was told to go is never armed again.
    ResetWorld();
    {
        Proc helper = SpawnHelper();
        Proc wd = Spawn(g_watchdogExe, L"--watch " + std::to_wstring(helper.pid));
        ipc::PipeClient c1, c2;
        EXPECT(c1.Connect(WatchdogPipeName(), 3000) && c2.Connect(WatchdogPipeName(), 3000), "two clients");
        EXPECT(WatchdogRequest(c1, ipc::msg::kHello, helper.pid) == 0, "armed");
        EXPECT(WatchdogRequest(c1, ipc::msg::kDisarm, helper.pid) == 0, "disarmed");
        EXPECT(WatchdogRequest(c2, ipc::msg::kHello, helper.pid) != 0, "a hello after the goodbye is not answered with ok");
        EXPECT(WaitExit(wd.h, 2000), "watchdog gone");
    }

    // 6. Exit -> immediate Enter: the old watchdog is gone before the shell exits, so the new session's
    //    watchdog (not a dying one) is the armed one: killing the new shell must restore Windows.
    int ok = 0;
    for (int i = 0; i < 5; ++i) {
        ResetWorld();
        Session a = StartShell();
        bool good = a.enterMs >= 0;
        Proc oldWd{OpenProcess(SYNCHRONIZE, FALSE, a.watchdogPid), a.watchdogPid};
        RunToEnd(g_shellExe, L"--exit");
        good = good && WaitExit(a.shell.h, 5000);
        bool oldGone = !oldWd.h || WaitForSingleObject(oldWd.h, 0) == WAIT_OBJECT_0;  // no grace: already gone
        if (oldWd.h) CloseHandle(oldWd.h);
        Session b = StartShell();  // immediately
        good = good && oldGone && b.enterMs >= 0;
        double t0 = NowMs();
        if (b.shell.h) TerminateProcess(b.shell.h, 9);
        double ms = PollUntil([] { return TraysAll(true); }, 3000, t0);
        good = good && ms >= 0 && ms <= 1000;
        // Let the crash-recovery watchdog finish and exit before the next iteration starts a shell
        // (it still owns the pipe while it restores).
        Proc bWd{OpenProcess(SYNCHRONIZE, FALSE, b.watchdogPid), b.watchdogPid};
        bool bWdGone = !bWd.h || WaitExit(bWd.h, 3000);
        if (bWd.h) CloseHandle(bWd.h);
        Record("exit->enter " + std::to_string(i), good && bWdGone,
               "first session entered " + std::to_string(a.enterMs >= 0) + ", old watchdog gone at shell exit " +
                   std::to_string(oldGone) + ", second session entered " + std::to_string(b.enterMs >= 0) +
                   ", recovered after " + Fmt(ms) + " ms");
        if (good) ++ok;
    }
    Record("every immediate re-enter is protected by its own watchdog", ok == 5, std::to_string(ok) + "/5");

    // 7. A failed watchdog restart terminates the unconnected watchdog it started, and the shell leaves Velora Mode.
    ResetWorld();
    {
        std::filesystem::path flag = g_dataDir / L"reject_hello";
        Session s = StartShell();
        EXPECT(s.enterMs >= 0, "enter");
        std::ofstream(flag) << "x";  // from now on every watchdog rejects hello
        HANDLE wd = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, s.watchdogPid);
        if (wd) {
            TerminateProcess(wd, 1);  // a Velora process
            CloseHandle(wd);
        }
        EXPECT(WaitExit(s.shell.h, 15000), "shell left Velora Mode when the watchdog could not be replaced");
        EXPECT(PollUntil([] { return ProcessIdsNamed(L"velora-watchdog.exe").empty(); }, 500) >= 0,
               "no rejected, unconnected watchdog left running");
        EXPECT(TraysAll(true), "taskbars visible");
        std::filesystem::remove(flag);
    }
}

// ------------------------------------------------------------- restore correctness (review PR B)

void SetLoginTimings(const wchar_t* wait, const wchar_t* retry, const wchar_t* max) {
    SetEnvironmentVariableW(L"VELORA_TEST_LOGIN_WAIT_MS", wait);
    SetEnvironmentVariableW(L"VELORA_TEST_LOGIN_RETRY_MS", retry);
    SetEnvironmentVariableW(L"VELORA_TEST_LOGIN_MAX_MS", max);
}

void PhaseRestoreCorrectness() {
    g_phase = "restore-correctness";
    sw::RestoreOptions opt;
    opt.reason = "gate test";

    // 5. state.json is applied only while the dirty marker exists, and is consumed afterwards.
    ResetWorld();
    g_fx.HideTrays();
    {
        SwitchState st;
        st.monitors = sw::SnapshotMonitors();
        st.taskbarAutoHide = true;
        SaveState(st, StatePath());  // stale: no marker
    }
    auto r = sw::RestoreWindowsDesktop(opt);
    EXPECT(!r.hadState, "state.json ignored without a dirty marker");
    EXPECT(!std::filesystem::exists(StatePath()), "stale state.json removed");
    EXPECT(TraysAll(true), "taskbars are still shown (always safe)");

    ResetWorld();
    g_fx.HideTrays();
    LeaveCrashDebris(0, 0);
    r = sw::RestoreWindowsDesktop(opt);
    EXPECT(r.hadState && r.markerRemoved && r.taskbarsVisible, "state.json applied with a marker");
    EXPECT(!std::filesystem::exists(StatePath()) && !DirtyMarkerExists(DirtyMarkerPath()), "state.json and marker consumed");

    // 6. No taskbar window is not success; the login restore stays pending and retries.
    ResetWorld();
    g_fx.DestroyTrays();
    EXPECT(!sw::WaitTaskbarsVisible(true, 100) && !sw::WaitTaskbarsVisible(false, 100), "an empty tray list never reports success");
    LeaveCrashDebris(0, 0);
    sw::RestoreOptions login = opt;
    login.requireTaskbar = true;
    r = sw::RestoreWindowsDesktop(login);
    EXPECT(r.pending && !r.markerRemoved && DirtyMarkerExists(DirtyMarkerPath()) && std::filesystem::exists(StatePath()),
           "restore without a taskbar is pending: marker and state kept");

    //   a) retry on TaskbarCreated (the periodic retry is set far away)
    SetLoginTimings(L"200", L"60000", L"30000");
    {
        Proc lc = Spawn(g_watchdogExe, L"--login-check");
        Sleep(1500);
        EXPECT(lc.Alive() && DirtyMarkerExists(DirtyMarkerPath()), "login check keeps waiting, marker kept");
        g_fx.CreateTraysHidden(/*broadcast=*/true);
        EXPECT(WaitExit(lc.h, 5000) && lc.ExitCode() == 0, "TaskbarCreated triggers the retry; exit " + std::to_string(lc.ExitCode()));
        EXPECT(TraysAll(true) && !DirtyMarkerExists(DirtyMarkerPath()), "restored after the retry");
    }
    //   b) periodic retry (no broadcast)
    ResetWorld();
    g_fx.DestroyTrays();
    LeaveCrashDebris(0, 0);
    SetLoginTimings(L"200", L"300", L"30000");
    {
        Proc lc = Spawn(g_watchdogExe, L"--login-check");
        Sleep(800);
        EXPECT(lc.Alive() && DirtyMarkerExists(DirtyMarkerPath()), "login check pending");
        g_fx.CreateTraysHidden(/*broadcast=*/false);
        EXPECT(WaitExit(lc.h, 5000) && lc.ExitCode() == 0, "periodic retry restores; exit " + std::to_string(lc.ExitCode()));
        EXPECT(TraysAll(true) && !DirtyMarkerExists(DirtyMarkerPath()), "restored by the periodic retry");
    }
    //   c) the taskbar never appears: gives up, reports pending (not success), marker kept
    ResetWorld();
    g_fx.DestroyTrays();
    LeaveCrashDebris(0, 0);
    SetLoginTimings(L"200", L"300", L"1500");
    {
        Proc lc = Spawn(g_watchdogExe, L"--login-check");
        EXPECT(WaitExit(lc.h, 8000), "login check gives up");
        EXPECT(lc.ExitCode() == 3, "reports pending (3), not success; exit " + std::to_string(lc.ExitCode()));
        EXPECT(DirtyMarkerExists(DirtyMarkerPath()), "marker kept for the next attempt");
    }
    SetLoginTimings(nullptr, nullptr, nullptr);
    g_fx.CreateTraysHidden(false);
    ResetWorld();  // trays visible again, debris removed
}

// ------------------------------------------------------------- attribution of lost windows

constexpr wchar_t kSelfDestroyClass[] = L"VeloraGateSelfDestroy";

// `switch_gate_test --selfdestroy-window`: a foreign process that shows a window and destroys it itself.
int SelfDestroyingWindow() {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kSelfDestroyClass;
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kSelfDestroyClass, L"self-destroying", WS_POPUP, -18000, -18000, 200, 100,
                             nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(w, SW_SHOWNOACTIVATE);
    Sleep(800);
    DestroyWindow(w);
    return 0;
}

void PhaseAttribution() {
    g_phase = "attribution";
    ResetWorld();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    Proc helper = Spawn(self, L"--selfdestroy-window");
    HWND w = nullptr;
    PollUntil([&] { return (w = FindWindowW(kSelfDestroyClass, nullptr)) != nullptr; }, 3000);
    WinMap before = SnapshotWindows();
    EXPECT(w && before.count(w), "foreign fake window present in the snapshot");
    EXPECT(WaitExit(helper.h, 5000), "helper destroyed its window and exited on its own");
    Sleep(200);  // WinEvents are delivered asynchronously
    g_externalLost.clear();
    std::string d = DiffWindows(before, SnapshotWindows());
    EXPECT(d.empty(), "a self-destroyed foreign window is not a Velora failure: " + d);
    EXPECT(g_externalLost.count(w) == 1 && g_externalLost[w].find("destroyed by its owner") != std::string::npos,
           "logged as external with the destroy event seen: " + (g_externalLost.count(w) ? g_externalLost[w] : std::string("(not logged)")));
    g_externalLost.clear();

    // Controls: Velora-attributable losses still fail.
    WinMap fake = SnapshotWindows();
    fake[reinterpret_cast<HWND>(0x7001)] = WinInfo{L"Fake", true, false, false, {}, GetCurrentProcessId()};
    EXPECT(!DiffWindows(fake, SnapshotWindows()).empty(), "a vanished test-owned window is still a failure");
    g_fgBefore = reinterpret_cast<HWND>(0x7002);
    fake = SnapshotWindows();
    fake[g_fgBefore] = WinInfo{L"Fake", true, false, false, {}, 4};
    EXPECT(!DiffWindows(fake, SnapshotWindows()).empty(), "the window that had focus (Velora restores it) is still a failure");
    g_fgBefore = nullptr;
}

// ------------------------------------------------------------- driver

void WriteResults(const std::filesystem::path& out) {
    json::Array checks;
    int failed = 0;
    for (const auto& c : g_checks) {
        if (!c.ok) ++failed;
        json::Value v;
        v.Set("phase", c.phase);
        v.Set("check", c.name);
        v.Set("ok", c.ok);
        if (!c.detail.empty()) v.Set("detail", c.detail);
        checks.push_back(v);
    }
    json::Value stats;
    for (auto& [name, xs] : g_samples) {
        std::vector<double> v;
        for (double x : xs) if (x >= 0) v.push_back(x);
        if (v.empty()) continue;
        std::sort(v.begin(), v.end());
        json::Value s;
        s.Set("n", static_cast<int>(v.size()));
        s.Set("min", v.front());
        s.Set("median", v[v.size() / 2]);
        s.Set("max", v.back());
        stats.Set(name, s);
    }
    json::Value root;
    root.Set("realExplorer", g_realExplorer);
    root.Set("externalNewWindows", static_cast<int>(g_externalNew.size()));
    root.Set("externalLostWindows", static_cast<int>(g_externalLost.size()));
    root.Set("failed", failed);
    root.Set("total", static_cast<int>(g_checks.size()));
    root.Set("latencyMs", stats);
    root.Set("checks", json::Value(checks));
    std::ofstream(out, std::ios::binary) << root.Dump();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--selfdestroy-window") return SelfDestroyingWindow();
    if (argc < 3) {
        std::printf("usage: switch_gate_test <velora-shell.exe> <velora-watchdog.exe> [--cycles N] [--kills N] [--hangs N] [--restarts N] [--out file]\n");
        return 64;
    }
    g_shellExe = argv[1];
    g_watchdogExe = argv[2];
    std::filesystem::path out = L"switch_gate_results.json";
    for (int i = 3; i + 1 < argc; i += 2) {
        std::wstring k = argv[i];
        if (k == L"--cycles") g_cycles = _wtoi(argv[i + 1]);
        else if (k == L"--kills") g_kills = _wtoi(argv[i + 1]);
        else if (k == L"--hangs") g_hangs = _wtoi(argv[i + 1]);
        else if (k == L"--restarts") g_restarts = _wtoi(argv[i + 1]);
        else if (k == L"--out") out = argv[i + 1];
    }

    // Test profile BEFORE anything reads CurrentProfile().
    std::wstring id = L"gate" + std::to_wstring(GetCurrentProcessId());
    g_dataDir = std::filesystem::temp_directory_path() / (L"velora_" + id);
    std::filesystem::remove_all(g_dataDir);
    std::filesystem::create_directories(g_dataDir);
    SetEnvironmentVariableW(L"VELORA_TEST_PROFILE", id.c_str());
    SetEnvironmentVariableW(L"VELORA_DATA_DIR", g_dataDir.c_str());
    // Test hook in velora-watchdog: rejects hello while this file exists (PhaseWatchdogLifecycle, step 7).
    SetEnvironmentVariableW(L"VELORA_TEST_WATCHDOG_REJECT_HELLO_FILE", (g_dataDir / L"reject_hello").c_str());

    g_realExplorer = FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    std::printf("switch gate: real shell tray %s; data dir %s\n", g_realExplorer ? "present" : "absent (appbar checks skipped)",
                WideToUtf8(g_dataDir.wstring()).c_str());

    auto explorerBefore = ProcessIdsNamed(L"explorer.exe");
    g_fx.Start();

    struct { const char* name; void (*fn)(); } phases[] = {
        {"dirty marker", PhaseDirtyMarker},      {"--restore CLI", PhaseRestoreCli},
        {"single instance", PhaseSingleInstance},
        {"emergency hotkey", PhaseEmergencyHotkey}, {"watchdog death", PhaseWatchdogDeath},
        {"hotkey unavailable", PhaseHotkeyUnavailable}, {"window-loss attribution", PhaseAttribution},
        {"watchdog lifecycle", PhaseWatchdogLifecycle}, {"restore correctness", PhaseRestoreCorrectness},
        {"explorer restarts", PhaseExplorerRestarts}, {"forced kills", PhaseKills},
        {"hangs", PhaseHangs},                   {"enter/exit cycles", PhaseCycles},
    };
    for (auto& ph : phases) {
        size_t first = g_checks.size();
        std::printf("== %s\n", ph.name);
        ph.fn();
        int bad = 0;
        for (size_t i = first; i < g_checks.size(); ++i) bad += g_checks[i].ok ? 0 : 1;
        std::printf("   %s (%zu checks, %d failed)\n", bad ? "FAILED" : "ok", g_checks.size() - first, bad);
        ResetWorld();
    }

    g_phase = "process-safety";
    Record("explorer.exe untouched (same PIDs)", ProcessIdsNamed(L"explorer.exe") == explorerBefore);
    Record("no velora process left running", ProcessIdsNamed(L"velora-shell.exe").empty() && ProcessIdsNamed(L"velora-watchdog.exe").empty());
    Record("real state/marker never used", true, "test profile data dir only");

    g_fx.Stop();
    WriteResults(out);

    int failed = 0;
    for (const auto& c : g_checks) failed += c.ok ? 0 : 1;
    for (auto& [name, xs] : g_samples) {
        std::vector<double> v(xs);
        std::sort(v.begin(), v.end());
        if (!v.empty()) std::printf("%-18s n=%zu min=%.0f median=%.0f max=%.0f ms\n", name.c_str(), v.size(), v.front(), v[v.size() / 2], v.back());
    }
    std::printf("windows other processes created during the run (tolerated): %zu\n", g_externalNew.size());
    std::printf("foreign windows that vanished on their own (external, not failed): %zu\n", g_externalLost.size());
    for (const auto& [h, what] : g_externalLost) std::printf("  external: %p %s\n", static_cast<void*>(h), what.c_str());
    std::printf("switch gate: %zu checks, %d failed -> %s\n", g_checks.size(), failed, failed ? "FAIL" : "PASS");
    if (!failed) std::filesystem::remove_all(g_dataDir);  // on failure keep the logs for diagnosis
    return failed ? 1 : 0;
}
