// The Velora Settings window (CLAUDE.md sec. 5 settings G1): a page list on the left (shared Velora window chrome) and the rows of
// the selected page on the right - switches, pick lists, buttons and one editable list. Direct2D, no controls library. Every change
// is written to settings.json at once and the running shell is told (IPC settings_changed). One window per process. Owner: C.
#pragma once

#include <d2d1_3.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

#include "core/win.h"
#include "menubar/menu_popup.h"
#include "render/render_host.h"
#include "settings/settings_model.h"
#include "ui/text_edit.h"

namespace velora::settings {

class SettingsWindow {
public:
    explicit SettingsWindow(HINSTANCE instance);
    ~SettingsWindow();
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // `file` is the settings.json to edit; `page` the page to show first.
    bool Create(const std::filesystem::path& file, int page, int showCmd);
    HWND Hwnd() const { return hwnd_; }
    void ShowPage(int page);
    int CurrentPage() const { return page_; }

    // What the buttons and the shell notification do; the defaults act on the real system. Tests replace them.
    void SetActionHook(std::function<void(const std::string&)> fn) { actionHook_ = std::move(fn); }
    void SetNotifyHook(std::function<void()> fn) { notifyHook_ = std::move(fn); }
    void SetStartupHook(std::function<void(bool)> fn) { startupHook_ = std::move(fn); }

    // Actions behind the mouse and keyboard (also what the automated tests drive).
    enum class HitKind { None, Toggle, ChoiceButton, ActionButton, ListRemove, ListField, ListAdd, Page, Traffic };
    struct Hit {
        HitKind kind = HitKind::None;
        int item = -1;  // index into the page's items
        int sub = -1;   // list entry / traffic light / page
        D2D1_RECT_F rect{};
    };
    std::vector<Hit> Hits();  // of the current page, window DIPs (after scrolling)
    void Activate(const Hit& hit);
    const Store& GetStore() const { return store_; }
    const std::wstring& ListFieldText() const { return field_.Text(); }
    void TypeInField(const std::wstring& text);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    float Scale() const;
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void Draw();
    void Layout();
    void Changed();
    void FlipToggle(const Item& item);
    void OpenChoice(const Item& item, const D2D1_RECT_F& rect);
    void RunAction(const std::string& action);
    void AddListEntry(const Item& item);
    void MoveFocus(int dir);
    int HitIndexAt(float x, float y) const;
    float ContentHeight() const { return contentH_; }

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    Store store_{{}};
    std::vector<Page> pages_;
    int page_ = 0;
    velora::tokens::Theme theme_ = velora::tokens::Theme::Light;
    bool active_ = true;
    float scroll_ = 0, contentH_ = 0;
    std::vector<Hit> hits_;  // rebuilt by Layout()
    int hover_ = -1;         // index into hits_
    int focus_ = -1;         // index into hits_ (interactive ones only)
    bool fieldFocus_ = false;
    ui::TextEdit field_;
    menubar::MenuPopup popup_;
    std::function<void(const std::string&)> actionHook_;
    std::function<void()> notifyHook_;
    std::function<void(bool)> startupHook_;
    bool saveFailedShown_ = false;
    std::wstring status_;  // one-line result under the page title ("Could not save settings.json")
};

}  // namespace velora::settings
