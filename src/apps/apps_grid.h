// Full-screen app grid ("Apps", CLAUDE.md sec. 5 apps G1): icon grid of every installed app, type-to-search,
// keyboard and mouse launch. The window and its GPU resources exist only while it is open. Owner: B.
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
    void Hide();
    bool Visible() const { return hwnd_ != nullptr; }
    // The index changed or icons arrived.
    void OnDataChanged();
    void SetAnimationsEnabled(bool on) { anim_.SetEnabled(on); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void Refilter();
    void Draw();
    void Tick();
    void LaunchSelected();
    int HitTile(float x, float y) const;
    void EnsureSelectedVisible();
    float ViewTop() const { return 140.0f; }
    float MaxScroll() const;

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    render::AnimationEngine anim_;
    float open_ = 0;  // 0..1 open animation
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
