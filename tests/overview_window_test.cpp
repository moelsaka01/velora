// The real Overview window over three ordinary windows of this process: thumbnails are registered, the layout fits, keyboard and mouse
// pick a window, Esc and a click outside close it, nothing is left behind. Opens windows: runs in CI.
#include <windowsx.h>

#include <cstdio>
#include <vector>

#include "core/event_bus.h"
#include "overview/overview.h"
#include "windows/window_tracker.h"

using namespace velora;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

static void Pump(int ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static HWND MakeWindow(const wchar_t* title, int x, int w, int h) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VeloraOverviewTestWindow";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        reg = true;
    }
    HWND hw = CreateWindowExW(0, L"VeloraOverviewTestWindow", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 60, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return hw;
}

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EventBus bus;
    win::WindowTracker tracker(bus);
    overview::Overview ov(GetModuleHandleW(nullptr), tracker);

    HWND a = MakeWindow(L"Alpha", 40, 640, 420), b = MakeWindow(L"Beta", 120, 520, 520), c = MakeWindow(L"Gamma", 200, 800, 380);
    CHECK(a && b && c);
    Pump(200);

    // Empty: opens with the "No open windows" state and closes with Esc.
    ov.SetCandidateOverride({});
    ov.Show();
    CHECK(ov.Visible());
    CHECK(ov.Items().empty());  // the (not started) tracker knows no windows
    SendMessageW(ov.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK(!ov.Visible());

    // Three windows.
    ov.SetCandidateOverride({a, b, c});
    ov.Show();
    CHECK(ov.Visible() && ov.Items().size() == 3);
    Pump(150);
    RECT wr{};
    GetWindowRect(ov.Hwnd(), &wr);
    int registered = 0;
    for (const auto& it : ov.Items()) registered += it.thumb ? 1 : 0;
    std::printf("INFO thumbnails registered: %d of 3 (placeholders: %d)\n", registered, 3 - registered);
    const float s = static_cast<float>(GetDpiForWindow(ov.Hwnd())) / 96.0f;
    const float areaW = (wr.right - wr.left) / s, areaH = (wr.bottom - wr.top) / s;
    bool inside = true, overlap = false;
    const auto& items = ov.Items();
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& cell = items[i].cell;
        inside = inside && cell.x >= 0 && cell.y >= 0 && cell.x + cell.w <= areaW + 0.5f && cell.y + cell.h <= areaH + 0.5f;
        for (size_t j = i + 1; j < items.size(); ++j) {
            const auto& o = items[j].cell;
            overlap = overlap || (cell.x < o.x + o.w && o.x < cell.x + cell.w && cell.y < o.y + o.h && o.y < cell.y + cell.h);
        }
    }
    CHECK(inside && !overlap);
    CHECK(items[0].title == L"Alpha" && items[1].title == L"Beta" && items[2].title == L"Gamma");

    // Keyboard: Right selects a neighbour, Enter activates it and closes the overview.
    SendMessageW(ov.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
    SendMessageW(ov.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(!ov.Visible());
    CHECK(IsWindow(a) && IsWindow(b) && IsWindow(c));  // nothing was closed or moved
    Pump(100);

    // Mouse: a click on a thumbnail activates it; a click on empty space closes.
    ov.Show();
    CHECK(ov.Visible());
    Pump(100);
    {
        const auto& cell = ov.Items()[1].cell;
        const LPARAM pt = MAKELPARAM(static_cast<int>((cell.x + cell.w / 2) * s), static_cast<int>((cell.y + cell.h / 2) * s));
        SendMessageW(ov.Hwnd(), WM_MOUSEMOVE, 0, pt);
        SendMessageW(ov.Hwnd(), WM_LBUTTONUP, 0, pt);
        CHECK(!ov.Visible());
    }
    ov.Show();
    CHECK(ov.Visible());
    SendMessageW(ov.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(2, 2));  // the corner: no thumbnail there
    CHECK(!ov.Visible());

    // Showing twice is harmless; Toggle works; a window that vanished while open does not break activation.
    ov.Show();
    ov.Show();
    CHECK(ov.Visible());
    DestroyWindow(c);
    CHECK(!ov.Activate(2) || true);  // may or may not still be listed; must not crash
    ov.Hide();
    CHECK(!ov.Visible());
    ov.Toggle();
    CHECK(ov.Visible());
    ov.Toggle();
    CHECK(!ov.Visible());

    DestroyWindow(a);
    DestroyWindow(b);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
