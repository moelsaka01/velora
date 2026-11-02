// icon tiles: detection of filled rounded squares and composition of glyph icons onto the neutral tile.
// Pure pixel maths, no COM, no shell.
#include <cmath>
#include <cstdio>

#include "ui/icon_tile.h"

using namespace velora::ui;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

Pixels Blank(int n) {
    Pixels p;
    p.width = p.height = n;
    p.bgra.assign(static_cast<size_t>(n) * n * 4, 0);
    return p;
}
void Set(Pixels& p, int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a) {
    auto* o = &p.bgra[(static_cast<size_t>(y) * p.width + x) * 4];
    o[0] = b; o[1] = g; o[2] = r; o[3] = a;
}
const std::uint8_t* At(const Pixels& p, int x, int y) { return &p.bgra[(static_cast<size_t>(y) * p.width + x) * 4]; }

Pixels FullSquare(int n) {  // opaque blue everywhere (premultiplied = straight when opaque)
    Pixels p = Blank(n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) Set(p, x, y, 200, 90, 20, 255);
    return p;
}
Pixels Glyph(int n) {  // opaque red disc, radius 25 % of the edge, on transparent
    Pixels p = Blank(n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (std::hypot(x + 0.5 - n / 2.0, y + 0.5 - n / 2.0) < n * 0.25) Set(p, x, y, 0, 0, 255, 255);
    return p;
}
}  // namespace

int main() {
    const int n = 128;
    Pixels full = FullSquare(n), glyph = Glyph(n), empty = Blank(n);
    CHECK(SquircleCoverage(full) > 0.99 && IsFilledTile(full));
    CHECK(!IsFilledTile(glyph) && !IsFilledTile(empty));

    // A rounded square with transparent corners (what real app icons look like) counts as filled.
    Pixels rounded = Blank(n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            double dx = std::max(std::fabs(x + 0.5 - n / 2.0) - (n / 2.0 - 0.227 * n), 0.0);
            double dy = std::max(std::fabs(y + 0.5 - n / 2.0) - (n / 2.0 - 0.227 * n), 0.0);
            if (std::hypot(dx, dy) <= 0.227 * n) Set(rounded, x, y, 40, 120, 10, 255);
        }
    CHECK(IsFilledTile(rounded));
    // A disc is not a filled square (corners empty).
    Pixels disc = Blank(n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (std::hypot(x + 0.5 - n / 2.0, y + 0.5 - n / 2.0) < n * 0.5) Set(disc, x, y, 10, 10, 10, 255);
    CHECK(!IsFilledTile(disc));

    // Composition: same size; corners transparent; tile body opaque light grey; glyph kept in the centre, smaller.
    Pixels t = ComposeOnTile(glyph);
    CHECK(t.width == n && t.height == n && t.bgra.size() == glyph.bgra.size());
    CHECK(At(t, 0, 0)[3] == 0);                       // outside the tile
    const std::uint8_t* body = At(t, n / 2, n / 2 - static_cast<int>(n * 0.40));  // inside the tile, above the glyph
    CHECK(body[3] == 255 && body[0] > 200 && body[1] > 200 && body[2] > 200);
    const std::uint8_t* mid = At(t, n / 2, n / 2);
    CHECK(mid[3] == 255 && mid[2] > 240 && mid[1] < 20 && mid[0] < 20);  // red glyph on top
    // The glyph disc radius is 0.25 n; at 86 % of the tile it is ~0.20 n: a pixel at 0.23 n is tile again.
    const std::uint8_t* ring = At(t, n / 2 + static_cast<int>(n * 0.23), n / 2);
    CHECK(ring[2] > 200 && ring[1] > 200);
    // Tile is lighter at the top than at the bottom (gradient), and the shadow lies below it.
    CHECK(At(t, n / 2, static_cast<int>(n * 0.06))[2] > At(t, n / 2, static_cast<int>(n * 0.94))[2]);
    const std::uint8_t* shadow = At(t, n / 2, n - 3);
    CHECK(shadow[3] > 0 && shadow[3] < 120 && shadow[2] < 10);

    // EnsureTile: no-op for a filled icon, tiles a glyph, reports which.
    Pixels a = full, b = glyph;
    CHECK(!EnsureTile(&a) && a.bgra == full.bgra);
    CHECK(EnsureTile(&b) && b.bgra != glyph.bgra);
    // Premultiplied invariant: no channel exceeds alpha.
    bool pm = true;
    for (size_t i = 0; i < t.bgra.size(); i += 4)
        for (int c = 0; c < 3; ++c) pm = pm && t.bgra[i + c] <= t.bgra[i + 3] + 1;
    CHECK(pm);

    std::printf("icon_tile_test: %s\n", g_failed ? "FAIL" : "ok");
    return g_failed ? 1 : 0;
}
