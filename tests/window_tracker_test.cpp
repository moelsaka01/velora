// windows/ tests: event-driven tracking against real top-level windows, pure
// fullscreen/elevation helpers. Focus-stealing checks are opt-in
// (VELORA_TEST_FOCUS=1) so a default run never moves the developer's focus.
#include "core/win.h"

#include <tlhelp32.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/event_bus.h"
#include "windows/window_tracker.h"

using namespace velora;

namespace {

int g_failed = 0, g_skipped = 0;

#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::printf("FAIL %s:%d  %s  %s\n", __FILE__, __LINE__, #cond, "" __VA_ARGS__); \
            ++g_failed;                                                                    \
        }                                                                                  \
    } while (0)

bool PumpUntil(const std::function<bool()>& pred, DWORD timeoutMs = 2000) {
    ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (pred()) return true;
        if (GetTickCount64() > end) return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

void Pump(DWORD ms) { PumpUntil([] { return false; }, ms); }

HWND MakeWindow(const wchar_t* title, DWORD style, DWORD ex, HWND owner = nullptr, int x = -20000, int y = -20000,
                int w = 300, int h = 200) {
    static bool registered = [] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VeloraTrackerTest";
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;
    return CreateWindowExW(ex, L"VeloraTrackerTest", title, style, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}

struct Log {
    std::vector<Event> events;
    int Count(EventType t, HWND h) const {
        int n = 0;
        for (const auto& e : events)
            if (e.header.type == t && e.window.hwnd == static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(h))) ++n;
        return n;
    }
    int Count(EventType t) const {
        int n = 0;
        for (const auto& e : events) n += e.header.type == t;
        return n;
    }
};

void PureHelpers() {
    RECT mon{0, 0, 1920, 1080};
    CHECK(win::IsFullscreenRect({0, 0, 1920, 1080}, mon));
    CHECK(win::IsFullscreenRect({-8, -8, 1928, 1088}, mon));            // exclusive/borderless overshoot
    CHECK(!win::IsFullscreenRect({0, 28, 1920, 1080}, mon));            // maximized under a menu bar
    CHECK(!win::IsFullscreenRect({-8, 20, 1928, 1088}, mon));           // maximized with frame offset
    CHECK(!win::IsFullscreenRect({100, 100, 800, 600}, mon));

    CHECK(!win::QueryElevation(GetCurrentProcessId()).Uncontrollable(win::IsSelfElevated()));

    // services.exe always exists, is protected/elevated: a non-elevated shell must not claim to control it.
    DWORD services = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{sizeof e};
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (_wcsicmp(e.szExeFile, L"services.exe") == 0) services = e.th32ProcessID;
    CloseHandle(snap);
    if (services) {
        auto el = win::QueryElevation(services);
        CHECK(el.Uncontrollable(false), "services.exe must be uncontrollable for a non-elevated shell");
        std::printf("services.exe: elevated=%d inaccessible=%d; self elevated=%d\n", el.elevated, el.inaccessible,
                    win::IsSelfElevated());
    } else {
        std::printf("SKIP elevation check: services.exe not visible\n");
        ++g_skipped;
    }
}

void Lifecycle() {
    EventBus bus;
    Log log;
    for (auto t : {EventType::WindowCreated, EventType::WindowDestroyed, EventType::WindowTitleChanged,
                   EventType::WindowMinimized, EventType::WindowRestored, EventType::WindowActivated})
        bus.Subscribe(t, [&](const Event& e) { log.events.push_back(e); });

    win::WindowTracker tracker(bus, {/*includeOwnProcess=*/true});
    CHECK(tracker.Start());
    std::printf("tracker enumerated %zu app windows on this desktop at start\n", tracker.Windows().size());

    HWND app = MakeWindow(L"tracker app", WS_OVERLAPPEDWINDOW, 0);
    HWND tool = MakeWindow(L"tracker tool", WS_POPUP, WS_EX_TOOLWINDOW);
    HWND owner = MakeWindow(L"tracker owner", WS_OVERLAPPEDWINDOW, 0);
    HWND owned = MakeWindow(L"tracker owned", WS_POPUP, 0, owner);
    HWND hidden = MakeWindow(L"tracker hidden", WS_OVERLAPPEDWINDOW, 0);
    ShowWindow(app, SW_SHOWNOACTIVATE);
    ShowWindow(tool, SW_SHOWNOACTIVATE);
    ShowWindow(owner, SW_SHOWNOACTIVATE);
    ShowWindow(owned, SW_SHOWNOACTIVATE);

    // Creation via events
    CHECK(PumpUntil([&] { return tracker.Find(app) != nullptr; }), "app window tracked");
    Pump(200);
    CHECK(tracker.Find(app) && tracker.Find(app)->title == L"tracker app");
    CHECK(tracker.Find(app) && tracker.Find(app)->controllable);
    CHECK(log.Count(EventType::WindowCreated, app) == 1, "exactly one WindowCreated");
    CHECK(tracker.Find(tool) == nullptr, "tool windows are not app windows");
    CHECK(tracker.Find(owned) == nullptr, "owned popups are not app windows");
    CHECK(tracker.Find(owner) != nullptr);
    CHECK(tracker.Find(hidden) == nullptr, "never-shown windows are not tracked");

    // Title
    SetWindowTextW(app, L"renamed");
    CHECK(PumpUntil([&] { return log.Count(EventType::WindowTitleChanged, app) >= 1; }), "title event");
    CHECK(tracker.Find(app)->title == L"renamed");

    // Minimize / restore
    ShowWindow(app, SW_SHOWMINNOACTIVE);
    CHECK(PumpUntil([&] { return tracker.Find(app) && tracker.Find(app)->minimized; }), "minimized state");
    CHECK(log.Count(EventType::WindowMinimized, app) == 1);
    CHECK(tracker.Find(app) != nullptr, "minimized windows stay in the list");
    ShowWindow(app, SW_SHOWNOACTIVATE);
    CHECK(PumpUntil([&] { return tracker.Find(app) && !tracker.Find(app)->minimized; }), "restored state");
    CHECK(log.Count(EventType::WindowRestored, app) == 1);

    // Hide leaves the list, show brings it back
    ShowWindow(app, SW_HIDE);
    CHECK(PumpUntil([&] { return tracker.Find(app) == nullptr; }), "hidden window left the list");
    CHECK(log.Count(EventType::WindowDestroyed, app) == 1);
    ShowWindow(app, SW_SHOWNOACTIVATE);
    CHECK(PumpUntil([&] { return tracker.Find(app) != nullptr; }), "shown again");

    // Destroy
    DestroyWindow(app);
    CHECK(PumpUntil([&] { return tracker.Find(app) == nullptr; }), "destroyed window removed");
    CHECK(log.Count(EventType::WindowDestroyed, app) == 2);

    // Late Refresh() is the safety net for missed events.
    HWND late = MakeWindow(L"tracker late", WS_OVERLAPPEDWINDOW, 0);
    ShowWindow(late, SW_SHOWNOACTIVATE);
    tracker.Refresh(late);
    CHECK(tracker.Find(late) != nullptr, "explicit Refresh adds a window");

    DestroyWindow(late);
    DestroyWindow(owned);
    DestroyWindow(owner);
    DestroyWindow(tool);
    DestroyWindow(hidden);
    tracker.Stop();
    CHECK(tracker.Windows().empty());

    // Own windows are ignored by default (the shell never tracks itself).
    EventBus bus2;
    win::WindowTracker t2(bus2);
    CHECK(t2.Start());
    HWND mine = MakeWindow(L"tracker mine", WS_OVERLAPPEDWINDOW, 0);
    ShowWindow(mine, SW_SHOWNOACTIVATE);
    Pump(300);
    CHECK(t2.Find(mine) == nullptr, "own process windows skipped by default");
    DestroyWindow(mine);
}

void FocusDependent() {
    wchar_t on[8] = {};
    GetEnvironmentVariableW(L"VELORA_TEST_FOCUS", on, 8);
    if (std::wstring(on) != L"1") {
        std::printf("SKIP foreground + fullscreen live checks (set VELORA_TEST_FOCUS=1; they move focus)\n");
        ++g_skipped;
        return;
    }
    EventBus bus;
    Log log;
    for (auto t : {EventType::WindowActivated, EventType::FullscreenEntered, EventType::FullscreenLeft})
        bus.Subscribe(t, [&](const Event& e) { log.events.push_back(e); });
    win::WindowTracker tracker(bus, {true});
    CHECK(tracker.Start());

    HWND prior = GetForegroundWindow();
    HWND w = MakeWindow(L"tracker focus", WS_OVERLAPPEDWINDOW, 0);
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    if (GetForegroundWindow() != w) {
        std::printf("SKIP: OS refused to give the test window foreground\n");
        ++g_skipped;
    } else {
        CHECK(PumpUntil([&] { return log.Count(EventType::WindowActivated, w) >= 1; }), "activation event");
        CHECK(tracker.Foreground() == w);
        CHECK(!tracker.FullscreenActive());
    }

    // Fullscreen: a click-through, almost invisible window covering the monitor.
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    HWND fs = MakeWindow(L"tracker fullscreen", WS_POPUP, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, nullptr,
                         mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top);
    SetLayeredWindowAttributes(fs, 0, 1, LWA_ALPHA);
    ShowWindow(fs, SW_SHOW);
    SetForegroundWindow(fs);
    if (GetForegroundWindow() == fs) {
        CHECK(PumpUntil([&] { return tracker.FullscreenActive(); }), "fullscreen detected");
        CHECK(log.Count(EventType::FullscreenEntered) >= 1);
        DestroyWindow(fs);
        if (prior && IsWindow(prior)) SetForegroundWindow(prior);
        CHECK(PumpUntil([&] { return !tracker.FullscreenActive(); }), "fullscreen left");
        CHECK(log.Count(EventType::FullscreenLeft) >= 1);
    } else {
        std::printf("SKIP: fullscreen test window did not get foreground\n");
        ++g_skipped;
        DestroyWindow(fs);
    }
    DestroyWindow(w);
    if (prior && IsWindow(prior)) SetForegroundWindow(prior);
}

}  // namespace

int main() {
    PureHelpers();
    Lifecycle();
    FocusDependent();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("window_tracker_test: all checks passed (%d skipped)\n", g_skipped);
    return 0;
}
