// The Dock (CLAUDE.md sec. 5 dock G1 + hover magnification): a glass shelf at the bottom of the screen, drawn
// inside the bottom appbar's reserved band. Pinned apps, running apps (from windows/), indicator dots, launch /
// activate / minimize, right-click menu. One Dock instance serves one appbar, so a per-monitor Dock is one more
// instance (only the primary monitor is instantiated today). Thumbnails are G3. Owner: B.
//
// The window is taller and wider than the shelf: it carries the headroom magnified icons grow into and the margin
// the shelf stretches into. While nothing is magnified its window region is only the shelf (plus the shadow), so
// the headroom never blocks other windows; while the pointer is over the Dock the region is the whole window.
//
// Material: the wallpaper behind the Dock is read through IDesktopWallpaper, cropped, blurred on the CPU and drawn
// as the shelf's backdrop under a neutral veil (the system backdrop cannot be shaped to a shelf inside a taller
// window). With transparency effects off, or gaming mode, the shelf is opaque.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <set>
#include <vector>

#include "core/event_bus.h"
#include "dock/dock_layout.h"
#include "dock/dock_model.h"
#include "render/animation.h"
#include "render/render_host.h"
#include "switch/appbar.h"
#include "windows/window_tracker.h"

namespace velora::dock {

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
    // Icon sources the Dock itself draws (IconStore::Trim keeps these).
    std::set<std::wstring> IconSources() const;
    void Trim() { host_.Trim(); }

private:
    bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
    RECT Placement(const RECT& reserved, UINT dpi) const;
    void Rebuild();
    void ScheduleRebuild();
    void ApplyPlacement();
    void UpdateRegion(bool force);
    void UpdateMagnification(bool active, float pointerX);
    void SetScaleTargets(const std::vector<float>& targets);
    void RebuildBackdrop(ID2D1DeviceContext* dc);
    void Draw();
    void Tick();
    int HitItem(float x, float y) const;
    float WindowWidthDip() const { return baseGeo_.pillWidth + 2 * sideMargin_; }
    float WindowHeightDip() const { return headroom_ + tokens::dock::kShelfHeight + tokens::size::kDockBottomInset; }
    float ShelfLeft() const { return (WindowWidthDip() - geo_.pillWidth) / 2; }  // shelf origin in window DIPs
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
    bool frameTimer_ = false, rebuildPending_ = false;
    size_t hiddenLogged_ = static_cast<size_t>(-1);  // last visible-pinned count that was logged
    float appear_ = 1.0f;  // 0..1 entrance animation
    std::vector<EventBus::Token> tokens_;

    std::vector<DockApp> pinned_;
    std::vector<DockItem> items_;
    DockGeometry geo_;      // as drawn (icons at their current scale)
    DockGeometry baseGeo_;  // at rest: sizes the window
    size_t pinnedCount_ = 0;  // visible pinned apps (the first pinnedCount_ items)
    bool tracking_ = false;   // a WM_MOUSELEAVE is armed
    float sideMargin_ = 0, headroom_ = 0;  // DIPs of window around the resting shelf
    std::vector<float> scale_, scaleTarget_;  // per item: current (spring) and target magnification
    std::vector<render::AnimationEngine::Id> scaleAnim_;
    bool hoverActive_ = false, regionFull_ = false, regionSet_ = false;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;  // blurred wallpaper behind the window (window-sized); null: none
    bool wallDirty_ = true;
    LONG wallMonitorLeft_ = 0;  // screen x of the bitmap's left edge (the monitor's left)
    LONG wallHeightPx_ = 0;     // window height the bitmap was cut for
    int hoverIndex_ = -1, pressIndex_ = -1;
    std::map<std::uint64_t, std::wstring> aumidCache_;
    std::map<DWORD, std::wstring> exeCache_;
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    ID2D1Bitmap* VeloraBitmap(ID2D1DeviceContext* dc, int tierPx);  // the Apps item's icon, decoded at the tier
    int iconListener_ = 0;
};

}  // namespace velora::dock
