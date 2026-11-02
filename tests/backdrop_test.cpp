// ui/backdrop: the wallpaper crop that feeds the Dock shelf and the Apps panel, on a generated image (red left half, blue
// right half), plus a smoke call of the blurred-wallpaper helper against this machine's real wallpaper.
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "ui/backdrop.h"

using namespace velora::ui;

namespace {

// 24-bit bottom-up BMP, 64 x 32: x < 32 red, x >= 32 blue.
std::filesystem::path WriteBmp() {
    constexpr int w = 64, h = 32;
    const int rowBytes = (w * 3 + 3) / 4 * 4;
    std::vector<std::uint8_t> file(54 + rowBytes * h, 0);
    auto put32 = [&](size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) file[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, static_cast<std::uint32_t>(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    file[26] = 1;
    file[28] = 24;
    put32(34, rowBytes * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = &file[54 + y * rowBytes + x * 3];  // B, G, R
            if (x < 32) px[2] = 255;
            else px[0] = 255;
        }
    auto path = std::filesystem::temp_directory_path() / "velora_backdrop_test.bmp";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return path;
}

}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };

    auto bmp = WriteBmp();
    // A 640 x 320 "monitor" at the origin; the image has the same aspect ratio, so fill covers it exactly.
    const RECT mon{0, 0, 640, 320};
    Pixels left, right;
    check(LoadWallpaperRegion(bmp.wstring(), false, mon, RECT{0, 100, 200, 200}, 20, 10, &left), "crop of the left part loads");
    check(LoadWallpaperRegion(bmp.wstring(), false, mon, RECT{440, 100, 640, 200}, 20, 10, &right), "crop of the right part loads");
    check(left.width == 20 && left.height == 10 && left.bgra.size() == 20u * 10 * 4, "the output has the requested size");
    if (left.bgra.size() >= 4 && right.bgra.size() >= 4) {
        check(left.bgra[2] > 200 && left.bgra[0] < 60, "behind the left part the wallpaper is red");
        check(right.bgra[0] > 200 && right.bgra[2] < 60, "behind the right part the wallpaper is blue");
    }
    Pixels none;
    check(!LoadWallpaperRegion(L"C:\\this\\does\\not\\exist.bmp", false, mon, RECT{0, 0, 10, 10}, 4, 4, &none), "a missing file fails cleanly");
    check(!LoadWallpaperRegion(bmp.wstring(), false, mon, RECT{10, 10, 10, 20}, 4, 4, &none), "an empty region fails cleanly");

    // Real wallpaper: depends on the machine (a solid colour or a slideshow has no image), so only the contract is checked.
    Pixels blurred;
    if (BlurredWallpaper(RECT{0, 0, 1920, 1080}, RECT{500, 100, 1340, 700}, 4, 20.0f, 96, &blurred))
        check(blurred.width == 210 && blurred.height == 150 && blurred.bgra.size() == 210u * 150 * 4, "blurred wallpaper has the shrunk size");
    else
        std::printf("backdrop_test: no wallpaper image on this machine (the helper returned false)\n");

    std::filesystem::remove(bmp);
    if (!failed) std::printf("backdrop_test: all passed\n");
    return failed ? 1 : 0;
}
