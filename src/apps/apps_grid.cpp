#include "apps/apps_grid.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "ui/ui_common.h"

namespace velora::apps {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"VeloraAppsGrid";
constexpr UINT_PTR kTimerFrame = 1;
constexpr float kIconDip = 56.0f;

std::uint64_t NowMs() { return GetTickCount64(); }

ATOM RegisterOnce(HINSTANCE inst, WNDPROC proc) {
    static ATOM atom = [&] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        return RegisterClassExW(&wc);
    }();
    return atom;
}

}  // namespace

GridLayout ComputeGridLayout(float viewWidth) {
    GridLayout g;
    float usable = std::max(0.0f, viewWidth - 2 * 48.0f);
    g.columns = std::clamp(static_cast<int>(usable / g.tileW), 3, 9);
    float used = g.columns * g.tileW;
    g.left = std::max(0.0f, (viewWidth - used) / 2);
    return g;
}

float ScrollToRow(float scroll, int row, float rowHeight, float viewHeight) {
    float top = row * rowHeight, bottom = top + rowHeight;
    if (top < scroll) return top;
    if (bottom > scroll + viewHeight) return bottom - viewHeight;
    return scroll;
}

AppsGrid::AppsGrid(HINSTANCE instance) : instance_(instance) {}

AppsGrid::~AppsGrid() { Hide(); }

void AppsGrid::Toggle() {
    if (hwnd_) Hide();
    else Show();
}

void AppsGrid::Show() {
    if (hwnd_) return;
    if (!RegisterOnce(instance_, &AppsGrid::WndProc)) return;
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Velora Apps", WS_POPUP,
                            mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                            mi.rcMonitor.bottom - mi.rcMonitor.top, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("apps grid: CreateWindowEx failed");
        return;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    if (!host_.Initialize(hwnd_)) {
        log::Warn("apps grid: render host unavailable");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return;
    }
    tokens::Theme theme = ui::SystemTheme();
    if (!(ui::TransparencyEnabled() && render::ApplyBackdrop(hwnd_, render::Backdrop::Acrylic, theme)))
        render::ApplyBackdrop(hwnd_, render::Backdrop::None, theme);
    render::ApplyRoundedCorners(hwnd_, false);

    apps_ = AppIndex::Get().Snapshot();
    query_.clear();
    selected_ = 0;
    hover_ = -1;
    scroll_ = 0;
    Refilter();
    listener_ = ui::IconStore::Get().AddListener([this] { OnDataChanged(); });

    open_ = 0;
    anim_.Start({0, 1, tokens::timing::kNormal, 0, tokens::timing::kDecelerate},
                [this](float v, bool) { open_ = v; }, NowMs());
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    Tick();
}

void AppsGrid::Hide() {
    if (!hwnd_) return;
    ui::IconStore::Get().RemoveListener(listener_);
    HWND h = hwnd_;
    hwnd_ = nullptr;  // before DestroyWindow: WM_ACTIVATE during teardown must not re-enter
    if (timerOn_) KillTimer(h, kTimerFrame);
    timerOn_ = false;
    bitmaps_.clear();  // D2D bitmaps die before the device
    host_.Shutdown();
    DestroyWindow(h);
    apps_.clear();
    shown_.clear();
}

void AppsGrid::OnDataChanged() {
    if (!hwnd_) return;
    auto fresh = AppIndex::Get().Snapshot();
    if (fresh != apps_) {
        apps_ = std::move(fresh);
        Refilter();
    }
    Draw();
}

void AppsGrid::Refilter() {
    shown_ = Filter(apps_, query_);
    selected_ = shown_.empty() ? -1 : 0;
    scroll_ = 0;
}

float AppsGrid::MaxScroll() const {
    if (!hwnd_) return 0;
    float w = static_cast<float>(host_.Width()) / ui::Scale(dpi_), h = static_cast<float>(host_.Height()) / ui::Scale(dpi_);
    GridLayout g = ComputeGridLayout(w);
    int rows = (static_cast<int>(shown_.size()) + g.columns - 1) / g.columns;
    return std::max(0.0f, rows * g.tileH - (h - ViewTop() - 24));
}

void AppsGrid::EnsureSelectedVisible() {
    if (selected_ < 0) return;
    float w = static_cast<float>(host_.Width()) / ui::Scale(dpi_), h = static_cast<float>(host_.Height()) / ui::Scale(dpi_);
    GridLayout g = ComputeGridLayout(w);
    scroll_ = ScrollToRow(scroll_, selected_ / g.columns, g.tileH, h - ViewTop() - 24);
    scroll_ = std::clamp(scroll_, 0.0f, MaxScroll());
}

int AppsGrid::HitTile(float x, float y) const {
    float w = static_cast<float>(host_.Width()) / ui::Scale(dpi_);
    GridLayout g = ComputeGridLayout(w);
    float gx = x - g.left, gy = y - ViewTop() + scroll_;
    if (gx < 0 || gy < 0 || y < ViewTop()) return -1;
    int col = static_cast<int>(gx / g.tileW), row = static_cast<int>(gy / g.tileH);
    if (col >= g.columns) return -1;
    int idx = row * g.columns + col;
    return idx < static_cast<int>(shown_.size()) ? idx : -1;
}

void AppsGrid::Tick() {
    if (!hwnd_) return;
    bool more = anim_.Tick(NowMs());
    Draw();
    if (more && !timerOn_) {
        SetTimer(hwnd_, kTimerFrame, 16, nullptr);
        timerOn_ = true;
    } else if (!more && timerOn_) {
        KillTimer(hwnd_, kTimerFrame);
        timerOn_ = false;
    }
}

void AppsGrid::LaunchSelected() {
    if (selected_ < 0 || selected_ >= static_cast<int>(shown_.size())) return;
    AppEntry app = apps_[shown_[selected_]];
    Hide();
    Launch(app);
}

void AppsGrid::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const tokens::Theme theme = ui::SystemTheme();
    const tokens::Palette& p = tokens::PaletteFor(theme);
    float s = ui::Scale(dpi_);
    float w = static_cast<float>(host_.Width()) / s, h = static_cast<float>(host_.Height()) / s;
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(ui::Rgba(p.surface, open_), &brush);
    dc->FillRectangle(D2D1::RectF(0, 0, w, h), brush.Get());

    // Search field.
    float sw = std::min(360.0f, w - 64), sx = (w - sw) / 2, sy = 52;
    D2D1_ROUNDED_RECT field = D2D1::RoundedRect(D2D1::RectF(sx, sy, sx + sw, sy + 40), 10, 10);
    brush->SetColor(ui::Rgba(p.separator, open_));
    dc->FillRoundedRectangle(field, brush.Get());
    bool empty = query_.empty();
    brush->SetColor(ui::Rgba(empty ? p.textSecondary : p.text, open_));
    ui::DrawLabel(dc, brush.Get(), empty ? L"Search apps" : query_, D2D1::RectF(sx + 14, sy, sx + sw - 14, sy + 40), ui::Format(15));

    if (apps_.empty()) {
        ui::DrawLabel(dc, brush.Get(), L"Indexing apps...", D2D1::RectF(0, h / 2 - 20, w, h / 2 + 20), ui::Format(15),
                      DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (shown_.empty()) {
        ui::DrawLabel(dc, brush.Get(), L"No apps match", D2D1::RectF(0, h / 2 - 20, w, h / 2 + 20), ui::Format(15),
                      DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    GridLayout g = ComputeGridLayout(w);
    float top = ViewTop();
    D2D1_RECT_F clip = D2D1::RectF(0, top, w, h);
    dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    float lift = (1.0f - open_) * 14.0f;  // tiles rise slightly while opening
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i) {
        int row = i / g.columns, col = i % g.columns;
        float x = g.left + col * g.tileW, y = top + row * g.tileH - scroll_ + lift;
        if (y + g.tileH < top || y > h) continue;
        const AppEntry& app = apps_[shown_[i]];
        if (i == selected_ || i == hover_) {
            brush->SetColor(ui::Rgba(p.separator, open_ * (i == selected_ ? 1.6f : 1.0f)));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 6, y + 4, x + g.tileW - 6, y + g.tileH - 4), 12, 12), brush.Get());
        }
        float ix = x + (g.tileW - kIconDip) / 2, iy = y + 14;
        D2D1_RECT_F ir = D2D1::RectF(ix, iy, ix + kIconDip, iy + kIconDip);
        ComPtr<ID2D1Bitmap> bmp;
        if (auto it = bitmaps_.find(app.icon); it != bitmaps_.end()) {
            bmp = it->second;
        } else if (auto px = ui::IconStore::Get().Find(app.icon)) {
            bmp = ui::MakeBitmap(dc, *px);
            bitmaps_[app.icon] = bmp;
        } else {
            ui::IconStore::Get().Request(app.icon);
        }
        if (bmp) {
            dc->DrawBitmap(bmp.Get(), ir, open_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            brush->SetColor(ui::Rgba(p.separator, open_));  // placeholder until the icon arrives
            dc->FillRoundedRectangle(D2D1::RoundedRect(ir, 12, 12), brush.Get());
        }
        brush->SetColor(ui::Rgba(p.text, open_));
        ui::DrawLabel(dc, brush.Get(), app.name, D2D1::RectF(x + 8, iy + kIconDip + 6, x + g.tileW - 8, iy + kIconDip + 30),
                      ui::Format(12), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    dc->PopAxisAlignedClip();
    if (!host_.EndDraw()) {
        log::Warn("apps grid: present failed; closing");
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK AppsGrid::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    }
    auto* self = reinterpret_cast<AppsGrid*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || self->hwnd_ != h) return DefWindowProcW(h, msg, wp, lp);
    return self->OnMessage(msg, wp, lp);
}

LRESULT AppsGrid::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    float s = ui::Scale(dpi_);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Draw();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == kTimerFrame) Tick();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) Hide();  // clicked elsewhere or switched away
            return 0;
        case WM_CLOSE:
            Hide();
            return 0;
        case WM_CHAR:
            if (wp == 8) {
                if (!query_.empty()) query_.pop_back();
            } else if (wp >= 32 && wp != 127) {
                query_.push_back(static_cast<wchar_t>(wp));
            } else {
                return 0;
            }
            Refilter();
            Draw();
            return 0;
        case WM_KEYDOWN: {
            float w = static_cast<float>(host_.Width()) / s;
            int cols = ComputeGridLayout(w).columns, n = static_cast<int>(shown_.size());
            int step = 0;
            switch (wp) {
                case VK_ESCAPE:
                    if (query_.empty()) {
                        Hide();
                    } else {
                        query_.clear();
                        Refilter();
                        Draw();
                    }
                    return 0;
                case VK_RETURN: LaunchSelected(); return 0;
                case VK_LEFT: step = -1; break;
                case VK_RIGHT: step = 1; break;
                case VK_UP: step = -cols; break;
                case VK_DOWN: step = cols; break;
                default: return 0;
            }
            if (n > 0) {
                selected_ = std::clamp(selected_ + step, 0, n - 1);
                EnsureSelectedVisible();
                Draw();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            scroll_ = std::clamp(scroll_ - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 60.0f, 0.0f, MaxScroll());
            Draw();
            return 0;
        }
        case WM_MOUSEMOVE: {
            int hit = HitTile(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (hit != hover_) {
                hover_ = hit;
                Draw();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int hit = HitTile(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (hit >= 0) {
                selected_ = hit;
                LaunchSelected();
            } else if (GET_Y_LPARAM(lp) / s > ViewTop()) {
                Hide();  // empty area dismisses
            }
            return 0;
        }
        case WM_RBUTTONUP:
            Hide();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::apps
