// Global menu bar drawn into the top appbar (CLAUDE.md sec. 5 menubar G1). Owner: B.
//
// G1: Velora menu (About, Settings, Apps, Sleep / Restart / Shut Down with confirm, Exit Velora),
// active app name, status items (battery) and the clock. Light/dark follows the system
// theme. App menus (G2) and per-monitor bars (G3) are not built.
//
// Tray icons: NOT hosted. On Windows 11 Explorer owns the notification area (XAML) and
// Velora never replaces Explorer or injects into it (CLAUDE.md sec. 0.8), so there is no
// supported way to receive Shell_NotifyIcon traffic. See docs/PROGRESS.md (menubar).
// The "⋯" item opens ms-settings:taskbar (no keystrokes are injected).
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/event_bus.h"
#include "render/render_host.h"
#include "switch/appbar.h"
#include "windows/window_tracker.h"

namespace velora::menubar {

struct StatusItem {
    std::wstring id;    // "battery"
    std::wstring text;  // empty => hidden
};

// Pure helpers (unit-tested).
std::wstring BatteryText(unsigned char percent, unsigned char acLineStatus, unsigned char batteryFlag);
// Display name for the foreground app: FileDescription, else the exe stem, else the window title.
std::wstring AppDisplayName(const std::wstring& exePath, const std::wstring& fileDescription, const std::wstring& windowTitle);

class MenuBar {
public:
    struct Actions {
        std::function<void()> exitVelora;
        std::function<void()> openSettings;
        std::function<void()> openApps;
    };

    MenuBar(EventBus& bus, win::WindowTracker& tracker, Actions actions);
    ~MenuBar();
    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    // Hooks for SwitchManager::SetMenuBarOptions; call before Enter().
    sw::AppBarOptions Options();
    // The appbar window exists now (Entered): create the render target and draw.
    void Attach(HWND hwnd);
    // Before the appbar window is destroyed (Exiting).
    void Detach();

    void SetEffects(bool enabled);  // gaming mode: no backdrop blur
    bool Attached() const { return hwnd_ != nullptr; }

private:
    bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
    void Layout();
    void Draw();
    void Invalidate();  // coalesced redraw
    void ReadTheme();
    void RefreshStatus();
    void ArmClock();
    void ShowVeloraMenu();
    std::wstring ActiveAppName();
    void OnWindowEvent();

    EventBus& bus_;
    win::WindowTracker& tracker_;
    Actions actions_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    bool backdrop_ = false;
    bool effects_ = true;
    tokens::Theme theme_ = tokens::Theme::Light;
    bool dirty_ = false;
    std::vector<EventBus::Token> tokens_;

    std::wstring appName_, clockText_;
    std::vector<StatusItem> status_;
    std::map<DWORD, std::wstring> nameCache_;  // pid -> display name
    D2D1_RECT_F logoRect_{}, appRect_{};
    std::vector<std::pair<std::wstring, D2D1_RECT_F>> rightItems_;
    bool hoverLogo_ = false;
    bool menuOpen_ = false;
};

}  // namespace velora::menubar
