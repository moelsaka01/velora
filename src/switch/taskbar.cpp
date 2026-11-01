#include "switch/taskbar.h"

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
        bool done = true;
        for (HWND h : FindTrayWindows())
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

int ReclaimWorkAreas(const SwitchState& state) {
    int changed = 0;
    struct Cur { std::wstring name; RECT work, mon; };
    std::vector<Cur> current;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof mi;
            if (GetMonitorInfoW(m, &mi))
                reinterpret_cast<std::vector<Cur>*>(lp)->push_back({mi.szDevice, mi.rcWork, mi.rcMonitor});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&current));

    for (const auto& rec : state.monitors) {
        auto it = std::find_if(current.begin(), current.end(), [&](const Cur& c) { return c.name == rec.deviceName; });
        if (it == current.end()) continue;
        RECT want{rec.workArea.left, rec.workArea.top, rec.workArea.right, rec.workArea.bottom};
        if (want.right <= want.left || want.bottom <= want.top) continue;
        if (EqualRect(&want, &it->work)) continue;
        RECT inter;
        if (!IntersectRect(&inter, &want, &it->mon) || !EqualRect(&inter, &want)) continue;  // layout changed: leave alone
        if (SystemParametersInfoW(SPI_SETWORKAREA, 0, &want, SPIF_SENDCHANGE)) {
            ++changed;
            log::Info("reclaimed work area on a monitor");
        }
    }
    return changed;
}

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
