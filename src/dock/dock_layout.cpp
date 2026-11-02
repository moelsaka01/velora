#include "dock/dock_layout.h"

#include <algorithm>
#include <cmath>

#include "render/tokens.h"

namespace velora::dock {

namespace {
namespace t = tokens;
}

DockGeometry ComputeGeometry(size_t itemCount, size_t pinnedCount, const std::vector<float>* scales) {
    DockGeometry g;
    pinnedCount = std::min(pinnedCount, itemCount);
    const bool sep = pinnedCount > 0 && itemCount > pinnedCount;
    float x = t::size::kDockPadding;
    for (size_t i = 0; i < itemCount; ++i) {
        if (sep && i == pinnedCount) {
            g.separatorX = x;           // x is one spacing after the previous icon: the middle of the wider gap
            x += t::dock::kSpacing;
        }
        float s = scales && i < scales->size() ? (*scales)[i] : 1.0f;
        float size = t::size::kDockIcon * s;
        g.itemX.push_back(x);
        g.itemSize.push_back(size);
        x += size + t::dock::kSpacing;
    }
    g.pillWidth = itemCount ? x - t::dock::kSpacing + t::size::kDockPadding : 2 * t::size::kDockPadding;
    g.pillHeight = t::dock::kShelfHeight;
    return g;
}

float IconBaseline() { return t::size::kDockPadding + t::size::kDockIcon; }
float DotCentre() { return IconBaseline() + t::dock::kDotBelowIcon; }

float MagnifyReach() { return t::dock::kMagnifyReach * (t::size::kDockIcon + t::dock::kSpacing); }

float MagnifyFalloff(float distance, float reach) {
    if (reach <= 0) return 0;
    const float u = std::fabs(distance) / reach;
    if (u >= 1.0f) return 0.0f;
    static constexpr float kPoints[4][2] = {{0.0f, 1.0f}, {0.5f, 0.414f}, {0.8f, 0.1f}, {1.0f, 0.0f}};
    for (int k = 0; k < 3; ++k) {
        if (u <= kPoints[k + 1][0]) {
            float f = (u - kPoints[k][0]) / (kPoints[k + 1][0] - kPoints[k][0]);
            return kPoints[k][1] + (kPoints[k + 1][1] - kPoints[k][1]) * f;
        }
    }
    return 0.0f;
}

std::vector<float> MagnifyScales(size_t itemCount, size_t pinnedCount, float pointerFromCenter, float peak) {
    std::vector<float> ones(itemCount, 1.0f);
    if (itemCount == 0 || peak <= 1.0f) return ones;
    const DockGeometry base = ComputeGeometry(itemCount, pinnedCount, &ones);
    const float reach = MagnifyReach();
    std::vector<float> baseCentre(itemCount);
    for (size_t i = 0; i < itemCount; ++i) baseCentre[i] = base.itemX[i] + base.itemSize[i] / 2 - base.pillWidth / 2;

    auto scalesAt = [&](float u) {
        std::vector<float> s(itemCount);
        for (size_t i = 0; i < itemCount; ++i) s[i] = 1.0f + (peak - 1.0f) * MagnifyFalloff(u - baseCentre[i], reach);
        return s;
    };
    // Where the base position u ends up once the shelf has stretched for a pointer at u (monotonic in practice).
    auto landing = [&](float u) {
        std::vector<float> s = scalesAt(u);
        DockGeometry g = ComputeGeometry(itemCount, pinnedCount, &s);
        auto centre = [&](size_t i) { return g.itemX[i] + g.itemSize[i] / 2 - g.pillWidth / 2; };
        if (u <= baseCentre.front()) return centre(0) + (u - baseCentre.front());
        if (u >= baseCentre.back()) return centre(itemCount - 1) + (u - baseCentre.back());
        size_t i = 0;
        while (i + 2 < itemCount && u > baseCentre[i + 1]) ++i;
        float f = (u - baseCentre[i]) / (baseCentre[i + 1] - baseCentre[i]);
        return centre(i) + (centre(i + 1) - centre(i)) * f;
    };
    float lo = baseCentre.front() - 120.0f, hi = baseCentre.back() + 120.0f;
    for (int it = 0; it < 28; ++it) {
        float mid = (lo + hi) / 2;
        (landing(mid) < pointerFromCenter ? lo : hi) = mid;
    }
    return scalesAt((lo + hi) / 2);
}

float MaxGrowth(size_t itemCount, size_t pinnedCount, float peak) {
    if (itemCount == 0 || peak <= 1.0f) return 0;
    std::vector<float> ones(itemCount, 1.0f);
    const float rest = ComputeGeometry(itemCount, pinnedCount, &ones).pillWidth;
    float widest = rest;
    for (float p = -rest / 2; p <= rest / 2; p += 6.0f) {
        auto s = MagnifyScales(itemCount, pinnedCount, p, peak);
        widest = std::max(widest, ComputeGeometry(itemCount, pinnedCount, &s).pillWidth);
    }
    return widest - rest;
}

SourceRect WallpaperSourceRect(int imgW, int imgH, int monW, int monH, bool stretch, int rx, int ry, int rw, int rh) {
    SourceRect r;
    if (imgW <= 0 || imgH <= 0 || monW <= 0 || monH <= 0) return r;
    float sx, sy, offX = 0, offY = 0;
    if (stretch) {
        sx = static_cast<float>(monW) / imgW;
        sy = static_cast<float>(monH) / imgH;
    } else {
        sx = sy = std::max(static_cast<float>(monW) / imgW, static_cast<float>(monH) / imgH);
        offX = (monW - imgW * sx) / 2;
        offY = (monH - imgH * sy) / 2;
    }
    r.x = (rx - offX) / sx;
    r.y = (ry - offY) / sy;
    r.w = rw / sx;
    r.h = rh / sy;
    return r;
}

namespace {

// One box-blur pass along one axis: `n` samples starting at `base`, `step` bytes apart per sample.
void BlurLine(const std::uint8_t* in, std::uint8_t* out, int n, size_t step, int radius) {
    for (int c = 0; c < 4; ++c) {
        long long sum = 0;
        for (int k = -radius; k <= radius; ++k) sum += in[static_cast<size_t>(std::clamp(k, 0, n - 1)) * step + c];
        const int window = 2 * radius + 1;
        for (int i = 0; i < n; ++i) {
            out[static_cast<size_t>(i) * step + c] = static_cast<std::uint8_t>((sum + window / 2) / window);
            sum += in[static_cast<size_t>(std::min(i + radius + 1, n - 1)) * step + c];
            sum -= in[static_cast<size_t>(std::max(i - radius, 0)) * step + c];
        }
    }
}

}  // namespace

void BoxBlur(std::vector<std::uint8_t>& pixels, int width, int height, int radius, int passes) {
    if (radius <= 0 || width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height * 4) return;
    std::vector<std::uint8_t> tmp(pixels.size());
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < height; ++y)
            BlurLine(pixels.data() + static_cast<size_t>(y) * width * 4, tmp.data() + static_cast<size_t>(y) * width * 4, width, 4, radius);
        for (int x = 0; x < width; ++x)
            BlurLine(tmp.data() + static_cast<size_t>(x) * 4, pixels.data() + static_cast<size_t>(x) * 4, height,
                     static_cast<size_t>(width) * 4, radius);
    }
}

}  // namespace velora::dock
