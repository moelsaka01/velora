// Overview layout, keyboard neighbours and the hot-corner dwell (pure).
#include <cmath>
#include <cstdio>

#include "overview/overview_layout.h"

using namespace velora::overview;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

static bool Inside(const Cell& c, const LayoutParams& p) {
    return c.x >= p.margin - 0.01f && c.x + c.w <= p.areaW - p.margin + 0.01f && c.y >= p.top - 0.01f;
}
static bool Overlap(const Cell& a, const Cell& b) {
    return a.x < b.x + b.w - 0.01f && b.x < a.x + a.w - 0.01f && a.y < b.y + b.h - 0.01f && b.y < a.y + a.h - 0.01f;
}

int main() {
    LayoutParams p;
    p.areaW = 1920;
    p.areaH = 1000;

    CHECK(ComputeLayout({}, p).empty());

    // One window: centred, never larger than its real size, aspect kept.
    {
        auto c = ComputeLayout({{800, 600}}, p);
        CHECK(c.size() == 1 && c[0].w <= 800.01f && c[0].h <= 600.01f);
        CHECK(std::fabs(c[0].w / c[0].h - 800.0f / 600.0f) < 0.01f);
        CHECK(std::fabs((c[0].x + c[0].w / 2) - p.areaW / 2) < 1.0f);
    }
    // A tiny window is not enlarged beyond its size.
    {
        auto c = ComputeLayout({{200, 100}}, p);
        CHECK(c[0].w <= 200.01f && c[0].h <= 100.01f);
    }
    // Many windows of mixed shapes: all inside, none overlapping, order and aspect kept, labels fit below.
    for (int n : {2, 3, 5, 8, 13, 21}) {
        std::vector<Size> src;
        for (int i = 0; i < n; ++i) src.push_back({static_cast<float>(700 + (i * 137) % 900), static_cast<float>(450 + (i * 91) % 500)});
        auto c = ComputeLayout(src, p);
        CHECK(static_cast<int>(c.size()) == n);
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            ok = ok && Inside(c[i], p) && std::fabs(c[i].w / c[i].h - src[i].w / src[i].h) < 0.02f;
            for (int j = i + 1; j < n; ++j) ok = ok && !Overlap(c[i], c[j]);
        }
        CHECK(ok);
        float bottom = 0;
        for (const auto& cell : c) bottom = std::max(bottom, cell.y + cell.h + p.labelH);
        CHECK(bottom <= p.areaH - p.bottom + 1.0f);  // the labels fit inside the area
    }
    // More windows -> smaller thumbnails (monotonic), but never zero.
    {
        auto few = ComputeLayout(std::vector<Size>(3, {1200, 800}), p);
        auto many = ComputeLayout(std::vector<Size>(30, {1200, 800}), p);
        CHECK(many[0].h < few[0].h && many[0].h >= 24);
    }
    // A great many windows still produce a layout (it may overflow downwards, it must not crash or overlap).
    {
        auto c = ComputeLayout(std::vector<Size>(200, {1000, 700}), p);
        CHECK(c.size() == 200);
        bool ok = true;
        for (size_t i = 0; i + 1 < c.size(); ++i) ok = ok && !Overlap(c[i], c[i + 1]);
        CHECK(ok);
    }
    // Degenerate sources.
    {
        auto c = ComputeLayout({{0, 0}, {1000, 0}, {0, 600}}, p);
        CHECK(c.size() == 3 && c[0].w > 0 && c[1].w > 0 && c[2].w > 0);
    }

    // Hit test and keyboard neighbours on a 2x2 grid.
    {
        std::vector<Cell> g = {{0, 0, 100, 100}, {200, 0, 100, 100}, {0, 200, 100, 100}, {200, 200, 100, 100}};
        CHECK(CellAt(g, 50, 50) == 0 && CellAt(g, 250, 250) == 3 && CellAt(g, 150, 50) == -1);
        CHECK(Neighbour(g, 0, 1, 0) == 1 && Neighbour(g, 0, 0, 1) == 2 && Neighbour(g, 3, -1, 0) == 2 && Neighbour(g, 3, 0, -1) == 1);
        CHECK(Neighbour(g, 0, -1, 0) == 0 && Neighbour(g, 0, 0, -1) == 0);  // nothing there: stay
        CHECK(Neighbour(g, -1, 1, 0) == 0 && Neighbour({}, 0, 1, 0) == -1);
    }

    // Hot corner: dwell, fire once, re-arm after leaving.
    {
        HotCorner h(200);
        CHECK(!h.Feed(false, 1000));
        CHECK(!h.Feed(true, 1100));
        CHECK(!h.Feed(true, 1250));
        CHECK(h.Feed(true, 1300));    // 200 ms after entering
        CHECK(!h.Feed(true, 1500));   // once only
        CHECK(!h.Feed(false, 1600));
        CHECK(!h.Feed(true, 1700));   // brushing past does not fire
        CHECK(!h.Feed(false, 1750));
        CHECK(!h.Feed(true, 2000));
        CHECK(h.Feed(true, 2250));
    }
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
