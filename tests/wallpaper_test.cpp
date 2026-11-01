// wallpaper: the generator and the BMP writer. The system side (IDesktopWallpaper apply/restore) changes the
// user's desktop and is checked manually, never here.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "wallpaper/wallpaper.h"

using namespace velora::wallpaper;

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
    Image dark = Generate(320, 180, true, 0);
    Image light = Generate(320, 180, false, 0);
    CHECK(dark.width == 320 && dark.height == 180 && dark.bgr.size() == 320u * 180u * 3u);
    CHECK(AverageLuma(dark) < AverageLuma(light));
    CHECK(AverageLuma(dark) < 110 && AverageLuma(light) > 150);

    // Deterministic, and variants differ.
    CHECK(Generate(64, 36, true, 1).bgr == Generate(64, 36, true, 1).bgr);
    CHECK(Generate(64, 36, true, 1).bgr != Generate(64, 36, true, 2).bgr);

    // Not flat: a gradient with visible structure (top row differs from bottom row, many distinct values).
    auto px = [&](const Image& im, int x, int y, int c) { return im.bgr[(static_cast<size_t>(y) * im.width + x) * 3 + c]; };
    CHECK(px(dark, 160, 5, 0) != px(dark, 160, 175, 0) || px(dark, 160, 5, 2) != px(dark, 160, 175, 2));
    int distinct = 0;
    std::vector<bool> seen(256, false);
    for (int x = 0; x < dark.width; ++x) {
        auto v = px(dark, x, 90, 0);
        if (!seen[v]) {
            seen[v] = true;
            ++distinct;
        }
    }
    CHECK(distinct > 20);

    // Degenerate sizes do not crash.
    CHECK(Generate(0, 0, true, 0).bgr.size() == 3);
    CHECK(Generate(1, 1, false, 3).bgr.size() == 3);

    // BMP: header and size (rows padded to 4 bytes), pixel order bottom-up.
    auto dir = std::filesystem::temp_directory_path() / L"velora_wallpaper_test";
    std::filesystem::create_directories(dir);
    auto file = dir / L"t.bmp";
    Image small = Generate(5, 3, true, 0);  // 5*3 = 15 bytes per row -> padded to 16
    CHECK(WriteBmp(file, small));
    std::ifstream f(file, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(data.size() == 54u + 16u * 3u);
    CHECK(data.size() > 2 && data[0] == 'B' && data[1] == 'M');
    if (data.size() >= 54u + 16u * 3u) {
        // First stored row is the image's last row.
        CHECK(static_cast<unsigned char>(data[54]) == small.bgr[(2 * 5 + 0) * 3 + 0]);
    }
    CHECK(!WriteBmp(dir / L"bad.bmp", Image{}));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    if (!g_failed) std::printf("wallpaper_test: all passed\n");
    return g_failed ? 1 : 0;
}
