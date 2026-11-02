#include "ui/icon_tile.h"

#include <algorithm>
#include <cmath>

namespace velora::ui {
namespace {

// Signed distance (px, negative inside) to a rounded rectangle centred on (cx, cy) with half extents (hx, hy).
float RoundedSdf(float x, float y, float cx, float cy, float hx, float hy, float r) {
    const float qx = std::fabs(x - cx) - (hx - r), qy = std::fabs(y - cy) - (hy - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Tile colours (straight, 0..1): a light neutral that reads on the glass in both themes.
constexpr float kTop[3] = {0.996f, 0.996f, 1.000f}, kBottom[3] = {0.835f, 0.840f, 0.862f};

}  // namespace

double SquircleCoverage(const Pixels& p) {
    if (p.width <= 0 || p.height <= 0 || p.bgra.size() < static_cast<size_t>(p.width) * p.height * 4) return 0;
    const float w = static_cast<float>(p.width), h = static_cast<float>(p.height);
    const float inset = 0.02f * w, r = kTileRadius * w - inset;
    long inside = 0, opaque = 0;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) {
            if (RoundedSdf(x + 0.5f, y + 0.5f, w / 2, h / 2, w / 2 - inset, h / 2 - inset, r) > 0) continue;
            ++inside;
            if (p.bgra[(static_cast<size_t>(y) * p.width + x) * 4 + 3] >= 240) ++opaque;
        }
    return inside ? static_cast<double>(opaque) / static_cast<double>(inside) : 0;
}

bool IsFilledTile(const Pixels& p) { return SquircleCoverage(p) >= 0.92; }

bool IsLowRes(const Pixels& p) {
    constexpr int kN = 48;
    if (p.width < 96 || p.height < 96 || p.bgra.size() < static_cast<size_t>(p.width) * p.height * 4) return false;
    // Box-reduce to kN x kN, then compare against bilinear samples of the reduced image.
    std::vector<float> reduced(static_cast<size_t>(kN) * kN * 4, 0.0f);
    for (int dy = 0; dy < kN; ++dy)
        for (int dx = 0; dx < kN; ++dx) {
            const int x0 = dx * p.width / kN, x1 = (dx + 1) * p.width / kN, y0 = dy * p.height / kN, y1 = (dy + 1) * p.height / kN;
            float sum[4] = {0, 0, 0, 0};
            int n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x, ++n)
                    for (int c = 0; c < 4; ++c) sum[c] += p.bgra[(static_cast<size_t>(y) * p.width + x) * 4 + c];
            for (int c = 0; c < 4; ++c) reduced[(static_cast<size_t>(dy) * kN + dx) * 4 + c] = n ? sum[c] / n : 0;
        }
    double diff = 0;
    size_t count = 0;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) {
            const float u = (x + 0.5f) * kN / p.width - 0.5f, v = (y + 0.5f) * kN / p.height - 0.5f;
            const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
            const float fx = u - x0, fy = v - y0;
            for (int c = 0; c < 4; ++c) {
                float s = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const int sx = std::clamp(x0 + i, 0, kN - 1), sy = std::clamp(y0 + j, 0, kN - 1);
                        s += (i ? fx : 1 - fx) * (j ? fy : 1 - fy) * reduced[(static_cast<size_t>(sy) * kN + sx) * 4 + c];
                    }
                diff += std::fabs(s - p.bgra[(static_cast<size_t>(y) * p.width + x) * 4 + c]);
                ++count;
            }
        }
    return count && diff / static_cast<double>(count) < 2.5;
}

bool IsBlankIcon(const Pixels& p) {
    const size_t total = static_cast<size_t>(p.width) * p.height;
    if (total == 0 || p.bgra.size() < total * 4) return true;
    size_t opaque = 0;
    double lum = 0;
    for (size_t i = 0; i < total; ++i) {
        const std::uint8_t* s = &p.bgra[i * 4];
        if (s[3] <= 16) continue;
        ++opaque;
        lum += (0.114 * s[0] + 0.587 * s[1] + 0.299 * s[2]) / s[3];  // unpremultiplied luminance, 0..1
    }
    if (opaque * 25 < total) return true;  // < 4 % of the area
    return lum / static_cast<double>(opaque) > 0.94;  // white on a light tile
}

Pixels ComposeOnTile(const Pixels& src) {
    Pixels out;
    out.width = src.width;
    out.height = src.height;
    out.bgra.assign(static_cast<size_t>(src.width) * src.height * 4, 0);
    const float w = static_cast<float>(src.width), h = static_cast<float>(src.height);
    const float margin = kTileMargin * w, half = w / 2 - margin, r = kTileRadius * (w - 2 * margin);
    const float shadowDy = 0.012f * w, shadowBlur = 0.022f * w;
    // Icon box inside the tile.
    const float box = (src.lowRes ? kTileContentLowRes : kTileContent) * (w - 2 * margin), bx = (w - box) / 2, by = (h - box) / 2;

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            // Shadow: the tile shifted down, softened by the distance outside it.
            const float sd = RoundedSdf(px, py - shadowDy, w / 2, h / 2, half, half, r);
            float a = 0.20f * (1.0f - Smooth((sd + shadowBlur * 0.5f) / shadowBlur));
            float c[3] = {0, 0, 0};  // premultiplied accumulator (shadow is black)
            // Tile body, vertical gradient, a hairline of light on the top edge.
            const float d = RoundedSdf(px, py, w / 2, h / 2, half, half, r);
            const float cover = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (cover > 0) {
                const float t = std::clamp((py - margin) / (h - 2 * margin), 0.0f, 1.0f);
                // Inner highlight: a thin bright ring just inside the edge, strongest at the top and gone by mid-height;
                // inner shade: the bottom edge sinks a little.
                const float ringW = std::max(1.0f, 0.012f * w);
                const float edge = std::clamp(1.0f + d / ringW, 0.0f, 1.0f);  // 1 at the edge, 0 ringW inside
                const float hi = edge * std::clamp(1.0f - t * 2.2f, 0.0f, 1.0f);
                const float sh = edge * std::clamp((t - 0.7f) * 3.3f, 0.0f, 1.0f);
                float tc[3];
                for (int i = 0; i < 3; ++i) {
                    float v = kTop[i] + (kBottom[i] - kTop[i]) * t;
                    v = v + (1.0f - v) * 0.85f * hi;  // toward white
                    v = v * (1.0f - 0.07f * sh);      // toward grey
                    tc[i] = std::clamp(v, 0.0f, 1.0f);
                }
                for (int i = 0; i < 3; ++i) c[i] = c[i] * (1 - cover) + tc[i] * cover;
                a = a * (1 - cover) + cover;
            }
            // Icon, bilinear from the source (downscale factor ~0.78), over the tile.
            float ic[3] = {0, 0, 0}, ia = 0;
            const float u = (px - bx) / box * src.width - 0.5f, v = (py - by) / box * src.height - 0.5f;
            if (px >= bx && px < bx + box && py >= by && py < by + box) {
                const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
                const float fx = u - x0, fy = v - y0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const int sx = std::clamp(x0 + i, 0, src.width - 1), sy = std::clamp(y0 + j, 0, src.height - 1);
                        const float wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                        const std::uint8_t* s = &src.bgra[(static_cast<size_t>(sy) * src.width + sx) * 4];
                        ic[0] += wgt * s[2] / 255.0f;  // premultiplied R
                        ic[1] += wgt * s[1] / 255.0f;
                        ic[2] += wgt * s[0] / 255.0f;
                        ia += wgt * s[3] / 255.0f;
                    }
            }
            const float* pm = c;  // already premultiplied: black shadow + tile * cover
            float res[3], ra = ia + a * (1 - ia);
            for (int i = 0; i < 3; ++i) res[i] = ic[i] + pm[i] * (1 - ia);
            std::uint8_t* o = &out.bgra[(static_cast<size_t>(y) * src.width + x) * 4];
            o[0] = static_cast<std::uint8_t>(std::clamp(res[2], 0.0f, 1.0f) * 255 + 0.5f);
            o[1] = static_cast<std::uint8_t>(std::clamp(res[1], 0.0f, 1.0f) * 255 + 0.5f);
            o[2] = static_cast<std::uint8_t>(std::clamp(res[0], 0.0f, 1.0f) * 255 + 0.5f);
            o[3] = static_cast<std::uint8_t>(std::clamp(ra, 0.0f, 1.0f) * 255 + 0.5f);
        }
    }
    return out;
}

bool EnsureTile(Pixels* p) {
    if (!p || p->width <= 0 || IsFilledTile(*p)) return false;
    *p = ComposeOnTile(*p);
    return true;
}

}  // namespace velora::ui
