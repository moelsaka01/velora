// Overview layout and hot-corner logic: pure (no windows.h), unit-tested. Owner: B.
//
// The thumbnails are laid out in justified rows: every row has one height, windows keep their aspect ratio, rows are
// centred, and the largest height that fits the area is used (never above the window's real size).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace velora::overview {

struct Size {
    float w = 0, h = 0;
};
struct Cell {
    float x = 0, y = 0, w = 0, h = 0;
};

struct LayoutParams {
    float areaW = 0, areaH = 0;
    float margin = 48;      // left and right
    float top = 24;         // above the first row
    float bottom = 24;      // below the last label
    float gap = 28;         // between thumbnails
    float labelH = 28;      // title under each thumbnail
    float maxHeightFrac = 0.62f;  // a single window never takes more than this part of the area's height
};

// One cell per window, in the same order. Empty input -> empty output.
inline std::vector<Cell> ComputeLayout(const std::vector<Size>& src, const LayoutParams& p) {
    std::vector<Cell> out(src.size());
    if (src.empty()) return out;
    const float availW = std::max(1.0f, p.areaW - 2 * p.margin);
    const float availH = std::max(1.0f, p.areaH - p.top - p.bottom);

    // Rows needed at row height H (greedy fill), and the total height they take.
    auto rowsAt = [&](float H, std::vector<std::vector<size_t>>* rows) {
        std::vector<std::vector<size_t>> r(1);
        float used = 0;
        for (size_t i = 0; i < src.size(); ++i) {
            const float h = std::min(H, std::max(1.0f, src[i].h));
            const float w = std::max(1.0f, src[i].w) * (h / std::max(1.0f, src[i].h));
            const float need = (r.back().empty() ? 0 : p.gap) + w;
            if (!r.back().empty() && used + need > availW) {
                r.emplace_back();
                used = 0;
            }
            used += (r.back().empty() ? 0 : p.gap) + w;
            r.back().push_back(i);
        }
        if (rows) *rows = r;
        return static_cast<float>(r.size()) * (H + p.labelH) + static_cast<float>(r.size() - 1) * p.gap;
    };

    float lo = 24, hi = std::max(24.0f, availH * p.maxHeightFrac);
    // Even the smallest height may not fit (a great many windows): then it is used anyway and the grid overflows downwards.
    if (rowsAt(lo, nullptr) > availH) hi = lo;
    for (int i = 0; i < 24 && hi - lo > 0.5f; ++i) {
        const float mid = (lo + hi) / 2;
        if (rowsAt(mid, nullptr) <= availH) lo = mid;
        else hi = mid;
    }
    std::vector<std::vector<size_t>> rows;
    const float H = lo;
    const float total = rowsAt(H, &rows);
    float y = p.top + std::max(0.0f, (availH - total) / 2);
    for (const auto& row : rows) {
        float rowW = 0;
        std::vector<Cell> cells;
        for (size_t i : row) {
            const float h = std::min(H, std::max(1.0f, src[i].h));
            const float w = std::max(1.0f, src[i].w) * (h / std::max(1.0f, src[i].h));
            cells.push_back({0, 0, w, h});
            rowW += w;
        }
        rowW += p.gap * static_cast<float>(row.size() - 1);
        float x = p.margin + std::max(0.0f, (availW - rowW) / 2);
        for (size_t k = 0; k < row.size(); ++k) {
            Cell c = cells[k];
            c.x = x;
            c.y = y + (H - c.h) / 2;  // shorter windows sit centred in the row
            out[row[k]] = c;
            x += c.w + p.gap;
        }
        y += H + p.labelH + p.gap;
    }
    return out;
}

// Index of the cell containing (x, y), or -1.
inline int CellAt(const std::vector<Cell>& cells, float x, float y) {
    for (size_t i = 0; i < cells.size(); ++i)
        if (x >= cells[i].x && x < cells[i].x + cells[i].w && y >= cells[i].y && y < cells[i].y + cells[i].h) return static_cast<int>(i);
    return -1;
}

// Keyboard: the cell nearest in the given direction (dx, dy in {-1,0,1}), or `cur` when there is none.
inline int Neighbour(const std::vector<Cell>& cells, int cur, int dx, int dy) {
    if (cur < 0 || cur >= static_cast<int>(cells.size())) return cells.empty() ? -1 : 0;
    const float cx = cells[cur].x + cells[cur].w / 2, cy = cells[cur].y + cells[cur].h / 2;
    int best = cur;
    float bestScore = 1e30f;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (static_cast<int>(i) == cur) continue;
        const float ox = cells[i].x + cells[i].w / 2 - cx, oy = cells[i].y + cells[i].h / 2 - cy;
        const float along = ox * static_cast<float>(dx) + oy * static_cast<float>(dy);
        if (along <= 1) continue;
        const float across = dx != 0 ? oy : ox;
        const float score = along + 3 * (across < 0 ? -across : across);
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Hot corner: fires once when the pointer has rested in the corner for `dwellMs`, and re-arms when it leaves.
class HotCorner {
public:
    explicit HotCorner(std::uint64_t dwellMs = 220) : dwell_(dwellMs) {}
    bool Feed(bool inCorner, std::uint64_t nowMs) {
        if (!inCorner) {
            entered_ = 0;
            fired_ = false;
            return false;
        }
        if (entered_ == 0) entered_ = nowMs ? nowMs : 1;
        if (!fired_ && nowMs - entered_ >= dwell_) {
            fired_ = true;
            return true;
        }
        return false;
    }

private:
    std::uint64_t dwell_, entered_ = 0;
    bool fired_ = false;
};

}  // namespace velora::overview
