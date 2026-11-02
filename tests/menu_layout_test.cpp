// Menu popup geometry against the measured spec (docs/MACOS_UI_SPEC.md) and the keyboard navigation rules.
#include <cmath>
#include <cstdio>

#include "menubar/menu_layout.h"

using namespace velora::menubar;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    // The measured Finder menu: 7 rows, 4 separators, 222 pt tall, 201.5 pt wide.
    std::vector<MenuItem> spec;
    for (int i = 0; i < 7; ++i) {
        spec.push_back({static_cast<unsigned>(i + 1), L"row"});
        if (i == 0 || i == 2 || i == 4 || i == 5) spec.push_back(Separator());
    }
    MenuLayout l = ComputeMenuLayout(spec);
    check(near(l.width, 201.5f), "popup width 201.5");
    check(near(l.height, 222.0f), "7 rows + 4 separators = 222 pt");
    check(near(l.top[0], 5.0f), "5 pt inner padding above the first row");
    check(near(l.bottom[0] - l.top[0], 24.0f), "24 pt rows");
    check(near(l.top[2] - l.top[0], 35.0f), "separator + row pitch 35 pt");

    // Hit testing.
    check(HitTestMenu(l, spec, 2.0f) == -1, "the top padding selects nothing");
    check(HitTestMenu(l, spec, 10.0f) == 0, "inside the first row");
    check(HitTestMenu(l, spec, l.top[1] + 5.0f) == -1, "a separator selects nothing");
    check(HitTestMenu(l, spec, l.height + 5.0f) == -1, "outside selects nothing");
    spec[2].enabled = false;
    check(HitTestMenu(l, spec, l.top[2] + 5.0f) == -1, "a disabled row selects nothing");
    spec[2].enabled = true;

    // Keyboard navigation skips separators and disabled rows and wraps around.
    std::vector<MenuItem> k = {{1, L"a"}, Separator(), {2, L"b", false, false}, {3, L"c"}};
    check(NextSelectable(k, -1, 1) == 0, "down from nothing picks the first row");
    check(NextSelectable(k, -1, -1) == 3, "up from nothing picks the last row");
    check(NextSelectable(k, 0, 1) == 3, "down skips a separator and a disabled row");
    check(NextSelectable(k, 3, 1) == 0, "down wraps around");
    check(NextSelectable(k, 0, -1) == 3, "up wraps around");
    check(NextSelectable({}, -1, 1) == -1, "an empty menu selects nothing");
    check(NextSelectable({Separator()}, -1, 1) == -1, "only separators select nothing");

    check(near(TitleCapsuleWidth(40.0f), 70.0f), "capsule: 15 pt padding each side (40 pt label = 70 pt capsule)");
    check(near(PopupLeft(1900, 201.5f, 0, 1920), 1718.5f), "popup stays inside the right edge");
    check(near(PopupLeft(-5, 201.5f, 0, 1920), 0.0f), "popup stays inside the left edge");
    check(near(PopupLeft(100, 201.5f, 0, 1920), 100.0f), "popup aligns with its anchor");

    if (!failed) std::printf("menu_layout_test: all passed\n");
    return failed ? 1 : 0;
}
