// Layout maths and selection rules of the Files window: pure (no windows.h), unit-tested. Owner: C.
// All lengths are DIPs. Numbers come from docs/MACOS_UI_SPEC.md (sidebar 216 wide with a 32 pitch, toolbar about 52 tall with 36 tall
// controls); the list and icon metrics are working values.
#pragma once

#include <algorithm>
#include <cmath>
#include <set>

#include "render/tokens.h"

namespace velora::files {

namespace tokens = velora::tokens;

inline constexpr float kSidebarW = tokens::window::kSidebarWidth, kSidebarRow = tokens::window::kSidebarRowPitch;
inline constexpr float kToolbarH = tokens::window::kToolbarHeight, kControlH = tokens::window::kToolbarControl;
inline constexpr float kHeaderH = 28.0f, kRowH = 28.0f, kStatusH = 28.0f;
inline constexpr float kTrafficD = tokens::window::kTrafficDiameter, kTrafficPitch = tokens::window::kTrafficPitch;
inline constexpr float kTrafficLeft = tokens::window::kTrafficLeft, kTrafficCenterY = tokens::window::kTrafficCenterY;
inline constexpr float kIconCellW = 112.0f, kIconCellH = 100.0f, kIconSize = 56.0f;

struct Columns {
    float name = 0, modified = 0, size = 0, kind = 0;  // widths; 0 = column hidden
};
// Name takes what the fixed columns leave; narrow views drop Kind first, then Modified.
inline Columns ComputeColumns(float width) {
    Columns c;
    constexpr float kSize = 88, kModified = 148, kKind = 148, kMinName = 220;
    float rest = width;
    auto take = [&](float w, float* out) {
        if (rest - w >= kMinName) {
            *out = w;
            rest -= w;
        }
    };
    take(kSize, &c.size);
    take(kModified, &c.modified);
    take(kKind, &c.kind);
    c.name = rest;
    return c;
}

struct IconGrid {
    int columns = 1;
    float cellW = kIconCellW, cellH = kIconCellH, left = 0;
};
inline IconGrid ComputeIconGrid(float width) {
    IconGrid g;
    const float usable = std::max(width - 24.0f, kIconCellW);
    g.columns = std::max(1, static_cast<int>(usable / kIconCellW));
    g.left = std::max(12.0f, (width - g.columns * g.cellW) / 2);
    return g;
}

inline int RowCount(int count, int columns) { return columns <= 0 ? 0 : (count + columns - 1) / columns; }

inline float ClampScroll(float scroll, float contentH, float viewH) { return std::clamp(scroll, 0.0f, std::max(0.0f, contentH - viewH)); }

// Scroll offset that keeps [top, top + h) inside a viewport of `viewH`.
inline float ScrollToVisible(float scroll, float top, float h, float viewH) {
    if (top < scroll) return top;
    if (top + h > scroll + viewH) return top + h - viewH;
    return scroll;
}

// Item under (x, y) of a view whose first row starts at `top` (y is in the same space as `top`); -1 for none.
inline int ListHit(float y, float top, float scroll, int count) {
    if (y < top) return -1;
    const int i = static_cast<int>((y - top + scroll) / kRowH);
    return i >= 0 && i < count ? i : -1;
}
inline int GridHit(float x, float y, float top, float scroll, const IconGrid& g, int count) {
    if (y < top) return -1;
    const float gx = x - g.left;
    if (gx < 0) return -1;
    const int col = static_cast<int>(gx / g.cellW), row = static_cast<int>((y - top + scroll) / g.cellH);
    if (col >= g.columns || row < 0) return -1;
    const int i = row * g.columns + col;
    return i < count ? i : -1;
}

enum class Nav { Left, Right, Up, Down, Home, End, PageUp, PageDown };
// Where the arrow keys etc. move the focus. `columns` is 1 in the list view. -1 current = nothing focused yet.
inline int NextIndex(int current, Nav key, int columns, int count, int page) {
    if (count <= 0) return -1;
    if (current < 0) return (key == Nav::Up || key == Nav::Left || key == Nav::End || key == Nav::PageUp) ? count - 1 : 0;
    int n = current;
    switch (key) {
        case Nav::Left: n = columns > 1 ? current - 1 : current; break;
        case Nav::Right: n = columns > 1 ? current + 1 : current; break;
        case Nav::Up: n = current - columns; break;
        case Nav::Down: n = current + columns; break;
        case Nav::Home: n = 0; break;
        case Nav::End: n = count - 1; break;
        case Nav::PageUp: n = current - std::max(1, page) * columns; break;
        case Nav::PageDown: n = current + std::max(1, page) * columns; break;
    }
    return std::clamp(n, 0, count - 1);
}

// Click / Ctrl-click / Shift-click selection and the focus/anchor bookkeeping.
class Selection {
public:
    const std::set<int>& Items() const { return items_; }
    bool Has(int i) const { return items_.count(i) != 0; }
    int Anchor() const { return anchor_; }
    int Focus() const { return focus_; }
    bool Empty() const { return items_.empty(); }
    void Clear() {
        items_.clear();
        anchor_ = focus_ = -1;
    }
    void Only(int i) {
        items_.clear();
        if (i >= 0) items_.insert(i);
        anchor_ = focus_ = i;
    }
    void Click(int i, bool ctrl, bool shift) {
        if (i < 0) {
            if (!ctrl && !shift) Clear();
            return;
        }
        if (shift && anchor_ >= 0) {
            if (!ctrl) items_.clear();
            for (int k = std::min(anchor_, i); k <= std::max(anchor_, i); ++k) items_.insert(k);
            focus_ = i;
        } else if (ctrl) {
            if (!items_.erase(i)) items_.insert(i);
            anchor_ = focus_ = i;
        } else {
            Only(i);
        }
    }
    // Keyboard move: plain = select only the target; Shift = extend the range from the anchor.
    void Move(int to, bool shift) {
        if (to < 0) return;
        if (shift && anchor_ >= 0) {
            items_.clear();
            for (int k = std::min(anchor_, to); k <= std::max(anchor_, to); ++k) items_.insert(k);
            focus_ = to;
        } else {
            Only(to);
        }
    }
    void All(int count) {
        items_.clear();
        for (int i = 0; i < count; ++i) items_.insert(i);
        anchor_ = count > 0 ? 0 : -1;
        focus_ = count - 1;
    }

private:
    std::set<int> items_;
    int anchor_ = -1, focus_ = -1;
};

}  // namespace velora::files
