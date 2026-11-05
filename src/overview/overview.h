// Overview ("Mission Control"-style, CLAUDE.md sec. 5 overview G1): every open window of the current monitor as a live DWM thumbnail
// in a justified grid over a dimmed, blurred wallpaper; click (or arrows + Enter) brings a window forward, Esc / click outside closes.
// DwmRegisterThumbnail only: nothing is injected into or moved in the other windows. The window and its thumbnails exist only while
// the overview is open. Owner: B.
#pragma once

#include <dwmapi.h>

#include <functional>
#include <string>
#include <vector>

#include <wrl/client.h>

#include "overview/overview_layout.h"
#include "render/render_host.h"
#include "windows/window_tracker.h"

namespace velora::overview {

class Overview {
public:
    Overview(HINSTANCE instance, win::WindowTracker& tracker);
    ~Overview();
    Overview(const Overview&) = delete;
    Overview& operator=(const Overview&) = delete;

    void Toggle() { Visible() ? Hide() : Show(); }
    void Show();
    void Hide();
    bool Visible() const { return hwnd_ != nullptr; }
    void SetOnHidden(std::function<void()> fn) { onHidden_ = std::move(fn); }

    struct Item {
        HWND hwnd = nullptr;
        std::wstring title;
        Size src;               // DIPs
        Cell cell;              // DIPs in the overview window
        HTHUMBNAIL thumb = nullptr;
        bool placeholder = false;  // the thumbnail could not be registered: a tile with the title instead
    };
    const std::vector<Item>& Items() const { return items_; }
    // Brings window `index` forward and closes the overview.
    bool Activate(int index);
    HWND Hwnd() const { return hwnd_; }
    // Test hook: use these windows instead of the tracker's (empty = the tracker's).
    void SetCandidateOverride(std::vector<HWND> windows) { override_ = std::move(windows); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void Collect(HMONITOR monitor);
    void Relayout();
    void UpdateThumbnails();
    void ReleaseThumbnails();
    void BuildBackdrop(ID2D1DeviceContext* dc);
    void Draw();
    float Scale() const;

    HINSTANCE instance_;
    win::WindowTracker& tracker_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    std::vector<Item> items_;
    std::vector<HWND> override_;
    int hover_ = -1, selected_ = -1;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;
    bool wallTried_ = false;
    std::function<void()> onHidden_;
};

}  // namespace velora::overview
