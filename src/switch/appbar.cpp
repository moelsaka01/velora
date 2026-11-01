#include "switch/appbar.h"

#include <ShellScalingApi.h>
#include <shellapi.h>

#include <cmath>

#include "core/log.h"

namespace velora::sw {

namespace {

constexpr wchar_t kClassName[] = L"VeloraAppBar";

COLORREF ToColorRef(std::uint32_t argb) {
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

ATOM RegisterClassOnce(HINSTANCE inst) {
    static ATOM atom = [&] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = &AppBar::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        return RegisterClassExW(&wc);
    }();
    return atom;
}

}  // namespace

bool AppBar::Create(HINSTANCE instance, HMONITOR monitor, Edge edge, float thicknessDip, std::uint32_t argb,
                    AppBarOptions options) {
    if (hwnd_) return true;
    options_ = std::move(options);
    if (!RegisterClassOnce(instance)) return false;
    monitor_ = monitor ? monitor : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    edge_ = edge;
    thicknessDip_ = thicknessDip;
    brush_ = CreateSolidBrush(ToColorRef(argb));
    callbackMessage_ = RegisterWindowMessageW(L"VeloraAppBarMessage");

    // No WS_EX_APPWINDOW: stays out of the taskbar/Alt+Tab. NOACTIVATE: never steals focus.
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    if (options_.composition) ex |= WS_EX_NOREDIRECTIONBITMAP;
    hwnd_ = CreateWindowExW(ex, kClassName, L"Velora AppBar", WS_POPUP,
                            0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        log::Error("appbar: CreateWindowEx failed");
        Destroy();
        return false;
    }
    if (!Register()) {
        log::Warn("appbar: ABM_NEW failed (no shell tray?); running without work-area reservation");
        Destroy();
        return false;
    }
    return true;
}

void AppBar::Destroy() {
    Unregister();
    if (hwnd_) {
        HWND h = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(h);
    }
    if (brush_) {
        DeleteObject(brush_);
        brush_ = nullptr;
    }
}

bool AppBar::Register() {
    APPBARDATA abd{sizeof abd};
    abd.hWnd = hwnd_;
    abd.uCallbackMessage = callbackMessage_;
    if (!SHAppBarMessage(ABM_NEW, &abd)) return false;
    registered_ = true;
    ++registrations_;
    SetPos();
    return true;
}

void AppBar::Unregister() {
    if (!registered_ || !hwnd_) {
        registered_ = false;
        return;
    }
    APPBARDATA abd{sizeof abd};
    abd.hWnd = hwnd_;
    SHAppBarMessage(ABM_REMOVE, &abd);
    registered_ = false;
}

bool AppBar::Reregister() {
    if (!hwnd_) return false;
    Unregister();  // harmless if Explorer already forgot us
    return Register();
}

UINT AppBar::Dpi() const {
    UINT x = 96, y = 96;
    if (monitor_) GetDpiForMonitor(monitor_, MDT_EFFECTIVE_DPI, &x, &y);
    return x;
}

void AppBar::Reposition() {
    if (registered_) SetPos();
}

void AppBar::SetPos() {
    if (inSetPos_ || !hwnd_) return;
    inSetPos_ = true;

    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(monitor_, &mi)) {
        monitor_ = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfoW(monitor_, &mi);
    }
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor_, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    int height = MulDiv(static_cast<int>(std::lround(thicknessDip_)), static_cast<int>(dpiX), 96);

    APPBARDATA abd{sizeof abd};
    abd.hWnd = hwnd_;
    abd.uEdge = edge_ == Edge::Top ? ABE_TOP : ABE_BOTTOM;
    abd.rc = mi.rcMonitor;
    if (edge_ == Edge::Top) abd.rc.bottom = abd.rc.top + height;
    else abd.rc.top = abd.rc.bottom - height;

    SHAppBarMessage(ABM_QUERYPOS, &abd);
    // The system adjusts only the edges that collide with other appbars; pin the thickness.
    if (edge_ == Edge::Top) abd.rc.bottom = abd.rc.top + height;
    else abd.rc.top = abd.rc.bottom - height;
    SHAppBarMessage(ABM_SETPOS, &abd);

    rect_ = abd.rc;
    RECT w = options_.placement ? options_.placement(rect_, dpiX) : rect_;
    SetWindowPos(hwnd_, HWND_TOPMOST, w.left, w.top, w.right - w.left, w.bottom - w.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    inSetPos_ = false;
}

int RemoveRecordedAppbars(const SwitchState& state) {
    int sent = 0;
    for (std::uint64_t raw : state.appbarHwnds) {
        HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(raw));
        if (!h) continue;
        if (IsWindow(h)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != state.shellPid) continue;  // handle reused by another process window: not ours to touch
        }
        APPBARDATA abd{sizeof abd};
        abd.hWnd = h;
        SHAppBarMessage(ABM_REMOVE, &abd);
        ++sent;
    }
    if (sent) log::Info("recovery: ABM_REMOVE sent for " + std::to_string(sent) + " recorded appbar(s)");
    return sent;
}

LRESULT CALLBACK AppBar::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<AppBar*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || self->hwnd_ != h) {
        // Messages during creation/destruction, before/after hwnd_ is set.
        return DefWindowProcW(h, msg, wp, lp);
    }
    return self->OnMessage(msg, wp, lp);
}

LRESULT AppBar::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (callbackMessage_ && msg == callbackMessage_) {
        switch (wp) {
            case ABN_POSCHANGED:
                SetPos();
                break;
            case ABN_FULLSCREENAPP:
                // A fullscreen app is up: get out of its way; come back when it leaves.
                SetWindowPos(hwnd_, lp ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                break;
            default:
                break;
        }
        return 0;
    }
    if (options_.hook) {
        LRESULT r = 0;
        if (options_.hook(hwnd_, msg, wp, lp, r)) return r;
    }
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            if (brush_ && !options_.composition) FillRect(reinterpret_cast<HDC>(wp), &rc, brush_);
            return 1;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ACTIVATE:
            if (registered_) {
                APPBARDATA abd{sizeof abd};
                abd.hWnd = hwnd_;
                SHAppBarMessage(ABM_ACTIVATE, &abd);
            }
            break;
        case WM_WINDOWPOSCHANGED:
            if (registered_) {
                APPBARDATA abd{sizeof abd};
                abd.hWnd = hwnd_;
                SHAppBarMessage(ABM_WINDOWPOSCHANGED, &abd);
            }
            break;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            Reposition();
            break;
        case WM_CLOSE:
            return 0;  // only Destroy() may close an appbar
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::sw
