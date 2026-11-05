#include "menubar/menubar.h"

#include <powrprof.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <thread>

#include <fstream>
#include <sstream>

#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/version.h"
#include "ui/backdrop.h"
#include "ui/ui_common.h"

#pragma comment(lib, "powrprof.lib")

namespace velora::menubar {

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT_PTR kTimerClock = 1;
constexpr UINT_PTR kTimerRedraw = 2;
constexpr UINT_PTR kTimerWallpaper = 3;  // one shot after attach: Velora applies its wallpaper right after entering
constexpr float kPad = 14.0f;
namespace tm = velora::tokens::menu;

constexpr UINT kMsgMenus = WM_APP + 70;        // the app-menu worker has answers
constexpr unsigned kBackItem = 0xFFFF;          // popup id of the "back" row in a drilled-in submenu
enum Cmd : UINT { kAbout = 1, kSettings, kApps, kSearch, kOverview, kControl, kSleep, kRestart, kShutDown, kExit };

bool EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

bool Confirm(HWND owner, const wchar_t* text) {
    return MessageBoxW(owner, text, L"Velora", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND) == IDYES;
}

// Documented APIs only: SetSuspendState (sleep) and ExitWindowsEx without EWX_FORCE, so
// applications can still ask to save their work.
void DoSleep() {
    log::Info("power: sleep requested from the Velora menu");
    std::thread([] { SetSuspendState(FALSE, FALSE, FALSE); }).detach();  // blocks until resume
}

void DoExitWindows(UINT flags, const char* what) {
    log::Info(std::string("power: ") + what + " requested from the Velora menu");
    if (!EnableShutdownPrivilege()) log::Warn("power: could not enable SeShutdownPrivilege");
    if (!ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED))
        log::Error(std::string("power: ExitWindowsEx failed, error ") + std::to_string(GetLastError()));
}

// The ⋯ item opens Windows' own page for the notification area ("Other system tray icons"). No keystrokes
// are injected: SendInput looked like malware to antivirus heuristics, and an injected Win/Enter sequence was
// a suspect for the PrintScreen breakage seen in the first live run (see docs/DECISIONS.md).
void OpenTaskbarSettings() {
    auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"ms-settings:taskbar", nullptr, nullptr, SW_SHOWNORMAL));
    if (r > 32)
        log::Info("menubar: overflow result=ms-settings (opened ms-settings:taskbar)");
    else
        log::Error("menubar: overflow result=failed (ms-settings:taskbar did not open, code " + std::to_string(r) + ")");
}

// settings.json {"menus": {"probeChromium": true}}: allow probing Chromium / Electron windows (it switches their accessibility on).
bool ProbeChromiumSetting() {
    const auto dir = DataDir();
    if (dir.empty()) return false;
    std::ifstream f(dir / L"settings.json", std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    auto root = json::Parse(ss.str());
    const json::Value* menus = root && root->IsObject() ? root->Find("menus") : nullptr;
    const json::Value* flag = menus && menus->IsObject() ? menus->Find("probeChromium") : nullptr;
    return flag && flag->AsBool(false);
}

bool IsShellClass(HWND h) {
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, static_cast<int>(std::size(cls)));
    std::wstring c = ui::Lower(cls);
    return c == L"progman" || c == L"workerw" || c == L"shell_traywnd" || c == L"shell_secondarytraywnd";
}

}  // namespace

std::wstring BatteryText(unsigned char percent, unsigned char acLineStatus, unsigned char batteryFlag) {
    if (percent > 100 || (batteryFlag & 128)) return {};  // unknown percent or no battery
    std::wstring t = std::to_wstring(percent) + L"%";
    if ((batteryFlag & 8) || acLineStatus == 1) t += L" \u26A1";
    return t;
}

std::wstring AppDisplayName(const std::wstring& exePath, const std::wstring& fileDescription, const std::wstring& windowTitle) {
    std::wstring stem = ui::Lower(ui::StemOf(exePath));
    if (stem == L"applicationframehost" && !windowTitle.empty()) return windowTitle;  // UWP: the host process is not the app
    if (!fileDescription.empty()) return fileDescription;
    if (!stem.empty()) return ui::StemOf(exePath);
    return windowTitle.empty() ? L"Desktop" : windowTitle;
}

MenuBar::MenuBar(EventBus& bus, win::WindowTracker& tracker, Actions actions)
    : bus_(bus), tracker_(tracker), actions_(std::move(actions)), popup_(GetModuleHandleW(nullptr)) {
    for (EventType t : {EventType::WindowActivated, EventType::WindowTitleChanged, EventType::WindowDestroyed})
        tokens_.push_back(bus_.Subscribe(t, [this](const Event&) { OnWindowEvent(); }));
    tokens_.push_back(bus_.Subscribe(EventType::WindowActivated, [this](const Event&) { RefreshAppMenus(); }));
    tokens_.push_back(bus_.Subscribe(EventType::WindowDestroyed, [this](const Event& e) {
        HWND gone = reinterpret_cast<HWND>(static_cast<UINT_PTR>(e.window.hwnd));
        if (menus_) menus_->Forget(gone);
        if (gone == menuTarget_) RefreshAppMenus();
    }));
}

MenuBar::~MenuBar() {
    for (auto t : tokens_) bus_.Unsubscribe(t);
    Detach();
}

sw::AppBarOptions MenuBar::Options() {
    sw::AppBarOptions o;
    o.composition = true;
    o.hook = [this](HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& r) { return OnMessage(h, m, w, l, r); };
    return o;
}

void MenuBar::Attach(HWND hwnd) {
    if (hwnd_ || !hwnd) return;
    hwnd_ = hwnd;
    dpi_ = GetDpiForWindow(hwnd);
    if (!host_.Initialize(hwnd)) {
        log::Warn("menubar: render host unavailable; the bar stays blank");
        hwnd_ = nullptr;
        return;
    }
    ReadTheme();
    RefreshStatus();
    ArmClock();
    Layout();
    SetTimer(hwnd_, kTimerWallpaper, 1500, nullptr);
    menus_ = std::make_unique<AppMenuProvider>(hwnd_, kMsgMenus);
    menus_->SetProbeChromium(ProbeChromiumSetting());
    RefreshAppMenus();
    Draw();
}

void MenuBar::Detach() {
    if (!hwnd_) return;
    popup_.Close();
    menus_.reset();
    menuTarget_ = nullptr;
    appBar_ = {};
    stack_.clear();
    titleHit_.clear();
    titleCapsule_.clear();
    hoverTitle_ = openTitle_ = -1;
    KillTimer(hwnd_, kTimerClock);
    KillTimer(hwnd_, kTimerRedraw);
    KillTimer(hwnd_, kTimerWallpaper);
    wall_.Reset();
    dirty_ = false;  // the popup's close callback may have asked for a redraw whose timer is now gone
    host_.Shutdown();
    hwnd_ = nullptr;
}

void MenuBar::SetEffects(bool enabled) {
    effects_ = enabled;
    if (hwnd_) {
        ReadTheme();
        Invalidate();
    }
}

void MenuBar::ReadTheme() {
    theme_ = ui::SystemTheme();
    // The material is drawn (blurred wallpaper + veil, like the Dock): the DWM backdrop renders as flat grey on a
    // bar this thin, so it is switched off.
    backdrop_ = effects_ && ui::TransparencyEnabled();
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, theme_);
    render::ApplyRoundedCorners(hwnd_, false);
    wallDirty_ = true;
}

// The slice of wallpaper behind the bar, blurred; null (a stronger veil is used) for a colour/slideshow wallpaper.
void MenuBar::RebuildBackdrop(ID2D1DeviceContext* dc) {
    wall_.Reset();
    wallDirty_ = false;
    RECT win{};
    MONITORINFO mi{sizeof mi};
    if (!hwnd_ || !dc || !GetWindowRect(hwnd_, &win) ||
        !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    ui::Pixels px;
    if (!ui::BlurredWallpaper(mi.rcMonitor, win, 1, tokens::dock::kBlurSigma, dpi_, &px)) {
        log::Info("menubar: no wallpaper image behind the bar; stronger veil instead of a blurred backdrop");
        return;
    }
    wall_ = ui::MakeBitmap(dc, px);
}

void MenuBar::RefreshStatus() {
    SYSTEM_POWER_STATUS sps{};
    StatusItem battery{L"battery", {}};
    if (GetSystemPowerStatus(&sps)) battery.text = BatteryText(sps.BatteryLifePercent, sps.ACLineStatus, sps.BatteryFlag);
    status_ = {battery};

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t date[64] = {}, time[32] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd d MMM", date, static_cast<int>(std::size(date)), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, static_cast<int>(std::size(time)));
    clockText_ = std::wstring(date) + L"  " + time;
}

void MenuBar::ArmClock() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    // Wake once per minute, just after the minute rolls over: idle CPU stays near zero.
    SetTimer(hwnd_, kTimerClock, 60000 - st.wSecond * 1000 - st.wMilliseconds + 50, nullptr);
}

std::wstring MenuBar::ActiveAppName() {
    HWND fg = GetForegroundWindow();
    if (!fg) return appName_.empty() ? L"Desktop" : appName_;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return appName_.empty() ? L"Desktop" : appName_;  // our own popup: keep the last app
    if (IsShellClass(fg)) return L"Desktop";
    std::wstring path = ui::ProcessImagePath(pid);
    wchar_t title[256] = {};
    GetWindowTextW(fg, title, static_cast<int>(std::size(title)));
    bool host = ui::Lower(ui::StemOf(path)) == L"applicationframehost";
    if (!host) {
        if (auto it = nameCache_.find(pid); it != nameCache_.end()) return it->second;
    }
    std::wstring name = AppDisplayName(path, host ? L"" : ui::FileDescription(path), title);
    if (!host) {
        if (nameCache_.size() > 256) nameCache_.clear();
        nameCache_[pid] = name;
    }
    return name;
}

void MenuBar::OnWindowEvent() {
    if (hwnd_) Invalidate();
}

void MenuBar::Invalidate() {
    if (!hwnd_ || dirty_) return;
    dirty_ = true;
    SetTimer(hwnd_, kTimerRedraw, 30, nullptr);  // coalesce bursts (titles that change per keystroke)
}

void MenuBar::Layout() {
    const float h = tokens::size::kMenuBarHeight;
    const float capTop = (h - tm::kCapsuleHeight) / 2;
    // The logo is the only title with a menu today; its capsule is as wide as a 16 pt disc plus 9 pt each side.
    logoRect_ = D2D1::RectF(10, capTop, 10 + 34, capTop + tm::kCapsuleHeight);
    std::wstring name = appName_.empty() ? L"Desktop" : appName_;
    float w = ui::MeasureWidth(name, ui::Format(tm::kFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD)) + 4;
    // Title text to title text is 21.75 pt: measured from the disc's right edge (the capsule has 9 pt of padding).
    const float nameLeft = logoRect_.right - 9 + tm::kTitleGap;
    appRect_ = D2D1::RectF(nameLeft, 0, nameLeft + std::min(w, 360.0f), h);

    // Right side, from the edge inwards: clock, then status items, 21.75 pt apart.
    rightItems_.clear();
    float width = static_cast<float>(host_.Width()) / ui::Scale(dpi_);
    float x = width - kPad;
    IDWriteTextFormat* fmt = ui::Format(tm::kFontSize);
    auto place = [&](const std::wstring& id, const std::wstring& text) {
        if (text.empty()) return;
        float tw = ui::MeasureWidth(text, fmt) + 2;
        rightItems_.emplace_back(id + L"\n" + text, D2D1::RectF(x - tw, 0, x, h));
        x -= tw + tm::kTitleGap;
    };
    place(L"clock", clockText_);
    if (!secondary_) {  // a secondary monitor's bar shows the app menus and the clock; the system items live on the primary bar
        place(L"control", L"\u2630");  // Control Center
        place(L"overflow", L"\u22EF");
        for (const auto& s : status_) place(s.id, s.text);
    }
    LayoutTitles();
}

// The active app's menu titles after its name: text to text 21.75 pt apart, each with a 23 pt capsule when hovered or open.
void MenuBar::LayoutTitles() {
    titleHit_.clear();
    titleCapsule_.clear();
    if (appBar_.titles.empty() || appBar_.hwnd != menuTarget_ || !menuTarget_) return;
    const float h = tokens::size::kMenuBarHeight, capTop = (h - tm::kCapsuleHeight) / 2;
    float limit = static_cast<float>(host_.Width()) / ui::Scale(dpi_);
    for (const auto& item : rightItems_) limit = std::min(limit, item.second.left);
    limit -= tm::kTitleGap;
    IDWriteTextFormat* fmt = ui::Format(tm::kFontSize);
    float x = appRect_.right - 4 + tm::kTitleGap;  // the name's text ends 4 pt before its rect does
    for (const AppMenuTitle& t : appBar_.titles) {
        const float w = ui::MeasureWidth(t.label, fmt);
        if (x + w > limit) break;
        titleHit_.push_back(D2D1::RectF(x - tm::kTitleGap / 2, 0, x + w + tm::kTitleGap / 2, h));
        titleCapsule_.push_back(D2D1::RectF(x - tm::kCapsulePadX, capTop, x + w + tm::kCapsulePadX, capTop + tm::kCapsuleHeight));
        x += w + tm::kTitleGap;
    }
}

int MenuBar::TitleAt(int px, int py) const {
    const float s = ui::Scale(dpi_), x = px / s, y = py / s;
    for (size_t i = 0; i < titleHit_.size(); ++i)
        if (x >= titleHit_[i].left && x < titleHit_[i].right && y >= titleHit_[i].top && y < titleHit_[i].bottom) return static_cast<int>(i);
    return -1;
}

void MenuBar::Draw() {
    if (!host_.Ready()) return;
    dirty_ = false;
    std::wstring name = ActiveAppName();
    if (name != appName_) {
        appName_ = name;
        Layout();
    }
    const tokens::Palette& p = tokens::PaletteFor(theme_);
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    float w = static_cast<float>(host_.Width()) / ui::Scale(dpi_);
    float h = tokens::size::kMenuBarHeight;

    // The spec's bar has no chrome at all (no veil, no edge). Over an arbitrary wallpaper that is not readable, so the bar
    // is the Dock's material: the blurred wallpaper with a neutral glass veil, and no bottom hairline.
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(ui::Rgba(p.surfaceStrong), &brush);
    if (backdrop_) {
        if (wallDirty_) RebuildBackdrop(dc);
        if (wall_) dc->DrawBitmap(wall_.Get(), D2D1::RectF(0, 0, w, h), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        const tokens::Glass& glass = tokens::GlassFor(theme_);
        const float veil = wall_ ? (theme_ == tokens::Theme::Dark ? tokens::dock::kVeilAlphaDark : tokens::dock::kVeilAlphaLight)
                                 : tokens::dock::kVeilAlphaUnblurred;
        brush->SetColor(ui::Rgba(glass.veil, veil));
    }
    dc->FillRectangle(D2D1::RectF(0, 0, w, h), brush.Get());

    if (hoverLogo_ || menuOpen_) {  // selected-title capsule: fully rounded
        brush->SetColor(ui::Rgba(p.separator, menuOpen_ ? 1.6f : 1.0f));
        dc->FillRoundedRectangle(D2D1::RoundedRect(logoRect_, tm::kCapsuleHeight / 2, tm::kCapsuleHeight / 2), brush.Get());
    }
    // Logo: original mark (accent disc with a "V"), not an OS glyph.
    brush->SetColor(ui::Rgba(p.accent));
    float cx = (logoRect_.left + logoRect_.right) / 2, cy = h / 2;
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8, 8), brush.Get());
    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    ui::DrawLabel(dc, brush.Get(), L"V", D2D1::RectF(cx - 8, 0, cx + 8, h), ui::Format(11, DWRITE_FONT_WEIGHT_BOLD),
                  DWRITE_TEXT_ALIGNMENT_CENTER);

    brush->SetColor(ui::Rgba(p.text));
    ui::DrawLabel(dc, brush.Get(), appName_, appRect_, ui::Format(tm::kFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    for (size_t i = 0; i < titleHit_.size() && i < appBar_.titles.size(); ++i) {
        const bool open = static_cast<int>(i) == openTitle_ && menuOpen_, hot = static_cast<int>(i) == hoverTitle_;
        if (open || hot) {
            brush->SetColor(ui::Rgba(p.separator, open ? 1.6f : 1.0f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(titleCapsule_[i], tm::kCapsuleHeight / 2, tm::kCapsuleHeight / 2), brush.Get());
        }
        brush->SetColor(ui::Rgba(appBar_.titles[i].enabled ? p.text : p.textSecondary));
        const float left = (titleCapsule_[i].left + titleCapsule_[i].right) / 2 - ui::MeasureWidth(appBar_.titles[i].label, ui::Format(tm::kFontSize)) / 2;
        ui::DrawLabel(dc, brush.Get(), appBar_.titles[i].label, D2D1::RectF(left, 0, titleHit_[i].right, h), ui::Format(tm::kFontSize));
    }
    for (const auto& [key, rect] : rightItems_) {
        std::wstring text = key.substr(key.find(L'\n') + 1);
        ui::DrawLabel(dc, brush.Get(), text, rect, ui::Format(tm::kFontSize), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if (!host_.EndDraw()) {
        log::Warn("menubar: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        Invalidate();
    }
}

void MenuBar::ShowVeloraMenu() {
    std::vector<MenuItem> items = {
        {kAbout, L"About Velora"}, Separator(),
        {kSettings, L"Settings\u2026"}, {kApps, L"Apps"}, {kSearch, L"Search"}, {kOverview, L"Overview"}, {kControl, L"Control Center"}, Separator(),
        {kSleep, L"Sleep"}, {kRestart, L"Restart\u2026"}, {kShutDown, L"Shut Down\u2026"}, Separator(),
        {kExit, L"Exit Velora"},
    };
    RECT r{};
    GetWindowRect(hwnd_, &r);
    const float s = ui::Scale(dpi_);
    // The popup sits 4.5 pt under the capsule's bottom edge and lines up with the capsule's left edge.
    const float top = (tokens::size::kMenuBarHeight + tm::kCapsuleHeight) / 2 + tm::kPopupGap;
    menuOpen_ = true;
    Draw();
    bool opened = popup_.Open(
        hwnd_, r.left + static_cast<int>(logoRect_.left * s), r.top + static_cast<int>(top * s), std::move(items),
        [this](unsigned cmd) { RunCommand(cmd); },
        [this] {
            menuOpen_ = false;
            popupClosedAt_ = GetTickCount64();
            Invalidate();
        });
    if (!opened) {
        menuOpen_ = false;
        Invalidate();
    }
}

void MenuBar::RunCommand(unsigned cmd) {
    switch (cmd) {
        case kAbout: {
            std::wstring text = L"Velora " + std::wstring(L"" VELORA_VERSION) +
                                L"\n\nAn open-source (Apache-2.0) desktop environment for Windows 11.\nWindows keeps running underneath.";
            MessageBoxW(hwnd_, text.c_str(), L"About Velora", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
            break;
        }
        case kSettings:
            if (actions_.openSettings) actions_.openSettings();
            break;
        case kApps:
            if (actions_.openApps) actions_.openApps();
            break;
        case kSearch:
            if (actions_.openSearch) actions_.openSearch();
            break;
        case kOverview:
            if (actions_.openOverview) actions_.openOverview();
            break;
        case kControl:
            if (actions_.openControl) actions_.openControl();
            break;
        case kSleep:
            if (Confirm(hwnd_, L"Put this computer to sleep?")) DoSleep();
            break;
        case kRestart:
            if (Confirm(hwnd_, L"Restart this computer now?\n\nApps may ask you to save your work.")) DoExitWindows(EWX_REBOOT, "restart");
            break;
        case kShutDown:
            if (Confirm(hwnd_, L"Shut down this computer now?\n\nApps may ask you to save your work.")) DoExitWindows(EWX_SHUTDOWN, "shut down");
            break;
        case kExit:
            if (actions_.exitVelora) actions_.exitVelora();
            break;
        default:
            break;
    }
}

bool MenuBar::OnMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    if (!hwnd_ || h != hwnd_) return false;
    auto inRect = [&](const D2D1_RECT_F& r, int px, int py) {
        float s = ui::Scale(dpi_);
        float x = px / s, y = py / s;
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    };
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            Draw();
            EndPaint(h, &ps);
            result = 0;
            return true;
        }
        case WM_ERASEBKGND:
            result = 1;
            return true;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                host_.Resize(LOWORD(lp), HIWORD(lp));
                wallDirty_ = true;
                Layout();
                Invalidate();
            }
            return false;
        case WM_DPICHANGED:
            dpi_ = HIWORD(wp);
            wallDirty_ = true;
            return false;  // the appbar repositions itself
        case WM_SETTINGCHANGE:
            ReadTheme();
            Invalidate();
            return false;
        case WM_TIMER:
            if (wp == kTimerWallpaper) {
                KillTimer(h, kTimerWallpaper);
                wallDirty_ = true;
                Invalidate();
                result = 0;
                return true;
            }
            if (wp == kTimerClock) {
                RefreshStatus();
                Layout();
                ArmClock();
                Invalidate();
                result = 0;
                return true;
            }
            if (wp == kTimerRedraw) {
                KillTimer(h, kTimerRedraw);
                Draw();
                result = 0;
                return true;
            }
            return false;
        case kMsgMenus:
            OnMenuAnswers();
            result = 0;
            return true;
        case WM_MOUSEMOVE: {
            bool over = inRect(logoRect_, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            const int title = TitleAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (over != hoverLogo_ || title != hoverTitle_) {
                hoverLogo_ = over;
                hoverTitle_ = title;
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                Invalidate();
                // With a menu open, moving to the next title opens that one (as menu bars do).
                if (title >= 0 && menuOpen_ && popup_.IsOpen() && title != openTitle_) OpenAppMenu(title);
            }
            return false;
        }
        case WM_MOUSELEAVE:
            hoverLogo_ = false;
            hoverTitle_ = -1;
            Invalidate();
            return false;
        case WM_LBUTTONDOWN:
            if (const int title = TitleAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); title >= 0) {
                if (popup_.IsOpen() && openTitle_ == title) popup_.Close();
                else if (GetTickCount64() - popupClosedAt_ > 250 || openTitle_ != title) OpenAppMenu(title);
                result = 0;
                return true;
            }
            if (inRect(logoRect_, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                // A click on the logo while the menu is open closes it; so does the click that deactivated the popup a moment ago.
                if (popup_.IsOpen()) popup_.Close();
                else if (GetTickCount64() - popupClosedAt_ > 250) ShowVeloraMenu();
                result = 0;
                return true;
            }
            for (const auto& [key, rect] : rightItems_) {
                if (key.rfind(L"control\n", 0) == 0 && inRect(rect, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                    if (actions_.openControl) actions_.openControl();
                    result = 0;
                    return true;
                }
                if (key.rfind(L"overflow\n", 0) == 0 && inRect(rect, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                    OpenTaskbarSettings();
                    result = 0;
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

// ======================= global app menus =======================
HWND MenuBar::AppMenuTarget() const {
    if (targetOverride_) return targetOverride_;
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return menuTarget_;  // our own popup: keep the app it belongs to
    if (IsShellClass(fg)) return nullptr;
    HWND root = GetAncestor(fg, GA_ROOT);
    if (!root) return nullptr;
    if (const win::TrackedWindow* w = tracker_.Find(root); w && !w->controllable) return nullptr;  // elevated: its menus cannot be driven from here
    return root;
}

void MenuBar::RefreshAppMenus() {
    if (!menus_) return;
    HWND target = AppMenuTarget();
    if (target != menuTarget_) {
        if (popup_.IsOpen() && openTitle_ >= 0) popup_.Close();
        menuTarget_ = target;
        appBar_ = {};
        stack_.clear();
        openTitle_ = hoverTitle_ = -1;
        LayoutTitles();
        Invalidate();
    }
    if (target) probeId_ = menus_->Probe(target);  // cheap for classic menus; cached for UI Automation
}

void MenuBar::OnMenuAnswers() {
    MenuAnswer a;
    while (menus_ && menus_->Take(&a)) {
        if (a.kind == MenuAnswer::Kind::Bar) {
            if (a.id != probeId_ || a.hwnd != menuTarget_) continue;  // a stale answer
            const bool same = appBar_.hwnd == a.hwnd && a.ok == (appBar_.tier != MenuTier::None) && (!a.ok || TitlesToString(appBar_) == TitlesToString(a.bar));
            if (same) continue;
            appBar_ = a.ok ? std::move(a.bar) : AppMenuBar{};
            appBar_.hwnd = a.hwnd;
            log::Info(std::string("app menus: ") + (appBar_.tier == MenuTier::Win32 ? "tier 1 (HMENU)" : appBar_.tier == MenuTier::Uia ? "tier 2 (UI Automation)" : "none") +
                      ", " + std::to_string(appBar_.titles.size()) + " titles");
            LayoutTitles();
            Invalidate();
        } else {
            if (a.id != expandId_ || a.hwnd != menuTarget_ || !menuOpen_ || openTitle_ < 0 || openTitle_ >= static_cast<int>(appBar_.titles.size())) continue;
            if (!a.ok || a.items.empty()) {
                log::Info("app menus: nothing to show for this menu");
                menuOpen_ = false;
                Invalidate();
                continue;
            }
            Level level;
            level.label = stack_.empty() ? appBar_.titles[openTitle_].label : pendingLabel_;
            level.items = std::move(a.items);
            level.uiaIndex = pendingPath_.empty() ? -1 : pendingPath_.back();
            stack_.push_back(std::move(level));
            ShowLevel();
        }
    }
}

void MenuBar::OpenAppMenu(int title) {
    if (!menus_ || title < 0 || title >= static_cast<int>(appBar_.titles.size()) || !menuTarget_) return;
    const AppMenuTitle& t = appBar_.titles[title];
    if (!t.enabled) return;
    if (popup_.IsOpen()) popup_.Close();  // another title's (or the Velora) menu
    if (t.direct) {  // a command in the bar itself
        SetForegroundWindow(menuTarget_);
        menus_->InvokeTitle(menuTarget_, appBar_.tier, t.id, t.index);
        return;
    }
    openTitle_ = title;
    menuOpen_ = true;
    stack_.clear();
    pendingPath_.clear();
    expandId_ = menus_->Expand(menuTarget_, appBar_.tier, t.index, {});  // the popup opens when the worker answers
    Invalidate();
}

void MenuBar::ShowLevel() {
    if (stack_.empty() || openTitle_ < 0 || openTitle_ >= static_cast<int>(titleCapsule_.size())) return;
    std::vector<MenuItem> items;
    if (stack_.size() > 1) {  // a drilled-in submenu starts with a way back
        MenuItem back;
        back.id = kBackItem;
        back.label = L"\u2039  " + stack_.back().label;
        items.push_back(back);
        items.push_back(Separator());
    }
    const auto& list = stack_.back().items;
    for (size_t i = 0; i < list.size(); ++i) {
        const AppMenuItem& it = list[i];
        if (it.separator) {
            items.push_back(Separator());
            continue;
        }
        MenuItem m;
        m.id = static_cast<unsigned>(i + 1);
        m.label = it.label;
        m.enabled = it.enabled;
        m.checked = it.checked;
        m.submenu = it.submenu;
        m.keepOpen = it.submenu;
        m.shortcut = it.shortcut;
        items.push_back(std::move(m));
    }
    RECT r{};
    GetWindowRect(hwnd_, &r);
    const float s = ui::Scale(dpi_);
    const float top = (tokens::size::kMenuBarHeight + tm::kCapsuleHeight) / 2 + tm::kPopupGap;
    const int title = openTitle_;
    menuOpen_ = true;
    log::Info("app menus: show level depth=" + std::to_string(stack_.size()) + " items=" + std::to_string(items.size()) + " open=" + std::to_string(popup_.IsOpen()) + " title=" + std::to_string(openTitle_) + " popupTitle=" + std::to_string(popupTitle_));
    if (popup_.IsOpen() && openTitle_ == popupTitle_) {  // drilling in or back: swap the rows of the open popup
        popup_.Update(std::move(items), [this](unsigned id) { RunAppItem(id); });
        return;
    }
    reopening_ = true;  // closing the previous level must not clear the open title
    if (popup_.IsOpen()) popup_.Close();
    reopening_ = false;
    popupTitle_ = title;
    popup_.SetOnHorizontal([this, title](int dir) {
        const int n = static_cast<int>(appBar_.titles.size());
        for (int step = 1; step <= n; ++step) {
            const int next = ((title + dir * step) % n + n) % n;
            if (next < static_cast<int>(titleHit_.size()) && appBar_.titles[next].enabled) {
                OpenAppMenu(next);
                return;
            }
        }
    });
    const bool opened = popup_.Open(
        hwnd_, r.left + static_cast<int>(titleCapsule_[title].left * s), r.top + static_cast<int>(top * s), std::move(items),
        [this](unsigned id) { RunAppItem(id); },
        [this] {
            if (reopening_) return;
            menuOpen_ = false;
            popupClosedAt_ = GetTickCount64();
            Invalidate();
        });
    if (!opened) {
        menuOpen_ = false;
        Invalidate();
    }
}

void MenuBar::RunAppItem(unsigned id) {
    log::Info("app menus: choose id=" + std::to_string(id) + " depth=" + std::to_string(stack_.size()) + (menus_ ? "" : " (no menus)"));
    if (stack_.empty() || !menus_) return;
    if (id == kBackItem) {
        stack_.pop_back();
        ShowLevel();
        return;
    }
    if (id == 0 || id > stack_.back().items.size()) return;
    const AppMenuItem item = stack_.back().items[id - 1];
    const AppMenuTitle& title = appBar_.titles[openTitle_];
    std::vector<int> path;  // UIA: the submenu items opened on the way down
    for (size_t i = 1; i < stack_.size(); ++i) path.push_back(stack_[i].uiaIndex);
    if (item.submenu) {
        if (appBar_.tier == MenuTier::Win32) {
            Level level;
            level.label = item.label;
            level.items = item.children;
            stack_.push_back(std::move(level));
            ShowLevel();
        } else {
            path.push_back(item.uiaIndex);
            pendingPath_ = path;
            pendingLabel_ = item.label;
            menuOpen_ = true;
            expandId_ = menus_->Expand(menuTarget_, appBar_.tier, title.index, path);
        }
        return;
    }
    // A command: bring the app forward first (the popup had the focus), then run it.
    SetForegroundWindow(menuTarget_);
    path.push_back(item.uiaIndex);
    menus_->Invoke(menuTarget_, appBar_.tier, title.index, std::move(path), item.id, item.label);
    stack_.clear();
    menuOpen_ = false;
    popupClosedAt_ = 0;  // the popup closed because a command was chosen, not by a click on the bar: the next click on a title opens at once
    Invalidate();
}

}  // namespace velora::menubar
