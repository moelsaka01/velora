// Shared window chrome: traffic-light hit test, resize border, work-area maximise rectangle (pure, no windows).
#include <cstdio>

#include "render/tokens.h"
#include "ui/window_chrome.h"

using namespace velora;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_failed;                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)

int main() {
    namespace tw = tokens::window;
    const float y = tw::kTrafficCenterY;
    for (int i = 0; i < 3; ++i) {
        const float cx = tw::kTrafficLeft + tw::kTrafficDiameter / 2 + i * tw::kTrafficPitch;
        CHECK(ui::chrome::TrafficHit(cx, y) == i);
        CHECK(ui::chrome::TrafficHit(cx + tw::kTrafficDiameter / 2 + 2, y) == i);  // the +3 DIP slop
    }
    CHECK(ui::chrome::TrafficHit(0, 0) == -1);
    CHECK(ui::chrome::TrafficHit(200, y) == -1);

    const RECT win{100, 100, 900, 700};
    CHECK(ui::chrome::ResizeHitTest(win, POINT{100, 100}, 6) == HTTOPLEFT);
    CHECK(ui::chrome::ResizeHitTest(win, POINT{899, 699}, 6) == HTBOTTOMRIGHT);
    CHECK(ui::chrome::ResizeHitTest(win, POINT{100, 400}, 6) == HTLEFT);
    CHECK(ui::chrome::ResizeHitTest(win, POINT{500, 100}, 6) == HTTOP);
    CHECK(ui::chrome::ResizeHitTest(win, POINT{500, 400}, 6) == 0);
    CHECK(ui::chrome::ResizeHitTest(win, POINT{106, 106}, 6) == 0);

    // Primary monitor 1920x1080, menu bar 45 px on top, Dock 90 px at the bottom.
    const RECT mon{0, 0, 1920, 1080}, work{0, 45, 1920, 990};
    auto m = ui::chrome::MaximizedRect(mon, work);
    CHECK(m.x == 0 && m.y == 45 && m.w == 1920 && m.h == 945);
    // Second monitor to the left: positions are relative to that monitor's origin.
    const RECT mon2{-1280, 0, 0, 1024}, work2{-1280, 40, 0, 1024};
    m = ui::chrome::MaximizedRect(mon2, work2);
    CHECK(m.x == 0 && m.y == 40 && m.w == 1280 && m.h == 984);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
