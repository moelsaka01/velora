#include "windows/window_tracker.h"

#include <dwmapi.h>
#include <shellapi.h>

#include "core/log.h"

namespace velora::win {

namespace {

WindowTracker* g_tracker = nullptr;  // WinEvent callbacks carry no user pointer; one tracker per process

bool IsCloaked(HWND h) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked != 0;
}

std::wstring TextOf(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return {};
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    n = GetWindowTextW(h, s.data(), n + 1);
    s.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return s;
}

std::wstring ClassOf(HWND h) {
    wchar_t c[256] = {};
    GetClassNameW(h, c, 256);
    return c;
}

bool IsShellOrDesktopClass(const std::wstring& c) {
    return c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd" || c == L"Shell_SecondaryTrayWnd" ||
           c == L"VeloraAppBar" || c == L"VeloraShellMain";
}

}  // namespace

// ---------------------------------------------------------------- pure helpers

bool IsFullscreenRect(const RECT& w, const RECT& m) {
    return w.left <= m.left && w.top <= m.top && w.right >= m.right && w.bottom >= m.bottom;
}

bool IsSelfElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e{};
    DWORD len = 0;
    bool elevated = GetTokenInformation(token, TokenElevation, &e, sizeof e, &len) && e.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

Elevation QueryElevation(DWORD pid) {
    Elevation r;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) {
        r.inaccessible = true;  // protected / system process
        return r;
    }
    HANDLE token = nullptr;
    if (!OpenProcessToken(p, TOKEN_QUERY, &token)) {
        // A medium-integrity process cannot open an elevated process' token.
        r.elevated = GetLastError() == ERROR_ACCESS_DENIED;
        r.inaccessible = !r.elevated;
    } else {
        TOKEN_ELEVATION e{};
        DWORD len = 0;
        if (GetTokenInformation(token, TokenElevation, &e, sizeof e, &len)) r.elevated = e.TokenIsElevated != 0;
        else r.inaccessible = true;
        CloseHandle(token);
    }
    CloseHandle(p);
    return r;
}

// ---------------------------------------------------------------- tracker

WindowTracker::WindowTracker(EventBus& bus, TrackerOptions options) : bus_(bus), options_(options) {}

const TrackedWindow* WindowTracker::Find(HWND h) const {
    auto it = windows_.find(h);
    return it == windows_.end() ? nullptr : &it->second;
}

bool WindowTracker::Start() {
    if (g_tracker) return false;  // single instance
    g_tracker = this;
    selfElevated_ = IsSelfElevated();

    struct Range { DWORD lo, hi; };
    // Out-of-context hooks only; SKIPOWNPROCESS unless tests asked for own windows.
    const DWORD flags = WINEVENT_OUTOFCONTEXT | (options_.includeOwnProcess ? 0 : WINEVENT_SKIPOWNPROCESS);
    const Range ranges[] = {
        {EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE},          // create, destroy, show, hide
        {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
    };
    for (const auto& r : ranges) {
        HWINEVENTHOOK h = SetWinEventHook(r.lo, r.hi, nullptr, &WindowTracker::HookProc, 0, 0, flags);
        if (!h) {
            log::Error("SetWinEventHook failed");
            Stop();
            return false;
        }
        hooks_.push_back(h);
    }

    EnumWindows(
        [](HWND h, LPARAM) -> BOOL {
            g_tracker->Refresh(h);
            return TRUE;
        },
        0);
    foreground_ = GetForegroundWindow();
    HookLocationFor(foreground_);
    EvaluateFullscreen();
    return true;
}

void WindowTracker::Stop() {
    for (auto h : hooks_) UnhookWinEvent(h);
    hooks_.clear();
    if (locationHook_) {
        UnhookWinEvent(locationHook_);
        locationHook_ = nullptr;
    }
    if (g_tracker == this) g_tracker = nullptr;
    windows_.clear();
    foreground_ = nullptr;
    fullscreenWindow_ = nullptr;
}

void CALLBACK WindowTracker::HookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!g_tracker || !hwnd) return;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    g_tracker->OnEvent(event, hwnd);
}

bool WindowTracker::IsAppWindow(HWND h) const {
    if (!IsWindow(h) || GetAncestor(h, GA_ROOT) != h) return false;  // top-level only
    if (!IsWindowVisible(h) || IsCloaked(h)) return false;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (GetWindow(h, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return false;
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return false;
    if (ex & WS_EX_NOACTIVATE && !(ex & WS_EX_APPWINDOW)) return false;
    if (IsShellOrDesktopClass(ClassOf(h))) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && !options_.includeOwnProcess) return false;
    return true;
}

TrackedWindow WindowTracker::Read(HWND h, const TrackedWindow* previous) const {
    TrackedWindow w;
    w.hwnd = h;
    GetWindowThreadProcessId(h, &w.pid);
    w.title = TextOf(h);
    w.className = ClassOf(h);
    w.minimized = IsIconic(h) != FALSE;
    w.cloaked = IsCloaked(h);
    w.controllable = previous ? previous->controllable : !QueryElevation(w.pid).Uncontrollable(selfElevated_);
    return w;
}

void WindowTracker::Post(EventType type, const TrackedWindow& w) {
    WindowEvent e;
    e.hwnd = static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(w.hwnd));
    e.pid = w.pid;
    e.elevated = !w.controllable;
    bus_.Post(type, e);
}

void WindowTracker::Refresh(HWND h) {
    auto it = windows_.find(h);
    if (!IsAppWindow(h)) {
        if (it != windows_.end()) {
            TrackedWindow gone = it->second;
            windows_.erase(it);
            if (fullscreenWindow_ == h) EvaluateFullscreen();
            Post(EventType::WindowDestroyed, gone);
        }
        return;
    }
    if (it == windows_.end()) {
        TrackedWindow w = Read(h, nullptr);
        windows_.emplace(h, w);
        Post(EventType::WindowCreated, w);
        return;
    }
    TrackedWindow& old = it->second;
    TrackedWindow now = Read(h, &old);
    bool titleChanged = now.title != old.title, minChanged = now.minimized != old.minimized;
    bool minimized = now.minimized;
    old = now;
    if (titleChanged) Post(EventType::WindowTitleChanged, now);
    if (minChanged) Post(minimized ? EventType::WindowMinimized : EventType::WindowRestored, now);
}

void WindowTracker::OnEvent(DWORD event, HWND hwnd) {
    switch (event) {
        case EVENT_SYSTEM_FOREGROUND: {
            foreground_ = hwnd;
            HookLocationFor(hwnd);
            Refresh(hwnd);
            if (const TrackedWindow* w = Find(hwnd)) Post(EventType::WindowActivated, *w);
            EvaluateFullscreen();
            break;
        }
        case EVENT_OBJECT_LOCATIONCHANGE:
            if (hwnd == foreground_) EvaluateFullscreen();
            break;
        case EVENT_OBJECT_CLOAKED:
        case EVENT_OBJECT_UNCLOAKED: {
            const TrackedWindow* before = Find(hwnd);
            TrackedWindow snapshot = before ? *before : TrackedWindow{};
            Refresh(hwnd);
            // A cloaked window leaves the app list (Refresh posts WindowDestroyed); also tell
            // listeners why, so Overview/Dock can distinguish "moved to another desktop".
            if (event == EVENT_OBJECT_CLOAKED && before) {
                snapshot.cloaked = true;
                Post(EventType::WindowCloaked, snapshot);
            } else if (event == EVENT_OBJECT_UNCLOAKED) {
                if (const TrackedWindow* w = Find(hwnd)) Post(EventType::WindowUncloaked, *w);
            }
            break;
        }
        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_CREATE:
        case EVENT_OBJECT_NAMECHANGE:
        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_SYSTEM_MINIMIZEEND:
            Refresh(hwnd);
            if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) EvaluateFullscreen();
            break;
        default:
            break;
    }
}

void WindowTracker::HookLocationFor(HWND fg) {
    // EVENT_OBJECT_LOCATIONCHANGE is very chatty; only listen to the foreground process.
    if (locationHook_) {
        UnhookWinEvent(locationHook_);
        locationHook_ = nullptr;
    }
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (!pid) return;
    DWORD flags = WINEVENT_OUTOFCONTEXT | (options_.includeOwnProcess ? 0 : WINEVENT_SKIPOWNPROCESS);
    locationHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                    &WindowTracker::HookProc, pid, 0, flags);
}

void WindowTracker::EvaluateFullscreen() {
    HWND candidate = nullptr;
    HWND fg = GetForegroundWindow();
    if (fg && IsWindowVisible(fg) && !IsIconic(fg) && !IsCloaked(fg) && !IsShellOrDesktopClass(ClassOf(fg))) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        RECT wr{};
        MONITORINFO mi{sizeof mi};
        if ((options_.includeOwnProcess || pid != GetCurrentProcessId()) && GetWindowRect(fg, &wr) &&
            GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi) && IsFullscreenRect(wr, mi.rcMonitor))
            candidate = fg;
    }
    if (candidate == fullscreenWindow_) return;
    HWND previous = fullscreenWindow_;
    fullscreenWindow_ = candidate;
    WindowEvent e;
    HWND subject = candidate ? candidate : previous;
    e.hwnd = static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(subject));
    if (subject) {
        DWORD pid = 0;
        GetWindowThreadProcessId(subject, &pid);
        e.pid = pid;
    }
    bus_.Post(candidate ? EventType::FullscreenEntered : EventType::FullscreenLeft, e);
}

}  // namespace velora::win
