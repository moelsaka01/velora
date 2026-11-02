// Dock geometry, hover magnification and the backdrop helpers: pure maths (no windows.h), unit-tested. Owner: B.
//
// Numbers come from docs/MACOS_UI_SPEC.md (measured shelf, icon, spacing, padding). The magnification curve and
// its reach are those of PuruVJ/macos-web (MIT, see THIRD_PARTY_NOTICES.md): a piecewise-linear falloff over six
// icon pitches; the peak size is Velora's own working value (tokens::dock::kMagnifyPeak).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace velora::dock {

// DIPs. x = 0 is the shelf's left edge, y = 0 its top edge; icons sit on a fixed baseline and grow upwards.
struct DockGeometry {
    float pillWidth = 0, pillHeight = 0;  // the shelf
    std::vector<float> itemX;             // left edge of each icon
    std::vector<float> itemSize;          // edge of each (square) icon: kDockIcon x its scale
    float separatorX = -1;                // between pinned and unpinned running apps (-1: none)
};
// `scales` (one per item, 1 = resting size) is optional.
DockGeometry ComputeGeometry(size_t itemCount, size_t pinnedCount, const std::vector<float>* scales = nullptr);

// Baseline of the icons below the shelf top, and the dot centre below it.
float IconBaseline();  // kDockPadding + kDockIcon
float DotCentre();     // baseline + kDotBelowIcon

// Falloff 1 (pointer on the icon) .. 0 (beyond `reach`), piecewise linear through the macos-web points
// 1 / 0.414 / 0.1 / 0 at 0 / 0.5 / 0.8 / 1 of the reach.
float MagnifyFalloff(float distance, float reach);
float MagnifyReach();  // kMagnifyReach x (icon + spacing)
// Scale of every icon for a pointer `pointerFromCenter` DIPs right of the shelf centre. The pointer is mapped back
// through the magnified layout, so the icon under it is the one that grows, wherever the shelf has stretched to.
std::vector<float> MagnifyScales(size_t itemCount, size_t pinnedCount, float pointerFromCenter, float peak);
// How much wider than at rest the shelf gets at its widest.
float MaxGrowth(size_t itemCount, size_t pinnedCount, float peak);

// ---- backdrop ----
// Where a wallpaper image (imgW x imgH) lands when drawn on a monitor (monW x monH): `stretch` fills exactly,
// otherwise it covers the monitor keeping its aspect ratio (centred). Returns the part of the image behind the
// monitor-relative pixel region (rx, ry, rw, rh).
struct SourceRect {
    float x = 0, y = 0, w = 0, h = 0;
};
SourceRect WallpaperSourceRect(int imgW, int imgH, int monW, int monH, bool stretch, int rx, int ry, int rw, int rh);
// Separable box blur of premultiplied 8-bit 4-channel pixels (edges clamped), `passes` times: three passes with
// radius r approximate a Gaussian of sigma about r.
void BoxBlur(std::vector<std::uint8_t>& pixels, int width, int height, int radius, int passes = 3);

}  // namespace velora::dock
