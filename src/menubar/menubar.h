// Global menu bar drawn into the top appbar (CLAUDE.md sec. 5 menubar G1). Owner: B.
//
// G1: Velora menu (drawn by menubar/menu_popup: About, Settings, Apps, Sleep / Restart / Shut Down with confirm, Exit Velora),
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
#include <memory>
#include <string>
#include <vector>

#include "core/event_bus.h"
#include "menubar/app_menu_provider.h"
#include "menubar/menu_popup.h"
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
        std::function<void()> openSearch;
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
    void Trim() { host_.Trim(); }

    // Tests: treat `hwnd` as the active app (the real bar follows the foreground window), and look at what the bar shows.
    void SetMenuTargetOverride(HWND hwnd) {
        targetOverride_ = hwnd;
        RefreshAppMenus();
    }
    std::wstring AppMenuSummary() const { return TitlesToString(appBar_); }   // the titles shown after the app name
    MenuTier AppMenuTier() const { return appBar_.tier; }
    bool TitleRectDip(int index, D2D1_RECT_F* out) const {
        if (index < 0 || index >= static_cast<int>(titleHit_.size())) return false;
        *out = titleHit_[index];
        return true;
    }
    bool AppMenuPopupOpen() const { return popup_.IsOpen() && openTitle_ >= 0; }
    int OpenTitleIndex() const { return popup_.IsOpen() ? openTitle_ : -1; }

private:
    bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
    void Layout();
    void Draw();
    void Invalidate();  // coalesced redraw
    void ReadTheme();
    void RefreshStatus();
    void ArmClock();
    void ShowVeloraMenu();
    void RunCommand(unsigned cmd);
    std::wstring ActiveAppName();
    void OnWindowEvent();

    // ---- global app menus (CLAUDE.md sec. 5 menubar G2, tiers 1-2) ----
    HWND AppMenuTarget() const;       // the window whose menus the bar shows (null: none / not controllable)
    void RefreshAppMenus();           // foreground changed: forget the old titles, ask for the new ones
    void OnMenuAnswers();             // the provider's worker has something
    void LayoutTitles();
    int TitleAt(int px, int py) const;
    void OpenAppMenu(int title);
    void ShowLevel();                 // (re)opens the popup for the top level of stack_
    void RunAppItem(unsigned id);
    struct Level {
        std::wstring label;
        std::vector<AppMenuItem> items;
        int uiaIndex = -1;            // the index of the submenu item that opened this level (UIA path)
    };
    std::unique_ptr<AppMenuProvider> menus_;
    HWND menuTarget_ = nullptr;
    AppMenuBar appBar_;
    unsigned probeId_ = 0, expandId_ = 0;
    std::vector<D2D1_RECT_F> titleHit_, titleCapsule_;
    int hoverTitle_ = -1, openTitle_ = -1;
    std::vector<Level> stack_;        // title level first, drilled-in submenus after it
    std::vector<int> pendingPath_;    // UIA path of the submenu being fetched
    std::wstring pendingLabel_;
    bool reopening_ = false;
    int popupTitle_ = -1;  // the title whose popup is open (Update() is only valid for the same title)
    HWND targetOverride_ = nullptr;

    EventBus& bus_;
    win::WindowTracker& tracker_;
    Actions actions_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    bool backdrop_ = false;  // translucent material (blurred wallpaper + veil); false = opaque bar
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;  // blurred wallpaper behind the bar, window-sized
    bool wallDirty_ = true;
    void RebuildBackdrop(ID2D1DeviceContext* dc);
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
    MenuPopup popup_;
    ULONGLONG popupClosedAt_ = 0;  // a click on the logo right after the popup closed itself must not reopen it
};

}  // namespace velora::menubar
