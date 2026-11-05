// Per-monitor menu bars and Docks (runtime). Owner: B.
//
// The switch manager keeps owning the primary menu bar and one Dock appbar. This class adds, from the pure plan in
// monitor_layout.h: a secondary menu bar on every other monitor, and (Settings > Dock on > every monitor) a Dock on every other
// monitor. Each extra is its own appbar on its own monitor, sized for that monitor's DPI, with its own MenuBar / Dock instance. It
// also moves the manager's bars when the primary monitor changes or, in "follow the mouse" mode, when the pointer rests on another
// monitor. Sync() is idempotent and is called after Enter, on display changes (debounced by the caller) and when Settings change.
//
// Not verified on real multi-monitor hardware: see docs/VALIDATION.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/event_bus.h"
#include "dock/dock.h"
#include "menubar/menubar.h"
#include "monitors/monitor_layout.h"
#include "switch/switch_manager.h"
#include "windows/window_tracker.h"

namespace velora::monitors {

// All monitors now (device names, rectangles, work areas, effective DPI, primary flag).
struct LiveMonitor {
    MonitorDesc desc;
    HMONITOR handle = nullptr;
};
std::vector<LiveMonitor> EnumerateMonitors();

class MonitorBars {
public:
    MonitorBars(HINSTANCE instance, EventBus& bus, win::WindowTracker& tracker, sw::SwitchManager& manager, menubar::MenuBar::Actions menuActions,
                dock::Dock::Actions dockActions);
    ~MonitorBars();
    MonitorBars(const MonitorBars&) = delete;
    MonitorBars& operator=(const MonitorBars&) = delete;

    // dock.on from Settings; applied by the next Sync().
    void SetDockMode(DockMode mode) { mode_ = mode; }
    DockMode Mode() const { return mode_; }
    // Reconciles the instances with the monitors present now. Cheap when nothing changed.
    void Sync();
    // Follow mode: call a few times a second; moves the Dock when the pointer has rested on another monitor.
    void PollFollow();
    // Explorer recreated its taskbar: register the extra appbars again.
    void ReregisterAll();
    // Destroys every extra (before the switch manager exits).
    void Shutdown();
    // Rebuilds the Dock band / thickness of every extra after a Dock option change.
    size_t ExtraMenuCount() const { return menus_.size(); }
    size_t ExtraDockCount() const { return docks_.size(); }

private:
    struct ExtraMenu {
        std::wstring id;
        sw::AppBar bar;
        std::unique_ptr<menubar::MenuBar> menu;
    };
    struct ExtraDock {
        std::wstring id;
        sw::AppBar bar;
        std::unique_ptr<dock::Dock> dock;
    };
    void RecordWithManager(const std::vector<LiveMonitor>& live, const std::vector<std::wstring>& newIds);
    ExtraMenu* FindMenu(const std::wstring& id);
    ExtraDock* FindDock(const std::wstring& id);

    HINSTANCE instance_;
    EventBus& bus_;
    win::WindowTracker& tracker_;
    sw::SwitchManager& manager_;
    menubar::MenuBar::Actions menuActions_;
    dock::Dock::Actions dockActions_;
    DockMode mode_ = DockMode::Primary;
    FollowTracker follow_;
    Plan plan_;
    std::vector<std::unique_ptr<ExtraMenu>> menus_;
    std::vector<std::unique_ptr<ExtraDock>> docks_;
    bool active_ = false;
};

}  // namespace velora::monitors
