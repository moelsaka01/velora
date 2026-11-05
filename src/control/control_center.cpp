#include "control/control_center.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>

#include "core/log.h"
#include "core/settings_file.h"
#include "render/tokens.h"
#include "ui/backdrop.h"
#include "ui/ui_common.h"

namespace velora::control {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"VeloraControlCenter";
constexpr UINT kMsgSnapshot = WM_APP + 1, kMsgWorkDone = WM_APP + 2;
constexpr float kWidth = 344.0f, kPad = 14.0f, kRow = 56.0f, kNetRow = 36.0f, kSliderRow = 64.0f, kInfoRow = 34.0f, kRadius = 20.0f;
constexpr float kSwitchW = 40.0f, kSwitchH = 24.0f, kTrackH = 6.0f, kKnob = 20.0f;
constexpr int kMaxNetworks = 6;

bool RegisterOnce(HINSTANCE instance, WNDPROC proc) {
    static ATOM atom = 0;
    if (atom) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    atom = RegisterClassExW(&wc);
    return atom != 0;
}

bool In(float x, float y, float l, float t, float r, float b) { return x >= l && x < r && y >= t && y < b; }

}  // namespace

Backend Backend::System() {
    Backend b;
    b.read = [] {
        Snapshot s;
        s.audio = ReadAudio();
        s.wifi = ReadWifi();
        s.wifiRadio = ReadRadio(RadioKind::Wifi);
        s.bluetooth = ReadRadio(RadioKind::Bluetooth);
        s.brightness = ReadBrightness();
        s.darkKnown = ReadDarkMode(&s.dark);
        s.battery = ReadBattery();
        s.dnd = SettingBool("notify.dnd", false);
        return s;
    };
    b.setVolume = [](float v) { SetVolume(v); };
    b.setMuted = [](bool m) { SetMuted(m); };
    b.setBrightness = [](int p) { SetBrightness(p); };
    b.setRadio = [](RadioKind k, bool on) { SetRadio(k, on); };
    b.connect = [](const std::wstring& profile) { ConnectSaved(profile); };
    b.setDark = [](bool d) { SetDarkMode(d); };
    b.setDnd = [](bool on) { SetSettingBool("notify.dnd", on); };
    b.openSoundSettings = [] { ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL); };
    b.openWifiSettings = [] { ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL); };
    return b;
}

ControlCenter::ControlCenter(HINSTANCE instance, Backend backend) : instance_(instance), backend_(std::move(backend)) {}

ControlCenter::~ControlCenter() { Hide(); }

float ControlCenter::Scale() const { return ui::Scale(dpi_); }

void ControlCenter::Show() {
    if (hwnd_) return;
    if (!RegisterOnce(instance_, &ControlCenter::WndProc)) return;
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    // Provisional size at the monitor's scale; Resize() fits the content once the layout is known.
    const UINT dpi = 96;
    (void)dpi;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Velora Control Center", WS_POPUP, mi.rcWork.right - 400,
                            mi.rcWork.top + 6, 400, 300, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("control center: CreateWindowEx failed");
        return;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    if (!host_.Initialize(hwnd_)) {
        log::Warn("control center: render host unavailable");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return;
    }
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, ui::SystemTheme());
    render::ApplyRoundedCorners(hwnd_, false);
    ++generation_;
    wall_.Reset();
    hover_ = drag_ = -1;
    haveSnapshot_ = false;
    snap_ = {};
    Resize();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    Refresh();
    Draw();
}

void ControlCenter::Hide() {
    if (!hwnd_) return;
    HWND h = hwnd_;
    hwnd_ = nullptr;  // before DestroyWindow: WM_ACTIVATE during teardown must not re-enter
    ++generation_;
    wall_.Reset();
    host_.Shutdown();
    DestroyWindow(h);
    if (onHidden_) onHidden_();
}

void ControlCenter::Refresh() {
    if (!hwnd_ || !backend_.read) return;
    ++pending_;
    const HWND h = hwnd_;
    const std::uint32_t gen = generation_;
    auto read = backend_.read;
    std::thread([h, gen, read] {
        auto* s = new Snapshot(read());
        if (!PostMessageW(h, kMsgSnapshot, gen, reinterpret_cast<LPARAM>(s))) delete s;
    }).detach();
}

void ControlCenter::Async(std::function<void()> work, bool refreshAfter) {
    if (!hwnd_) return;
    ++pending_;
    const HWND h = hwnd_;
    const std::uint32_t gen = generation_;
    std::thread([h, gen, work = std::move(work), refreshAfter] {
        work();
        PostMessageW(h, kMsgWorkDone, gen, refreshAfter ? 1 : 0);
    }).detach();
}

// ---- layout ---------------------------------------------------------------------------------------------------------------

void ControlCenter::Layout() {
    hits_.clear();
    float y = kPad;
    const float left = kPad, right = kWidth - kPad;
    auto toggle = [&](Kind kind, float rowTop) {
        hits_.push_back({kind, -1, right - 14 - kSwitchW, rowTop + (kRow - kSwitchH) / 2, right - 14, rowTop + (kRow + kSwitchH) / 2});
    };
    // Wi-Fi tile (+ networks).
    hits_.push_back({Kind::WifiExpand, -1, left, y, right - 14 - kSwitchW - 8, y + kRow});
    toggle(Kind::Wifi, y);
    y += kRow + 4;
    if (wifiOpen_ && snap_.wifiRadio.on) {
        const int n = std::min<int>(kMaxNetworks, static_cast<int>(snap_.wifi.networks.size()));
        for (int i = 0; i < n; ++i) {
            hits_.push_back({Kind::WifiNetwork, i, left + 8, y, right - 8, y + kNetRow});
            y += kNetRow;
        }
        y += 4;
    }
    toggle(Kind::Bluetooth, y);
    y += kRow + 4;
    toggle(Kind::Dark, y);
    y += kRow + 4;
    toggle(Kind::Dnd, y);
    y += kRow + 8;
    // Sliders.
    if (snap_.brightness.available) hits_.push_back({Kind::Brightness, -1, left + 18, y + 38, right - 18, y + 38 + kKnob});
    y += kSliderRow;
    if (snap_.audio.available) {
        hits_.push_back({Kind::Mute, -1, left + 14, y + 8, left + 14 + 28, y + 8 + 24});
        hits_.push_back({Kind::Volume, -1, left + 18, y + 38, right - 18, y + 38 + kKnob});
    }
    y += kSliderRow;
    hits_.push_back({Kind::SoundSettings, -1, right - 130, y, right, y + kInfoRow});
    y += kInfoRow;
    if (snap_.battery.present) y += kInfoRow;
    height_ = y + kPad - 2;
}

void ControlCenter::Resize() {
    Layout();
    if (!hwnd_) return;
    POINT pt{};
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
    const float s = Scale();
    const int w = static_cast<int>(std::lround(kWidth * s)), h = static_cast<int>(std::lround(height_ * s));
    const int x = mi.rcWork.right - w - static_cast<int>(8 * s), y = mi.rcWork.top + static_cast<int>(6 * s);
    SetWindowPos(hwnd_, nullptr, x, y, w, std::min<int>(h, mi.rcWork.bottom - y - 8), SWP_NOACTIVATE | SWP_NOZORDER);
    host_.Resize(static_cast<UINT>(w), static_cast<UINT>(std::min<int>(h, mi.rcWork.bottom - y - 8)));
    wall_.Reset();
}

int ControlCenter::HitAt(float x, float y) const {
    // Later entries sit on top (toggles over the row body).
    for (int i = static_cast<int>(hits_.size()) - 1; i >= 0; --i)
        if (In(x, y, hits_[i].left, hits_[i].top, hits_[i].right, hits_[i].bottom)) return i;
    return -1;
}

// ---- actions --------------------------------------------------------------------------------------------------------------

void ControlCenter::SliderTo(const Hit& hit, float fraction, bool commit) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (hit.kind == Kind::Volume) {
        snap_.audio.volume = fraction;
        if (snap_.audio.muted && fraction > 0) {
            snap_.audio.muted = false;
            if (backend_.setMuted) backend_.setMuted(false);
        }
        if (backend_.setVolume) backend_.setVolume(fraction);  // cheap: inline, so the sound follows the pointer
    } else if (hit.kind == Kind::Brightness) {
        const int pct = static_cast<int>(std::lround(fraction * 100));
        snap_.brightness.percent = pct;
        const std::uint64_t now = GetTickCount64();
        if (commit || now - lastBrightnessSend_ > 150) {  // WMI is slow: not on every mouse move
            lastBrightnessSend_ = now;
            auto set = backend_.setBrightness;
            if (set) Async([set, pct] { set(pct); }, false);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ControlCenter::Click(const Hit& hit, float fraction) {
    switch (hit.kind) {
        case Kind::Wifi: {
            if (!snap_.wifiRadio.present) break;
            const bool on = !snap_.wifiRadio.on;
            snap_.wifiRadio.on = on;
            auto set = backend_.setRadio;
            if (set) Async([set, on] { set(RadioKind::Wifi, on); }, true);
            break;
        }
        case Kind::WifiExpand:
            if (snap_.wifiRadio.on && snap_.wifi.available) {
                wifiOpen_ = !wifiOpen_;
                Resize();
            }
            break;
        case Kind::WifiNetwork:
            if (hit.index >= 0 && hit.index < static_cast<int>(snap_.wifi.networks.size())) {
                const WifiNetwork& n = snap_.wifi.networks[hit.index];
                if (n.connected) break;
                if (!n.profile.empty()) {
                    auto connect = backend_.connect;
                    const std::wstring profile = n.profile;
                    if (connect) Async([connect, profile] { connect(profile); }, true);
                } else if (backend_.openWifiSettings) {
                    backend_.openWifiSettings();  // a network Windows has no saved profile for: its own flow asks for the key
                    Hide();
                    return;
                }
            }
            break;
        case Kind::Bluetooth: {
            if (!snap_.bluetooth.present) break;
            const bool on = !snap_.bluetooth.on;
            snap_.bluetooth.on = on;
            auto set = backend_.setRadio;
            if (set) Async([set, on] { set(RadioKind::Bluetooth, on); }, true);
            break;
        }
        case Kind::Dark:
            if (snap_.darkKnown) {
                snap_.dark = !snap_.dark;
                if (backend_.setDark) backend_.setDark(snap_.dark);
            }
            break;
        case Kind::Dnd:
            snap_.dnd = !snap_.dnd;
            if (backend_.setDnd) backend_.setDnd(snap_.dnd);
            break;
        case Kind::Brightness:
        case Kind::Volume:
            SliderTo(hit, fraction, true);
            break;
        case Kind::Mute:
            if (snap_.audio.available) {
                snap_.audio.muted = !snap_.audio.muted;
                if (backend_.setMuted) backend_.setMuted(snap_.audio.muted);
            }
            break;
        case Kind::SoundSettings:
            if (backend_.openSoundSettings) backend_.openSoundSettings();
            Hide();
            return;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ControlCenter::WaitIdle() {
    const ULONGLONG end = GetTickCount64() + 8000;
    while (GetTickCount64() < end) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (pending_ == 0) return;
        Sleep(5);
    }
}

// ---- drawing --------------------------------------------------------------------------------------------------------------

void ControlCenter::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const tokens::Theme theme = ui::SystemTheme();
    const bool dark = theme == tokens::Theme::Dark;
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    Layout();
    const float s = Scale();
    const float w = static_cast<float>(host_.Width()) / s, h = static_cast<float>(host_.Height()) / s;
    const D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), kRadius, kRadius);
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);

    // Glass: blurred wallpaper behind, neutral veil on top, light rim.
    if (!wall_ && ui::TransparencyEnabled()) {
        RECT win{};
        MONITORINFO mi{sizeof mi};
        ui::Pixels px;
        if (GetWindowRect(hwnd_, &win) && GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi) &&
            ui::BlurredWallpaper(mi.rcMonitor, win, 1, tokens::dock::kBlurSigma, dpi_, &px))
            wall_ = ui::MakeBitmap(dc, px);
    }
    bool blurred = false;
    if (wall_) {
        ComPtr<ID2D1BitmapBrush> bb;
        if (SUCCEEDED(dc->CreateBitmapBrush(wall_.Get(), &bb))) {
            float bx = 96, by = 96;
            wall_->GetDpi(&bx, &by);
            bb->SetTransform(D2D1::Matrix3x2F::Scale(bx / static_cast<float>(dpi_), by / static_cast<float>(dpi_)));
            dc->FillRoundedRectangle(body, bb.Get());
            blurred = true;
        }
    }
    b->SetColor(dark ? D2D1::ColorF(0.11f, 0.11f, 0.13f, blurred ? 0.74f : 0.96f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, blurred ? 0.74f : 0.97f));
    dc->FillRoundedRectangle(body, b.Get());
    b->SetColor(D2D1::ColorF(1, 1, 1, dark ? 0.14f : 0.55f));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), kRadius - 0.5f, kRadius - 0.5f), b.Get(), 1.0f);

    const tokens::Palette& p = tokens::PaletteFor(theme);
    const D2D1_COLOR_F text = ui::Rgba(p.text), dim = ui::Rgba(p.textSecondary), accent = ui::Rgba(p.accent);
    const D2D1_COLOR_F tile = dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(1, 1, 1, 0.62f);
    const D2D1_COLOR_F off = dark ? D2D1::ColorF(1, 1, 1, 0.24f) : D2D1::ColorF(0, 0, 0, 0.20f);
    IDWriteTextFormat* title = ui::Format(14, DWRITE_FONT_WEIGHT_MEDIUM);
    IDWriteTextFormat* sub = ui::Format(12);
    const float left = kPad, right = w - kPad;

    auto drawToggleTile = [&](Kind kind, float top, const std::wstring& name, const std::wstring& detail, bool on, bool enabled) {
        b->SetColor(tile);
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, right, top + kRow), 14, 14), b.Get());
        D2D1_COLOR_F t = text, d = dim;
        if (!enabled) { t.a *= 0.45f; d.a *= 0.45f; }
        b->SetColor(t);
        ui::DrawLabel(dc, b.Get(), name, D2D1::RectF(left + 14, top + 9, right - 14 - kSwitchW - 10, top + 29), title);
        b->SetColor(d);
        ui::DrawLabel(dc, b.Get(), detail, D2D1::RectF(left + 14, top + 28, right - 14 - kSwitchW - 10, top + 46), sub);
        for (const Hit& hit : hits_) {
            if (hit.kind != kind) continue;
            const D2D1_RECT_F r = D2D1::RectF(hit.left, hit.top, hit.right, hit.bottom);
            D2D1_COLOR_F track = on && enabled ? accent : off;
            if (!enabled) track.a *= 0.5f;
            b->SetColor(track);
            dc->FillRoundedRectangle(D2D1::RoundedRect(r, kSwitchH / 2, kSwitchH / 2), b.Get());
            const float k = kSwitchH - 6, kx = on ? r.right - 3 - k : r.left + 3;
            b->SetColor(D2D1::ColorF(1, 1, 1, enabled ? 1.0f : 0.6f));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + k / 2, (r.top + r.bottom) / 2), k / 2, k / 2), b.Get());
        }
    };
    auto rowTop = [&](Kind kind) {
        for (const Hit& hit : hits_)
            if (hit.kind == kind) return hit.top - (kRow - kSwitchH) / 2;
        return 0.0f;
    };

    // Wi-Fi.
    {
        const float top = rowTop(Kind::Wifi);
        std::wstring detail = !snap_.wifiRadio.present ? (haveSnapshot_ ? L"No Wi-Fi adapter" : L"...") : !snap_.wifiRadio.on ? L"Off" : snap_.wifi.connected ? snap_.wifi.ssid : L"Not connected";
        drawToggleTile(Kind::Wifi, top, L"Wi-Fi", detail, snap_.wifiRadio.on, snap_.wifiRadio.present);
        if (wifiOpen_ && snap_.wifiRadio.on) {
            for (const Hit& hit : hits_) {
                if (hit.kind != Kind::WifiNetwork) continue;
                const WifiNetwork& n = snap_.wifi.networks[hit.index];
                if (hover_ >= 0 && hover_ < static_cast<int>(hits_.size()) && hits_[hover_].kind == Kind::WifiNetwork && hits_[hover_].index == hit.index) {
                    b->SetColor(dark ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(0, 0, 0, 0.06f));
                    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(hit.left, hit.top, hit.right, hit.bottom), 8, 8), b.Get());
                }
                b->SetColor(text);
                ui::DrawLabel(dc, b.Get(), (n.connected ? L"✓  " : L"    ") + n.ssid, D2D1::RectF(hit.left + 8, hit.top, hit.right - 70, hit.bottom), sub);
                b->SetColor(dim);
                const std::wstring meta = std::wstring(n.profile.empty() ? L"" : L"saved  ") + (n.secure ? L"locked  " : L"") + std::to_wstring(n.signal) + L"%";
                ui::DrawLabel(dc, b.Get(), meta, D2D1::RectF(hit.left, hit.top, hit.right - 8, hit.bottom), ui::Format(11), DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
        }
    }
    drawToggleTile(Kind::Bluetooth, rowTop(Kind::Bluetooth), L"Bluetooth", !snap_.bluetooth.present ? (haveSnapshot_ ? L"No Bluetooth adapter" : L"...") : snap_.bluetooth.on ? L"On" : L"Off",
                   snap_.bluetooth.on, snap_.bluetooth.present);
    drawToggleTile(Kind::Dark, rowTop(Kind::Dark), L"Dark mode", !snap_.darkKnown ? (haveSnapshot_ ? L"Unavailable" : L"...") : snap_.dark ? L"On" : L"Off", snap_.dark, snap_.darkKnown);
    drawToggleTile(Kind::Dnd, rowTop(Kind::Dnd), L"Do Not Disturb", snap_.dnd ? L"On (Velora notifications are held)" : L"Off", snap_.dnd, true);

    // Sliders.
    auto slider = [&](const wchar_t* name, Kind kind, float value, bool available, float top, const wchar_t* unavailable) {
        b->SetColor(tile);
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, right, top + kSliderRow - 6), 14, 14), b.Get());
        b->SetColor(text);
        ui::DrawLabel(dc, b.Get(), name, D2D1::RectF(left + 14 + (kind == Kind::Volume ? 34 : 0), top + 8, right - 14, top + 30), title);
        if (!available) {
            b->SetColor(dim);
            ui::DrawLabel(dc, b.Get(), haveSnapshot_ ? unavailable : L"...", D2D1::RectF(left + 14, top + 32, right - 14, top + 52), sub);
            return;
        }
        for (const Hit& hit : hits_) {
            if (hit.kind != kind) continue;
            const float cy = (hit.top + hit.bottom) / 2;
            b->SetColor(off);
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(hit.left, cy - kTrackH / 2, hit.right, cy + kTrackH / 2), kTrackH / 2, kTrackH / 2), b.Get());
            const float x = hit.left + (hit.right - hit.left) * std::clamp(value, 0.0f, 1.0f);
            b->SetColor(accent);
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(hit.left, cy - kTrackH / 2, x, cy + kTrackH / 2), kTrackH / 2, kTrackH / 2), b.Get());
            b->SetColor(D2D1::ColorF(1, 1, 1, 1));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), kKnob / 2, kKnob / 2), b.Get());
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.18f));
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), kKnob / 2 - 0.5f, kKnob / 2 - 0.5f), b.Get(), 0.5f);
        }
    };
    float sliderTop = 0, soundTop = 0;
    for (const Hit& hit : hits_) {
        if (hit.kind == Kind::SoundSettings) soundTop = hit.top - kSliderRow;
    }
    sliderTop = soundTop - kSliderRow;
    slider(L"Display", Kind::Brightness, snap_.brightness.percent / 100.0f, snap_.brightness.available, sliderTop, L"Brightness is not available for this display");
    slider(L"Sound", Kind::Volume, snap_.audio.muted ? 0.0f : snap_.audio.volume, snap_.audio.available, soundTop, L"No output device");
    if (snap_.audio.available) {  // speaker glyph: a cone and waves (a slash while muted)
        for (const Hit& hit : hits_) {
            if (hit.kind != Kind::Mute) continue;
            const float cx = hit.left + 10, cy = (hit.top + hit.bottom) / 2;
            b->SetColor(text);
            dc->FillRectangle(D2D1::RectF(cx - 6, cy - 3, cx - 2, cy + 3), b.Get());
            ComPtr<ID2D1PathGeometry> g;
            ComPtr<ID2D1Factory> f;
            dc->GetFactory(&f);
            if (f && SUCCEEDED(f->CreatePathGeometry(&g))) {
                ComPtr<ID2D1GeometrySink> sink;
                g->Open(&sink);
                sink->BeginFigure(D2D1::Point2F(cx - 2, cy - 3), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + 3, cy - 7));
                sink->AddLine(D2D1::Point2F(cx + 3, cy + 7));
                sink->AddLine(D2D1::Point2F(cx - 2, cy + 3));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                dc->FillGeometry(g.Get(), b.Get());
            }
            if (snap_.audio.muted) dc->DrawLine(D2D1::Point2F(cx - 7, cy + 8), D2D1::Point2F(cx + 8, cy - 8), b.Get(), 1.6f);
            else {
                dc->DrawLine(D2D1::Point2F(cx + 6, cy - 4), D2D1::Point2F(cx + 8, cy), b.Get(), 1.4f);
                dc->DrawLine(D2D1::Point2F(cx + 8, cy), D2D1::Point2F(cx + 6, cy + 4), b.Get(), 1.4f);
            }
        }
    }
    // Output device + link, battery.
    {
        b->SetColor(dim);
        const float top = soundTop + kSliderRow;
        ui::DrawLabel(dc, b.Get(), snap_.audio.available ? L"Output: " + snap_.audio.device : L"Output: none", D2D1::RectF(left + 8, top, right - 140, top + kInfoRow), sub);
        b->SetColor(accent);
        ui::DrawLabel(dc, b.Get(), L"Sound settings", D2D1::RectF(right - 130, top, right - 6, top + kInfoRow), sub, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (snap_.battery.present) {
            b->SetColor(text);
            std::wstring line = L"Battery " + std::to_wstring(snap_.battery.percent) + L"%";
            if (snap_.battery.charging) line += L"  (charging)";
            else if (snap_.battery.onAc) line += L"  (plugged in)";
            ui::DrawLabel(dc, b.Get(), line, D2D1::RectF(left + 8, top + kInfoRow, right - 8, top + 2 * kInfoRow), sub);
        }
    }
    if (!host_.EndDraw()) {
        log::Warn("control center: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        wall_.Reset();
    }
}

// ---- messages -------------------------------------------------------------------------------------------------------------

LRESULT CALLBACK ControlCenter::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<ControlCenter*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (msg == kMsgSnapshot) {  // always consume the heap snapshot
        auto* snap = reinterpret_cast<Snapshot*>(lp);
        std::unique_ptr<Snapshot> own(snap);
        if (self->pending_ > 0) --self->pending_;
        if (self->hwnd_ == h && wp == self->generation_) {
            self->snap_ = std::move(*own);
            self->haveSnapshot_ = true;
            self->Resize();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    if (!self->hwnd_ && msg != WM_NCCREATE && msg != WM_CREATE) return DefWindowProcW(h, msg, wp, lp);
    if (!self->hwnd_) self->hwnd_ = h;
    return self->OnMessage(msg, wp, lp);
}

LRESULT ControlCenter::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const float s = Scale();
    switch (msg) {
        case kMsgWorkDone:
            if (pending_ > 0) --pending_;
            if (lp == 1 && wp == generation_) Refresh();  // a write finished: read the system again
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Draw();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) Hide();
            return 0;
        case WM_MOUSEMOVE: {
            const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
            if (drag_ >= 0 && drag_ < static_cast<int>(hits_.size())) {
                const Hit hit = hits_[drag_];
                SliderTo(hit, (x - hit.left) / (hit.right - hit.left), false);
                return 0;
            }
            const int i = HitAt(x, y);
            if (i != hover_) {
                hover_ = i;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
            const int i = HitAt(x, y);
            if (i < 0) return 0;
            const Hit hit = hits_[i];
            if (hit.kind == Kind::Volume || hit.kind == Kind::Brightness) {
                drag_ = i;
                SetCapture(hwnd_);
            }
            Click(hit, (x - hit.left) / std::max(1.0f, hit.right - hit.left));
            return 0;
        }
        case WM_LBUTTONUP:
            if (drag_ >= 0) {
                if (drag_ < static_cast<int>(hits_.size()) && hits_[drag_].kind == Kind::Brightness) {
                    const Hit hit = hits_[drag_];
                    const float x = GET_X_LPARAM(lp) / s;
                    SliderTo(hit, (x - hit.left) / (hit.right - hit.left), true);  // the final value always reaches the display
                }
                drag_ = -1;
                ReleaseCapture();
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) Hide();
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::control
