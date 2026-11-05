#include "switch/taskbar.h"

#include "monitors/monitor_layout.h"

#include <shellapi.h>

#include <algorithm>

#include "core/log.h"
#include "core/runtime.h"

namespace velora::sw {

namespace {

struct Finder {
    std::vector<HWND> out;
    const std::wstring* primary;
    const std::wstring* secondary;
};

BOOL CALLBACK EnumTray(HWND h, LPARAM lp) {
    auto* f = reinterpret_cast<Finder*>(lp);
    wchar_t cls[160];
    int n = GetClassNameW(h, cls, static_cast<int>(std::size(cls)));
    if (n > 0 && (*f->primary == cls || *f->secondary == cls)) f->out.push_back(h);
    return TRUE;
}

// Windows 10 only: the Start button is a separate top-level window with an
// atom class (0xC017). Absent on Windows 11; harmless if missing.
HWND FindStartButton() {
    if (CurrentProfile().IsTest()) return nullptr;
    return FindWindowExW(nullptr, nullptr, MAKEINTATOM(0xC017), nullptr);
}

void SetOne(HWND h, bool visible) {
    UINT flags = (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
    SetWindowPos(h, nullptr, 0, 0, 0, 0, flags);
}

}  // namespace

std::vector<HWND> FindTrayWindows() {
    Finder f{{}, &CurrentProfile().trayClass, &CurrentProfile().secondaryTrayClass};
    EnumWindows(EnumTray, reinterpret_cast<LPARAM>(&f));
    return f.out;
}

bool AnyTaskbarVisible() {
    for (HWND h : FindTrayWindows())
        if (IsWindowVisible(h)) return true;
    return false;
}

bool HasRealTaskbar() { return !CurrentProfile().IsTest() && FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr; }

void SetTaskbarsVisible(bool visible) {
    for (HWND h : FindTrayWindows())
        if ((IsWindowVisible(h) != FALSE) != visible) SetOne(h, visible);
    if (HWND start = FindStartButton())
        if ((IsWindowVisible(start) != FALSE) != visible) SetOne(start, visible);
}

bool WaitTaskbarsVisible(bool visible, DWORD timeoutMs) {
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        std::vector<HWND> trays = FindTrayWindows();
        bool done = !trays.empty();
        for (HWND h : trays)
            if ((IsWindowVisible(h) != FALSE) != visible) done = false;
        if (done) return true;
        if (GetTickCount64() >= deadline) return false;
        Sleep(2);
    }
}

static UINT TrayAppBarState(HWND tray) {
    APPBARDATA abd{sizeof abd};
    abd.hWnd = tray;
    return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
}

bool GetTaskbarAutoHide() {
    if (CurrentProfile().IsTest()) return false;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    return tray && (TrayAppBarState(tray) & ABS_AUTOHIDE) != 0;
}

void SetTaskbarAutoHide(bool autoHide) {
    if (CurrentProfile().IsTest()) return;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) return;
    UINT state = TrayAppBarState(tray);
    APPBARDATA abd{sizeof abd};
    abd.hWnd = tray;
    abd.lParam = static_cast<LPARAM>(autoHide ? (state | ABS_AUTOHIDE) : (state & ~static_cast<UINT>(ABS_AUTOHIDE)));
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

namespace {
struct MonCollector {
    std::vector<MonitorState> out;
    std::vector<HWND> trays;
};

BOOL CALLBACK EnumMon(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    auto* c = reinterpret_cast<MonCollector*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(m, &mi)) return TRUE;
    MonitorState s;
    s.deviceName = mi.szDevice;
    s.workArea = {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom};
    s.taskbarVisible = false;
    for (HWND t : c->trays)
        if (IsWindowVisible(t) && MonitorFromWindow(t, MONITOR_DEFAULTTONEAREST) == m) s.taskbarVisible = true;
    c->out.push_back(std::move(s));
    return TRUE;
}
}  // namespace

std::vector<MonitorState> SnapshotMonitors() {
    MonCollector c;
    c.trays = FindTrayWindows();
    EnumDisplayMonitors(nullptr, nullptr, EnumMon, reinterpret_cast<LPARAM>(&c));
    return c.out;
}

namespace {

struct CurrentMonitor {
    std::wstring name;
    RECT work, mon;
};

std::vector<CurrentMonitor> CurrentMonitors() {
    std::vector<CurrentMonitor> current;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof mi;
            if (GetMonitorInfoW(m, &mi))
                reinterpret_cast<std::vector<CurrentMonitor>*>(lp)->push_back({mi.szDevice, mi.rcWork, mi.rcMonitor});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&current));
    return current;
}

std::vector<monitors::MonitorDesc> AsDescs(const std::vector<CurrentMonitor>& current) {
    std::vector<monitors::MonitorDesc> out;
    for (const auto& c : current) {
        monitors::MonitorDesc d;
        d.id = c.name;
        d.rect = {c.mon.left, c.mon.top, c.mon.right, c.mon.bottom};
        d.work = {c.work.left, c.work.top, c.work.right, c.work.bottom};
        out.push_back(std::move(d));
    }
    return out;
}

// Pure rules in monitors::PlanWorkAreaRestore (unit-tested with simulated monitor sets).
std::vector<monitors::WorkFix> WorkFixes(const SwitchState& state) {
    std::vector<monitors::RecordedWork> rec;
    for (const auto& m : state.monitors)
        rec.push_back({m.deviceName, {m.workArea.left, m.workArea.top, m.workArea.right, m.workArea.bottom}});
    return monitors::PlanWorkAreaRestore(rec, AsDescs(CurrentMonitors()));
}

}  // namespace

int ReclaimWorkAreas(const SwitchState& state) {
    int changed = 0;
    for (const auto& fix : WorkFixes(state)) {
        RECT want{fix.want.left, fix.want.top, fix.want.right, fix.want.bottom};
        if (SystemParametersInfoW(SPI_SETWORKAREA, 0, &want, SPIF_SENDCHANGE)) {
            ++changed;
            log::Info("reclaimed work area on a monitor");
        }
    }
    return changed;
}

bool AnyWorkAreaReserved() {
    for (const auto& m : CurrentMonitors())
        if (!EqualRect(&m.work, &m.mon)) return true;
    return false;
}

bool WorkAreasMatch(const SwitchState& state) { return WorkFixes(state).empty(); }

std::wstring CurrentWallpaper() {
    wchar_t buf[MAX_PATH * 2] = {};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, static_cast<UINT>(std::size(buf)), buf, 0)) return {};
    return buf;
}

std::uint64_t ProcessStartTime(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u)) return 0;
    return (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}

}  // namespace velora::sw
