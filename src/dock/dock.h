// The Dock (CLAUDE.md sec. 5 dock G1): a pill at the bottom of the screen, drawn inside the bottom appbar's
// reserved band. Pinned apps, running apps (from windows/), indicator dots, launch / activate / minimize,
// right-click menu. One Dock instance serves one appbar, so a per-monitor Dock is one more instance
// (only the primary monitor is instantiated today). Magnification and thumbnails are G2/G3. Owner: B.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/event_bus.h"
#include "dock/dock_model.h"
#include "render/animation.h"
#include "render/render_host.h"
#include "switch/appbar.h"
#include "windows/window_tracker.h"

namespace velora::dock {

// Pure geometry (unit-tested), DIPs.
struct DockGeometry {
    float pillWidth = 0, pillHeight = 0;
    std::vector<float> itemX;  // left edge of each icon
    float separatorX = -1;     // between pinned and unpinned running apps (-1: none)
};
DockGeometry ComputeGeometry(size_t itemCount, size_t pinnedCount);

std::vector<DockApp> DefaultPinned();  // Apps, Files, Settings, File Explorer, Edge when present
std::vector<DockApp> LoadPinned();     // settings.json "dock.pinned", defaults when missing
bool SavePinned(const std::vector<DockApp>& pinned);

class Dock {
public:
    struct Actions {
        std::function<void()> openApps;
    };

    Dock(EventBus& bus, win::WindowTracker& tracker, Actions actions);
    ~Dock();
    Dock(const Dock&) = delete;
    Dock& operator=(const Dock&) = delete;

    sw::AppBarOptions Options();
    float ReservedDip() const;  // appbar thickness: pill + bottom inset
    void Attach(sw::AppBar* bar);
    void Detach();
    void SetAnimationsEnabled(bool on) { anim_.SetEnabled(on); }

    const std::vector<DockItem>& Items() const { return items_; }

private:
    bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
    RECT Placement(const RECT& reserved, UINT dpi) const;
    void Rebuild();
    void ScheduleRebuild();
    void ApplyPlacement();
    void ApplyRegion();
    void Draw();
    void Tick();
    int HitItem(float x, float y) const;
    void OnClick(int index);
    void ShowItemMenu(int index);
    void Pin(const DockApp& app, bool pin);
    DockWindow Describe(HWND h, const win::TrackedWindow& w);
    DockApp DescribeUnpinned(const DockWindow& w);
    ID2D1Bitmap* BitmapFor(ID2D1DeviceContext* dc, const std::wstring& icon);

    EventBus& bus_;
    win::WindowTracker& tracker_;
    Actions actions_;
    sw::AppBar* bar_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    render::AnimationEngine anim_;
    bool frameTimer_ = false, rebuildPending_ = false, backdrop_ = false;
    size_t hiddenLogged_ = static_cast<size_t>(-1);  // last visible-pinned count that was logged
    float appear_ = 1.0f;  // 0..1 entrance animation
    std::vector<EventBus::Token> tokens_;

    std::vector<DockApp> pinned_;
    std::vector<DockItem> items_;
    DockGeometry geo_;
    std::vector<float> hover_;  // per item 0..1
    int hoverIndex_ = -1, pressIndex_ = -1;
    std::map<std::uint64_t, std::wstring> aumidCache_;
    std::map<DWORD, std::wstring> exeCache_;
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    ID2D1Bitmap* VeloraBitmap(ID2D1DeviceContext* dc, const D2D1_RECT_F& dipRect);  // the Apps item's icon
    int iconListener_ = 0;
};

}  // namespace velora::dock
