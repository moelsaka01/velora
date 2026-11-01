#include "menubar/menubar.h"

#include <powrprof.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <thread>

#include "core/log.h"
#include "core/version.h"
#include "ui/ui_common.h"

#pragma comment(lib, "powrprof.lib")

namespace velora::menubar {

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT_PTR kTimerClock = 1;
constexpr UINT_PTR kTimerRedraw = 2;
constexpr UINT_PTR kTimerOverflowEnter = 3;
constexpr UINT_PTR kTimerOverflowCheck = 4;
constexpr float kPad = 12.0f;

enum Cmd : UINT { kAbout = 1, kSettings, kApps, kSleep, kRestart, kShutDown, kExit };

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

// Opens Windows' own notification-overflow flyout the documented way: the Win+B shortcut moves
// focus to the notification area, Enter then opens the hidden-icons flyout. Keyboard input only;
// nothing is injected into Explorer.
void SendKeys(std::initializer_list<std::pair<WORD, bool>> keys) {
    std::vector<INPUT> in;
    for (auto [vk, up] : keys) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        in.push_back(i);
    }
    SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
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
    : bus_(bus), tracker_(tracker), actions_(std::move(actions)) {
    for (EventType t : {EventType::WindowActivated, EventType::WindowTitleChanged, EventType::WindowDestroyed})
        tokens_.push_back(bus_.Subscribe(t, [this](const Event&) { OnWindowEvent(); }));
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
    Draw();
}

void MenuBar::Detach() {
    if (!hwnd_) return;
    KillTimer(hwnd_, kTimerClock);
    KillTimer(hwnd_, kTimerRedraw);
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
    backdrop_ = effects_ && ui::TransparencyEnabled() &&
                render::ApplyBackdrop(hwnd_, render::Backdrop::Acrylic, theme_);
    if (!backdrop_) render::ApplyBackdrop(hwnd_, render::Backdrop::None, theme_);
    render::ApplyRoundedCorners(hwnd_, false);
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
    float h = tokens::size::kMenuBarHeight;
    logoRect_ = D2D1::RectF(6, 3, 6 + 28, h - 3);
    std::wstring name = appName_.empty() ? L"Desktop" : appName_;
    float w = ui::MeasureWidth(name, ui::Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD)) + 16;
    appRect_ = D2D1::RectF(logoRect_.right + 2, 3, logoRect_.right + 2 + std::min(w, 360.0f), h - 3);

    // Right side, from the edge inwards: clock, then status items.
    rightItems_.clear();
    float width = static_cast<float>(host_.Width()) / ui::Scale(dpi_);
    float x = width - kPad;
    IDWriteTextFormat* fmt = ui::Format(13);
    auto place = [&](const std::wstring& id, const std::wstring& text) {
        if (text.empty()) return;
        float tw = ui::MeasureWidth(text, fmt) + 4;
        rightItems_.emplace_back(id + L"\n" + text, D2D1::RectF(x - tw, 0, x, h));
        x -= tw + 14;
    };
    place(L"clock", clockText_);
    place(L"overflow", L"\u22EF");
    for (const auto& s : status_) place(s.id, s.text);
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

    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(ui::Rgba(backdrop_ ? p.surface : p.surfaceStrong), &brush);
    dc->FillRectangle(D2D1::RectF(0, 0, w, h), brush.Get());
    brush->SetColor(ui::Rgba(p.separator));
    dc->FillRectangle(D2D1::RectF(0, h - 1, w, h), brush.Get());

    if (hoverLogo_ || menuOpen_) {
        brush->SetColor(ui::Rgba(p.separator));
        dc->FillRoundedRectangle(D2D1::RoundedRect(logoRect_, tokens::radius::kSmall, tokens::radius::kSmall), brush.Get());
    }
    // Logo: original mark (accent disc with a "V"), not an OS glyph.
    brush->SetColor(ui::Rgba(p.accent));
    float cx = (logoRect_.left + logoRect_.right) / 2, cy = h / 2;
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8, 8), brush.Get());
    brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    ui::DrawLabel(dc, brush.Get(), L"V", D2D1::RectF(cx - 8, 0, cx + 8, h), ui::Format(11, DWRITE_FONT_WEIGHT_BOLD),
                  DWRITE_TEXT_ALIGNMENT_CENTER);

    brush->SetColor(ui::Rgba(p.text));
    ui::DrawLabel(dc, brush.Get(), appName_, D2D1::RectF(appRect_.left + 8, 0, appRect_.right, h),
                  ui::Format(13, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    for (const auto& [key, rect] : rightItems_) {
        std::wstring text = key.substr(key.find(L'\n') + 1);
        ui::DrawLabel(dc, brush.Get(), text, rect, ui::Format(13), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if (!host_.EndDraw()) {
        log::Warn("menubar: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        Invalidate();
    }
}

void MenuBar::ShowVeloraMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kAbout, L"About Velora");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSettings, L"Settings\u2026");
    AppendMenuW(menu, MF_STRING, kApps, L"Apps");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSleep, L"Sleep");
    AppendMenuW(menu, MF_STRING, kRestart, L"Restart\u2026");
    AppendMenuW(menu, MF_STRING, kShutDown, L"Shut Down\u2026");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExit, L"Exit Velora");

    RECT r{};
    GetWindowRect(hwnd_, &r);
    float s = ui::Scale(dpi_);
    menuOpen_ = true;
    Draw();
    SetForegroundWindow(hwnd_);  // required for the menu to dismiss correctly
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                              r.left + static_cast<int>(logoRect_.left * s), r.bottom, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    menuOpen_ = false;
    Invalidate();

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
                Layout();
                Invalidate();
            }
            return false;
        case WM_DPICHANGED:
            dpi_ = HIWORD(wp);
            return false;  // the appbar repositions itself
        case WM_SETTINGCHANGE:
            ReadTheme();
            Invalidate();
            return false;
        case WM_TIMER:
            if (wp == kTimerClock) {
                RefreshStatus();
                Layout();
                ArmClock();
                Invalidate();
                result = 0;
                return true;
            }
            if (wp == kTimerOverflowEnter) {
                KillTimer(h, kTimerOverflowEnter);
                SendKeys({{VK_RETURN, false}, {VK_RETURN, true}});
                SetTimer(h, kTimerOverflowCheck, 600, nullptr);
                result = 0;
                return true;
            }
            if (wp == kTimerOverflowCheck) {
                KillTimer(h, kTimerOverflowCheck);
                // Win+B needs a visible taskbar on some builds. If the flyout did not appear, open the page where
                // the user manages "Other system tray icons" instead.
                HWND fly = FindWindowW(L"TopLevelWindowForOverflowXamlIsland", nullptr);
                if (!fly || !IsWindowVisible(fly)) {
                    log::Warn("menubar: overflow flyout did not appear; opening ms-settings:taskbar");
                    ShellExecuteW(nullptr, L"open", L"ms-settings:taskbar", nullptr, nullptr, SW_SHOWNORMAL);
                }
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
        case WM_MOUSEMOVE: {
            bool over = inRect(logoRect_, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (over != hoverLogo_) {
                hoverLogo_ = over;
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                Invalidate();
            }
            return false;
        }
        case WM_MOUSELEAVE:
            hoverLogo_ = false;
            Invalidate();
            return false;
        case WM_LBUTTONDOWN:
            if (inRect(logoRect_, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                ShowVeloraMenu();
                result = 0;
                return true;
            }
            for (const auto& [key, rect] : rightItems_) {
                if (key.rfind(L"overflow\n", 0) == 0 && inRect(rect, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                    log::Info("menubar: opening the Windows notification overflow (Win+B, Enter)");
                    SendKeys({{VK_LWIN, false}, {'B', false}, {'B', true}, {VK_LWIN, true}});
                    SetTimer(h, kTimerOverflowEnter, 250, nullptr);
                    result = 0;
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

}  // namespace velora::menubar
