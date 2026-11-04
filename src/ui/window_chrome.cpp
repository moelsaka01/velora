#include "ui/window_chrome.h"

#include <cmath>

#include "render/tokens.h"

namespace velora::ui::chrome {

namespace {
namespace tw = tokens::window;
}

int TrafficHit(float x, float y) {
    for (int i = 0; i < 3; ++i) {
        const float cx = tw::kTrafficLeft + tw::kTrafficDiameter / 2 + i * tw::kTrafficPitch;
        if (std::hypot(x - cx, y - tw::kTrafficCenterY) <= tw::kTrafficDiameter / 2 + 3) return i;
    }
    return -1;
}

void DrawTrafficLights(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b, bool active, bool dark, bool hoverGroup) {
    const D2D1_COLOR_F lights[3] = {D2D1::ColorF(0.90f, 0.33f, 0.29f), D2D1::ColorF(0.91f, 0.70f, 0.24f), D2D1::ColorF(0.30f, 0.75f, 0.36f)};
    const float d = tw::kTrafficDiameter, cy = tw::kTrafficCenterY;
    for (int i = 0; i < 3; ++i) {
        const float cx = tw::kTrafficLeft + d / 2 + i * tw::kTrafficPitch;
        b->SetColor(active ? lights[i] : (dark ? D2D1::ColorF(1, 1, 1, 0.20f) : D2D1::ColorF(0, 0, 0, 0.16f)));
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2, d / 2), b);
        b->SetColor(D2D1::ColorF(0, 0, 0, 0.18f));
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2 - 0.25f, d / 2 - 0.25f), b, 0.5f);
        if (hoverGroup && active) {  // glyphs appear while the pointer is over the group
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.55f));
            const float k = 3.0f;
            if (i == 0) {
                dc->DrawLine(D2D1::Point2F(cx - k, cy - k), D2D1::Point2F(cx + k, cy + k), b, 1.2f);
                dc->DrawLine(D2D1::Point2F(cx - k, cy + k), D2D1::Point2F(cx + k, cy - k), b, 1.2f);
            } else if (i == 1) {
                dc->DrawLine(D2D1::Point2F(cx - k, cy), D2D1::Point2F(cx + k, cy), b, 1.2f);
            } else {
                dc->DrawLine(D2D1::Point2F(cx - k, cy), D2D1::Point2F(cx + k, cy), b, 1.2f);
                dc->DrawLine(D2D1::Point2F(cx, cy - k), D2D1::Point2F(cx, cy + k), b, 1.2f);
            }
        }
    }
}

LRESULT ResizeHitTest(const RECT& wr, POINT pt, int b) {
    const bool l = pt.x < wr.left + b, r = pt.x >= wr.right - b, t = pt.y < wr.top + b, bo = pt.y >= wr.bottom - b;
    if (t && l) return HTTOPLEFT;
    if (t && r) return HTTOPRIGHT;
    if (bo && l) return HTBOTTOMLEFT;
    if (bo && r) return HTBOTTOMRIGHT;
    if (l) return HTLEFT;
    if (r) return HTRIGHT;
    if (t) return HTTOP;
    if (bo) return HTBOTTOM;
    return 0;
}

MaxRect MaximizedRect(const RECT& monitor, const RECT& work) {
    return {work.left - monitor.left, work.top - monitor.top, work.right - work.left, work.bottom - work.top};
}

void FillMinMaxInfo(HWND hwnd, MINMAXINFO* info, int minW, int minH) {
    info->ptMinTrackSize = POINT{minW, minH};
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const MaxRect m = MaximizedRect(mi.rcMonitor, mi.rcWork);
    info->ptMaxPosition = POINT{m.x, m.y};
    info->ptMaxSize = POINT{m.w, m.h};
}

}  // namespace velora::ui::chrome
