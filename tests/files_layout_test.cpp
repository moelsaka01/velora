// Files window layout maths and selection rules (pure; no window is created).
#include <cmath>
#include <cstdio>

#include "files/files_layout.h"

using namespace velora::files;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    // Spec numbers (docs/MACOS_UI_SPEC.md).
    check(kSidebarW == 216.0f && kSidebarRow == 32.0f && kControlH == 36.0f && kToolbarH == 52.0f, "spec sidebar / toolbar metrics");
    check(near(kTrafficPitch, 23.0f) && near(kTrafficD, 13.0f), "traffic light pitch 23, diameter 13");

    // Columns: everything fits in a wide view; narrow views drop Kind, then Modified; name never below its minimum.
    Columns wide = ComputeColumns(900);
    check(wide.size > 0 && wide.modified > 0 && wide.kind > 0 && near(wide.name + wide.size + wide.modified + wide.kind, 900), "wide view shows all columns");
    Columns mid = ComputeColumns(500);
    check(mid.kind == 0 && mid.modified > 0 && mid.name >= 220, "narrow view drops Kind first");
    Columns small = ComputeColumns(330);
    check(small.modified == 0 && small.kind == 0 && small.name >= 220, "very narrow view keeps Name and Size");
    check(ComputeColumns(100).name == 100, "tiny view: Name takes all");

    // Icon grid.
    IconGrid g = ComputeIconGrid(700);
    check(g.columns == 6 && g.left >= 12, "700 DIPs wide = 6 columns of 112");
    check(ComputeIconGrid(50).columns == 1, "at least one column");
    check(RowCount(13, 6) == 3 && RowCount(0, 6) == 0 && RowCount(5, 0) == 0, "row count");

    // Hit testing.
    check(ListHit(10, 80, 0, 5) == -1, "above the list");
    check(ListHit(80, 80, 0, 5) == 0 && ListHit(80 + kRowH, 80, 0, 5) == 1, "rows from the top");
    check(ListHit(80 + 5 * kRowH + 1, 80, 0, 5) == -1, "below the last row");
    check(ListHit(80, 80, kRowH, 5) == 1, "scroll shifts the rows");
    check(GridHit(5, 100, 80, 0, g, 20) == -1 || g.left > 5, "left of the grid");
    check(GridHit(g.left + 10, 85, 80, 0, g, 20) == 0, "first cell");
    check(GridHit(g.left + g.cellW + 10, 85, 80, 0, g, 20) == 1, "second cell");
    check(GridHit(g.left + 10, 85 + g.cellH, 80, 0, g, 20) == g.columns, "second row");
    check(GridHit(g.left + g.cellW * g.columns + 5, 85, 80, 0, g, 20) == -1, "right of the grid");
    check(GridHit(g.left + 10, 85 + 4 * g.cellH, 80, 0, g, 10) == -1, "past the last item");

    // Scrolling.
    check(near(ClampScroll(500, 300, 200), 100), "scroll clamps to content - view");
    check(near(ClampScroll(-5, 300, 200), 0) && near(ClampScroll(10, 100, 200), 0), "no scroll when everything fits");
    check(near(ScrollToVisible(0, 300, 28, 200), 128), "scroll down to reveal an item");
    check(near(ScrollToVisible(200, 50, 28, 200), 50), "scroll up to reveal an item");
    check(near(ScrollToVisible(40, 60, 28, 200), 40), "visible items do not scroll");

    // Keyboard moves.
    check(NextIndex(-1, Nav::Down, 1, 10, 5) == 0 && NextIndex(-1, Nav::Up, 1, 10, 5) == 9, "first move from nothing");
    check(NextIndex(3, Nav::Down, 1, 10, 5) == 4 && NextIndex(3, Nav::Up, 1, 10, 5) == 2, "list up/down");
    check(NextIndex(3, Nav::Left, 1, 10, 5) == 3 && NextIndex(3, Nav::Right, 1, 10, 5) == 3, "left/right do nothing in a list");
    check(NextIndex(3, Nav::Right, 4, 10, 2) == 4 && NextIndex(3, Nav::Left, 4, 10, 2) == 2, "grid left/right");
    check(NextIndex(3, Nav::Down, 4, 10, 2) == 7 && NextIndex(7, Nav::Down, 4, 10, 2) == 9, "grid down clamps to the last item");
    check(NextIndex(3, Nav::Up, 4, 10, 2) == 0, "grid up clamps to the first item");
    check(NextIndex(3, Nav::Home, 1, 10, 5) == 0 && NextIndex(3, Nav::End, 1, 10, 5) == 9, "home / end");
    check(NextIndex(3, Nav::PageDown, 1, 10, 5) == 8 && NextIndex(3, Nav::PageUp, 1, 10, 5) == 0, "page moves");
    check(NextIndex(0, Nav::Down, 1, 0, 5) == -1, "empty folder");

    // Selection.
    Selection s;
    s.Click(2, false, false);
    check(s.Items().size() == 1 && s.Has(2) && s.Anchor() == 2 && s.Focus() == 2, "click selects one");
    s.Click(5, false, true);
    check(s.Items().size() == 4 && s.Has(2) && s.Has(5) && s.Has(3), "shift extends from the anchor");
    s.Click(1, true, false);
    check(s.Items().size() == 5 && s.Has(1), "ctrl adds");
    s.Click(3, true, false);
    check(!s.Has(3) && s.Items().size() == 4, "ctrl toggles off");
    s.Click(-1, false, false);
    check(s.Empty() && s.Focus() == -1, "click on nothing clears");
    s.Click(4, false, false);
    s.Move(6, true);
    check(s.Items().size() == 3 && s.Has(4) && s.Has(6), "shift + arrow extends");
    s.Move(5, true);
    check(s.Items().size() == 2 && !s.Has(6), "shift + arrow back shrinks");
    s.Move(8, false);
    check(s.Items().size() == 1 && s.Has(8) && s.Anchor() == 8, "plain move selects only the target");
    s.All(4);
    check(s.Items().size() == 4 && s.Focus() == 3, "select all");
    s.All(0);
    check(s.Empty(), "select all of nothing");

    std::printf("%s\n", failed ? "FAILED" : "PASS");
    return failed ? 1 : 0;
}
