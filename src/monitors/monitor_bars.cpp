#include "monitors/monitor_bars.h"

#include <ShellScalingApi.h>

#include <algorithm>

#include "core/log.h"
#include "core/text.h"
#include "render/tokens.h"
#include "switch/taskbar.h"

namespace velora::monitors {

namespace {

BOOL CALLBACK Collect(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(m, &mi)) return TRUE;
    LiveMonitor l;
    l.handle = m;
    l.desc.id = mi.szDevice;
    l.desc.rect = {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom};
    l.desc.work = {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom};
    l.desc.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    UINT x = 96, y = 96;
    if (SUCCEEDED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &x, &y))) l.desc.dpi = x;
    reinterpret_cast<std::vector<LiveMonitor>*>(lp)->push_back(std::move(l));
    return TRUE;
}

HMONITOR HandleOf(const std::vector<LiveMonitor>& live, const std::wstring& id) {
    for (const auto& l : live)
        if (l.desc.id == id) return l.handle;
    return nullptr;
}

std::uint64_t Raw(HWND h) { return static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(h)); }

}  // namespace

std::vector<LiveMonitor> EnumerateMonitors() {
    std::vector<LiveMonitor> out;
    EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&out));
    return out;
}

MonitorBars::MonitorBars(HINSTANCE instance, EventBus& bus, win::WindowTracker& tracker, sw::SwitchManager& manager,
                         menubar::MenuBar::Actions menuActions, dock::Dock::Actions dockActions)
    : instance_(instance), bus_(bus), tracker_(tracker), manager_(manager), menuActions_(std::move(menuActions)), dockActions_(std::move(dockActions)) {}

MonitorBars::~MonitorBars() { Shutdown(); }

MonitorBars::ExtraMenu* MonitorBars::FindMenu(const std::wstring& id) {
    for (auto& m : menus_)
        if (m->id == id) return m.get();
    return nullptr;
}

MonitorBars::ExtraDock* MonitorBars::FindDock(const std::wstring& id) {
    for (auto& d : docks_)
        if (d->id == id) return d.get();
    return nullptr;
}

void MonitorBars::RecordWithManager(const std::vector<LiveMonitor>& live, const std::vector<std::wstring>& newIds) {
    std::vector<HWND> hwnds;
    for (auto& m : menus_) hwnds.push_back(m->bar.Hwnd());
    for (auto& d : docks_) hwnds.push_back(d->bar.Hwnd());
    std::vector<MonitorState> fresh;
    if (!newIds.empty()) {
        for (const auto& s : sw::SnapshotMonitors())
            if (std::find(newIds.begin(), newIds.end(), s.deviceName) != newIds.end()) fresh.push_back(s);
    }
    (void)live;
    manager_.RecordExtras(hwnds, fresh);
}

void MonitorBars::Sync() {
    if (!manager_.Entered()) return;
    const auto live = EnumerateMonitors();
    std::vector<MonitorDesc> descs;
    for (const auto& l : live) descs.push_back(l.desc);
    if (descs.empty()) return;
    active_ = true;

    Plan probe = MakePlan(descs, DockMode::Primary, L"");
    if (follow_.Current().empty() || !HandleOf(live, follow_.Current())) follow_.Reset(probe.primaryId);
    const Plan next = MakePlan(descs, mode_, mode_ == DockMode::Follow ? follow_.Current() : std::wstring{});
    const Diff diff = Compare(plan_, next);

    // 1. Monitors that were not there when Velora Mode started: record their work areas before any bar lands on them, so exit, a
    //    crash and --restore put every monitor back.
    {
        std::vector<std::wstring> ids = {next.primaryId, next.dockHomeId};
        ids.insert(ids.end(), next.extraMenu.begin(), next.extraMenu.end());
        ids.insert(ids.end(), next.extraDock.begin(), next.extraDock.end());
        RecordWithManager(live, ids);
    }

    // 2. The switch manager's own bars follow the plan (primary changed / the Dock follows the pointer).
    if (HMONITOR pm = HandleOf(live, next.primaryId); pm && manager_.MenuBarBar().Hwnd() && manager_.MenuBarBar().Monitor() != pm) {
        log::Info("monitors: the menu bar moves to the primary monitor");
        manager_.MenuBarBar().MoveToMonitor(pm);
    }
    if (HMONITOR dm = HandleOf(live, next.dockHomeId); dm && manager_.DockBar().Hwnd() && manager_.DockBar().Monitor() != dm) {
        log::Info("monitors: the Dock moves to another monitor");
        manager_.DockBar().MoveToMonitor(dm);
    }

    // 3. Instances that are no longer wanted.
    for (const auto& id : diff.destroyDock) {
        auto it = std::find_if(docks_.begin(), docks_.end(), [&](const auto& d) { return d->id == id; });
        if (it == docks_.end()) continue;
        (*it)->dock->Detach();
        (*it)->bar.Destroy();
        docks_.erase(it);
    }
    for (const auto& id : diff.destroyMenu) {
        auto it = std::find_if(menus_.begin(), menus_.end(), [&](const auto& m) { return m->id == id; });
        if (it == menus_.end()) continue;
        (*it)->menu->Detach();
        (*it)->bar.Destroy();
        menus_.erase(it);
    }

    // 4. New ones.
    for (const auto& id : diff.createMenu) {
        HMONITOR mon = HandleOf(live, id);
        if (!mon || FindMenu(id)) continue;
        auto e = std::make_unique<ExtraMenu>();
        e->id = id;
        e->menu = std::make_unique<menubar::MenuBar>(bus_, tracker_, menuActions_);
        e->menu->SetSecondary(true);
        if (!e->bar.Create(instance_, mon, sw::Edge::Top, tokens::size::kMenuBarHeight, tokens::kLight.surfaceStrong.argb, e->menu->Options())) {
            log::Warn("monitors: menu bar appbar unavailable on a secondary monitor");
            continue;
        }
        e->menu->Attach(e->bar.Hwnd());
        menus_.push_back(std::move(e));
    }
    for (const auto& id : diff.createDock) {
        HMONITOR mon = HandleOf(live, id);
        if (!mon || FindDock(id)) continue;
        auto e = std::make_unique<ExtraDock>();
        e->id = id;
        e->dock = std::make_unique<dock::Dock>(bus_, tracker_, dockActions_);
        if (!e->bar.Create(instance_, mon, sw::Edge::Bottom, e->dock->ReservedDip(), tokens::kLight.surfaceStrong.argb, e->dock->Options())) {
            log::Warn("monitors: Dock appbar unavailable on a secondary monitor");
            continue;
        }
        e->dock->Attach(&e->bar);
        docks_.push_back(std::move(e));
    }

    // 5. Instances that stay: their monitor handle may have changed and the resolution / DPI may differ.
    for (auto& m : menus_)
        if (HMONITOR h = HandleOf(live, m->id)) m->bar.MoveToMonitor(h);
    for (auto& d : docks_)
        if (HMONITOR h = HandleOf(live, d->id)) d->bar.MoveToMonitor(h);

    plan_ = next;
    RecordWithManager(live, {});
    if (!diff.Empty())
        log::Info("monitors: " + std::to_string(live.size()) + " monitor(s), " + std::to_string(menus_.size()) + " secondary menu bar(s), " +
                  std::to_string(docks_.size() + 1) + " Dock(s)");
}

void MonitorBars::PollFollow() {
    if (mode_ != DockMode::Follow || !active_ || !manager_.Entered()) return;
    POINT pt{};
    if (!GetCursorPos(&pt)) return;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    HMONITOR m = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    if (!m || !GetMonitorInfoW(m, &mi)) return;
    if (!follow_.Feed(mi.szDevice, GetTickCount64()).empty()) Sync();
}

void MonitorBars::ReregisterAll() {
    for (auto& m : menus_) m->bar.Reregister();
    for (auto& d : docks_) d->bar.Reregister();
}

void MonitorBars::Shutdown() {
    for (auto& d : docks_) {
        d->dock->Detach();
        d->bar.Destroy();
    }
    docks_.clear();
    for (auto& m : menus_) {
        m->menu->Detach();
        m->bar.Destroy();
    }
    menus_.clear();
    plan_ = {};
    active_ = false;
}

}  // namespace velora::monitors
