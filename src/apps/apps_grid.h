// App grid ("Apps", CLAUDE.md sec. 5 apps G1): a glass panel (blurred wallpaper under a dark veil) with a search field
// and an icon grid of every installed app, type-to-search, keyboard and mouse launch. The panel opens and closes with a
// spring. The window (transparent, one monitor, so a click outside dismisses) and its GPU resources exist only while it is
// open. Owner: B.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "apps/app_index.h"
#include "render/animation.h"
#include "render/render_host.h"

namespace velora::apps {

// Pure layout math (unit-tested). All values in DIPs.
struct GridLayout {
    int columns = 1;
    float tileW = 112, tileH = 120, left = 0;  // left edge of the first column
};
GridLayout ComputeGridLayout(float viewWidth);
// The panel inside a monitor-sized view: 843 pt wide (narrower on a small screen), centred, with room left for the Dock.
struct PanelGeometry {
    float left = 0, top = 0, width = 0, height = 0;
};
PanelGeometry ComputePanel(float viewWidth, float viewHeight);
// Scroll offset that keeps row `row` fully inside a viewport of `viewHeight`.
float ScrollToRow(float scroll, int row, float rowHeight, float viewHeight);

class AppsGrid {
public:
    AppsGrid(HINSTANCE instance);
    ~AppsGrid();
    AppsGrid(const AppsGrid&) = delete;
    AppsGrid& operator=(const AppsGrid&) = delete;

    void Toggle();
    void Show();
    void Hide();            // immediately
    void Close();           // with the closing spring (immediately when animations are off)    bool Visible() const { return hwnd_ != nullptr; }
    // The index changed or icons arrived.
    void OnDataChanged();
    void SetAnimationsEnabled(bool on) { anim_.SetEnabled(on); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void Refilter();
    void Draw();
    void Tick();
    void BuildBackdrop(ID2D1DeviceContext* dc);
    void StartOpen();
    void OnSpring(float v, bool done);
    void LaunchSelected();
    int HitTile(float x, float y) const;
    void EnsureSelectedVisible();
    PanelGeometry Panel() const;
    float ViewTop() const;     // top of the scrolling grid (window DIPs)
    float ViewBottom() const;
    float MaxScroll() const;

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    render::AnimationEngine anim_;
    float open_ = 0;  // 0..1 open animation (a spring: critically damped, never above 1)
    render::AnimationEngine::Id spring_ = 0;
    bool closing_ = false, closeDone_ = false, wallTried_ = false;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;  // blurred wallpaper behind the panel (1 / kBackdropShrink size)
    bool timerOn_ = false;

    std::vector<AppEntry> apps_;
    std::vector<size_t> shown_;  // indices into apps_
    std::wstring query_;
    int selected_ = 0, hover_ = -1;
    float scroll_ = 0;
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    int listener_ = 0;
};

}  // namespace velora::apps
