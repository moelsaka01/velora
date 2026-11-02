// Drawn popup menu for the menu bar (replaces the native TrackPopupMenu so the rows, radius and font follow the spec).
// One window while open, nothing when closed. Owner: B.
//
// Rows are 24 pt, the popup is 201.5 pt wide, the highlight is an accent rounded rectangle (menubar/menu_layout.h).
// Corners are rounded by DWM (8 px at 100 %, the system "round" preference): a custom radius cannot be combined with a
// DWM Acrylic backdrop, so the measured 11 pt radius is not reproduced.
#pragma once

#include <functional>
#include <vector>

#include "menubar/menu_layout.h"
#include <wrl/client.h>

#include "render/render_host.h"

namespace velora::menubar {

class MenuPopup {
public:
    explicit MenuPopup(HINSTANCE instance);
    ~MenuPopup();
    MenuPopup(const MenuPopup&) = delete;
    MenuPopup& operator=(const MenuPopup&) = delete;

    // Shows the popup with its top-left corner at the screen pixel (x, y), moved inside the monitor if needed. `onChoose`
    // runs after the popup has closed, with the id of the chosen item; `onClosed` runs on every close (chosen or not).
    bool Open(HWND owner, int x, int y, std::vector<MenuItem> items, std::function<void(unsigned)> onChoose,
              std::function<void()> onClosed);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void Draw();
    void Choose(int index);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;  // blurred wallpaper behind the popup (null until drawn / no image)
    bool translucent_ = false;
    std::vector<MenuItem> items_;
    MenuLayout layout_;
    int selected_ = -1;
    int pressed_ = -1;
    std::function<void(unsigned)> onChoose_;
    std::function<void()> onClosed_;
};

}  // namespace velora::menubar
