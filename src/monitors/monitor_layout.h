// Per-monitor menu bars and Docks: the pure planning logic (no windows.h), unit-tested with simulated monitor sets. Owner: B.
//
// Every monitor gets a menu bar (the primary one is the full bar, the others are secondary bars). The Dock appears on the primary
// monitor, on every monitor, or follows the pointer, depending on Settings (dock.on). The runtime (monitor_bars) turns a Plan into
// appbar windows; hot-plug, resolution/DPI changes and a changed primary monitor just produce a new Plan and a Diff against the
// old one. Monitors are identified by their device name (stable across HMONITOR handle changes).
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace velora::monitors {

struct Rc {
    int left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    bool Empty() const { return right <= left || bottom <= top; }
    bool Contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    bool Contains(const Rc& o) const { return o.left >= left && o.top >= top && o.right <= right && o.bottom <= bottom; }
    friend bool operator==(const Rc&, const Rc&) = default;
};

struct MonitorDesc {
    std::wstring id;  // device name, e.g. \\.\DISPLAY2
    Rc rect;          // virtual-screen pixels (may have negative coordinates)
    Rc work;          // work area before Velora's bars
    unsigned dpi = 96;
    bool primary = false;
};

enum class DockMode { Primary, Every, Follow };

inline DockMode ParseDockMode(const std::string& s) {
    if (s == "every") return DockMode::Every;
    if (s == "follow") return DockMode::Follow;
    return DockMode::Primary;  // unknown strings behave as the default
}

struct Plan {
    std::wstring primaryId;               // the full menu bar lives here
    std::wstring dockHomeId;              // the Dock owned by the switch manager lives here ("" = no monitors)
    std::vector<std::wstring> extraMenu;  // secondary menu bars
    std::vector<std::wstring> extraDock;  // additional Docks ("every" mode)
    friend bool operator==(const Plan&, const Plan&) = default;
};

// `followId` is the monitor the pointer rests on (only used in Follow mode; unknown/empty falls back to the primary monitor).
inline Plan MakePlan(const std::vector<MonitorDesc>& monitors, DockMode mode, const std::wstring& followId) {
    Plan p;
    if (monitors.empty()) return p;
    auto primary = std::find_if(monitors.begin(), monitors.end(), [](const MonitorDesc& m) { return m.primary; });
    if (primary == monitors.end()) primary = monitors.begin();  // no primary flag (should not happen): the first one
    p.primaryId = primary->id;
    p.dockHomeId = primary->id;
    if (mode == DockMode::Follow) {
        auto f = std::find_if(monitors.begin(), monitors.end(), [&](const MonitorDesc& m) { return m.id == followId; });
        if (f != monitors.end()) p.dockHomeId = f->id;
    }
    // Stable order: by position (left to right, top to bottom), so the result does not depend on enumeration order.
    std::vector<const MonitorDesc*> order;
    for (const auto& m : monitors) order.push_back(&m);
    std::sort(order.begin(), order.end(), [](const MonitorDesc* a, const MonitorDesc* b) {
        return a->rect.left != b->rect.left ? a->rect.left < b->rect.left : a->rect.top < b->rect.top;
    });
    for (const MonitorDesc* m : order) {
        if (m->id == p.primaryId) continue;
        p.extraMenu.push_back(m->id);
        if (mode == DockMode::Every) p.extraDock.push_back(m->id);
    }
    return p;
}

struct Diff {
    std::vector<std::wstring> createMenu, destroyMenu, createDock, destroyDock;
    bool primaryChanged = false, dockHomeChanged = false;
    bool Empty() const { return createMenu.empty() && destroyMenu.empty() && createDock.empty() && destroyDock.empty() && !primaryChanged && !dockHomeChanged; }
};

inline Diff Compare(const Plan& before, const Plan& after) {
    Diff d;
    auto has = [](const std::vector<std::wstring>& v, const std::wstring& id) { return std::find(v.begin(), v.end(), id) != v.end(); };
    for (const auto& id : after.extraMenu) if (!has(before.extraMenu, id)) d.createMenu.push_back(id);
    for (const auto& id : before.extraMenu) if (!has(after.extraMenu, id)) d.destroyMenu.push_back(id);
    for (const auto& id : after.extraDock) if (!has(before.extraDock, id)) d.createDock.push_back(id);
    for (const auto& id : before.extraDock) if (!has(after.extraDock, id)) d.destroyDock.push_back(id);
    d.primaryChanged = before.primaryId != after.primaryId;
    d.dockHomeChanged = before.dockHomeId != after.dockHomeId;
    return d;
}

// ---- sizes and work areas ------------------------------------------------------------------------------------------------

// Thickness of a bar in pixels on a monitor of the given DPI (rounded DIPs, then scaled: matches sw::AppBar).
inline int ThicknessPx(float dip, unsigned dpi) {
    const int d = static_cast<int>(dip + (dip >= 0 ? 0.5f : -0.5f));
    return (d * static_cast<int>(dpi) + 48) / 96;
}

// What a monitor's work area is once a top bar and/or a bottom bar of the given pixel thickness are registered on it.
inline Rc WorkAreaWith(const Rc& monitor, int topPx, int bottomPx) {
    Rc w = monitor;
    w.top += std::max(0, topPx);
    w.bottom -= std::max(0, bottomPx);
    if (w.bottom < w.top) w.bottom = w.top;
    return w;
}

struct Thickness {
    float menuDip = 30.0f, dockDip = 90.0f;
};

// Expected work area per monitor for a plan (index-aligned with `monitors`).
inline std::vector<Rc> ExpectedWorkAreas(const std::vector<MonitorDesc>& monitors, const Plan& plan, const Thickness& t) {
    std::vector<Rc> out;
    for (const auto& m : monitors) {
        const bool hasDock = m.id == plan.dockHomeId || std::find(plan.extraDock.begin(), plan.extraDock.end(), m.id) != plan.extraDock.end();
        out.push_back(WorkAreaWith(m.rect, ThicknessPx(t.menuDip, m.dpi), hasDock ? ThicknessPx(t.dockDip, m.dpi) : 0));
    }
    return out;
}

// The rectangle a maximised window gets on a monitor: its work area (never beyond the monitor).
inline Rc MaximizedWithin(const Rc& monitor, const Rc& work) {
    Rc r = work;
    r.left = std::max(r.left, monitor.left);
    r.top = std::max(r.top, monitor.top);
    r.right = std::min(r.right, monitor.right);
    r.bottom = std::min(r.bottom, monitor.bottom);
    return r;
}

// ---- restoring work areas ---------------------------------------------------------------------------------------------------

struct RecordedWork {
    std::wstring id;
    Rc work;
};
struct WorkFix {
    std::wstring id;
    Rc want;
};
// Which monitors' work areas to put back: the recorded one, for a monitor still present under the same name, when the recorded
// rectangle is valid, fits the monitor, and differs from the present one. A monitor that is gone, or whose layout changed so the
// recorded rectangle no longer fits, is left alone.
inline std::vector<WorkFix> PlanWorkAreaRestore(const std::vector<RecordedWork>& recorded, const std::vector<MonitorDesc>& current) {
    std::vector<WorkFix> out;
    for (const auto& r : recorded) {
        auto it = std::find_if(current.begin(), current.end(), [&](const MonitorDesc& m) { return m.id == r.id; });
        if (it == current.end() || r.work.Empty() || !it->rect.Contains(r.work)) continue;
        if (r.work == it->work) continue;
        out.push_back({r.id, r.work});
    }
    return out;
}

// ---- follow the pointer ---------------------------------------------------------------------------------------------------------

// The Dock follows the pointer only after it has rested on another monitor for `dwellMs` (passing over a monitor edge is not a move).
class FollowTracker {
public:
    explicit FollowTracker(std::uint64_t dwellMs = 350) : dwell_(dwellMs) {}
    // `under` = monitor id under the pointer ("" when unknown). Returns the id to move to, or "" for no change.
    std::wstring Feed(const std::wstring& under, std::uint64_t nowMs) {
        if (under.empty()) return {};
        if (current_.empty()) {
            current_ = under;
            return {};
        }
        if (under == current_) {
            candidate_.clear();
            return {};
        }
        if (under != candidate_) {
            candidate_ = under;
            since_ = nowMs ? nowMs : 1;
            return {};
        }
        if (nowMs - since_ >= dwell_) {
            current_ = under;
            candidate_.clear();
            return current_;
        }
        return {};
    }
    void Reset(const std::wstring& current) {
        current_ = current;
        candidate_.clear();
    }
    const std::wstring& Current() const { return current_; }

private:
    std::uint64_t dwell_, since_ = 0;
    std::wstring current_, candidate_;
};

}  // namespace velora::monitors
