// Shared window chrome of Velora's own windows (Files, Settings): the drawn traffic lights, the resize border and the
// maximise rectangle. Owner: B.
//
// These windows are borderless popups with WS_THICKFRAME (the whole window is client area). A maximised popup would
// cover the whole monitor, including the Dock and the menu bar, so the maximised rectangle is the monitor's work area.
#pragma once

#include <d2d1_1.h>

#include "core/win.h"

namespace velora::ui::chrome {

// Traffic lights at the tokens' positions, in window DIPs. 0 = close, 1 = minimise, 2 = zoom, -1 = none.
int TrafficHit(float x, float y);
// Draws the three lights (grey while the window is inactive; glyphs while the pointer is over the group).
void DrawTrafficLights(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, bool active, bool dark, bool hoverGroup);

// HTLEFT ... HTBOTTOMRIGHT when `pt` (screen pixels) is on the resize border of `window`, otherwise 0.
LRESULT ResizeHitTest(const RECT& window, POINT pt, int borderPx);

// The rectangle a maximised window takes: the monitor's work area, relative to the monitor origin (MINMAXINFO convention).
struct MaxRect {
    int x = 0, y = 0, w = 0, h = 0;
};
MaxRect MaximizedRect(const RECT& monitor, const RECT& work);
// WM_GETMINMAXINFO: minimum track size and the work-area maximise rectangle of the window's monitor.
void FillMinMaxInfo(HWND hwnd, MINMAXINFO* info, int minWidthPx, int minHeightPx);

}  // namespace velora::ui::chrome
