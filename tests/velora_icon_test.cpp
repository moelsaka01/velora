// The embedded Velora app icon (src/shell.rc): decodes, scales to the Dock's device pixels at 100/150/200 %
// and keeps its transparent rounded corners.
#include <cstdio>

#include "core/win.h"
#include "ui/ui_common.h"

using namespace velora;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)
}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (int px : {48, 72, 96, 128}) {
        ui::Pixels p;
        CHECK(ui::LoadVeloraIcon(px, &p));
        CHECK(p.width == px && p.height == px && p.bgra.size() == static_cast<size_t>(px) * px * 4);
        if (p.bgra.size() != static_cast<size_t>(px) * px * 4) continue;
        CHECK(p.bgra[3] < 16);                                                        // corner pixel is transparent
        size_t centre = (static_cast<size_t>(px / 2) * px + px / 2) * 4;
        CHECK(p.bgra[centre + 3] > 240);                                              // the middle is opaque
        for (size_t i = 0; i < p.bgra.size(); i += 4)                                 // premultiplied: colour <= alpha (cubic scaling may overshoot by a few steps)
            if (p.bgra[i] > p.bgra[i + 3] + 4 || p.bgra[i + 1] > p.bgra[i + 3] + 4 || p.bgra[i + 2] > p.bgra[i + 3] + 4) {
                CHECK(false);
                break;
            }
    }
    ui::Pixels bad;
    CHECK(!ui::LoadVeloraIcon(0, &bad));
    CHECK(!ui::LoadVeloraIcon(5000, &bad));
    if (!g_failed) std::printf("velora_icon_test: all passed\n");
    return g_failed ? 1 : 0;
}
