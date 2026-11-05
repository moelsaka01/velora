#include "apps/apps_grid.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "ui/backdrop.h"
#include "ui/ui_common.h"

namespace velora::apps {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"VeloraAppsGrid";
constexpr UINT_PTR kTimerFrame = 1;
namespace ap = velora::tokens::appspanel;
constexpr float kIconDip = ap::kIcon;
constexpr int kBackdropMaxPx = 640;  // longest side of the blurred backdrop bitmap: a quarter-size image blurs the same and costs ~2 MB
constexpr float kSearchTop = 24.0f, kSearchHeight = 40.0f, kGridGap = 16.0f, kBottomInset = 12.0f;

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
    g.tileW = ap::kColumnPitch;
    g.tileH = ap::kRowPitch;
    float usable = std::max(0.0f, viewWidth - 2 * ap::kInset);
    g.columns = std::clamp(static_cast<int>(usable / g.tileW), 3, 9);
    float used = g.columns * g.tileW;
    g.left = std::max(0.0f, (viewWidth - used) / 2);
    return g;
}

PanelGeometry ComputePanel(float viewWidth, float viewHeight) {
    PanelGeometry p;
    p.width = std::min(ap::kWidth, std::max(320.0f, viewWidth - 64.0f));
    p.left = (viewWidth - p.width) / 2;
    p.top = std::clamp(viewHeight * 0.17f, 32.0f, 200.0f);
    constexpr float kDockRoom = 110.0f;  // the Dock sits below the panel
    p.height = std::max(240.0f, std::min(std::max(380.0f, viewHeight * 0.66f), viewHeight - p.top - kDockRoom));
    return p;
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
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, ui::SystemTheme());  // the panel draws its own material
    render::ApplyRoundedCorners(hwnd_, false);

    apps_ = AppIndex::Get().SnapshotForLauncher();
    query_.clear();
    selected_ = 0;
    hover_ = -1;
    scroll_ = 0;
    Refilter();
    listener_ = ui::IconStore::Get().AddListener([this] { OnDataChanged(); });

    closing_ = closeDone_ = wallTried_ = false;  // the backdrop is rebuilt on every open (the wallpaper may have changed)
    StartOpen();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    Tick();
}

void AppsGrid::OnSpring(float v, bool done) {
    open_ = std::clamp(v, 0.0f, 1.0f);
    if (done && closing_) closeDone_ = true;  // Hide() runs after Tick(), never from inside the engine's callback
}

void AppsGrid::StartOpen() {
    open_ = 0;
    spring_ = anim_.StartSpring(0, 1, tokens::timing::kPanelOpen, [this](float v, bool done) { OnSpring(v, done); }, NowMs());
}

void AppsGrid::Close() {
    if (!hwnd_ || closing_) return;
    closing_ = true;
    if (!anim_.Enabled()) {
        Hide();
        return;
    }
    // Reverse the running spring, or start a new one from where the panel is when the open spring has already settled.
    if (!anim_.Retarget(spring_, 0))
        spring_ = anim_.StartSpring(open_, 0, tokens::timing::kPanelOpen, [this](float v, bool done) { OnSpring(v, done); }, NowMs());
    Tick();
}

void AppsGrid::Hide() {
    if (!hwnd_) return;
    ui::IconStore::Get().RemoveListener(listener_);
    anim_.Cancel(spring_);  // a spring still running (closed mid-open) must not drive the next open
    HWND h = hwnd_;
    hwnd_ = nullptr;  // before DestroyWindow: WM_ACTIVATE during teardown must not re-enter
    if (timerOn_) KillTimer(h, kTimerFrame);
    timerOn_ = false;
    bitmaps_.clear();  // D2D bitmaps die before the device
    wall_.Reset();
    host_.Shutdown();
    DestroyWindow(h);
    apps_.clear();
    shown_.clear();
    apps_.shrink_to_fit();
    shown_.shrink_to_fit();
    if (onHidden_) onHidden_();
}

void AppsGrid::OnDataChanged() {
    if (!hwnd_) return;
    auto fresh = AppIndex::Get().SnapshotForLauncher();
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

PanelGeometry AppsGrid::Panel() const {
    const float s = ui::Scale(dpi_);
    return ComputePanel(static_cast<float>(host_.Width()) / s, static_cast<float>(host_.Height()) / s);
}

float AppsGrid::ViewTop() const {
    PanelGeometry g = Panel();
    return g.top + kSearchTop + kSearchHeight + kGridGap;
}

float AppsGrid::ViewBottom() const {
    PanelGeometry g = Panel();
    return g.top + g.height - kBottomInset;
}

float AppsGrid::MaxScroll() const {
    if (!hwnd_) return 0;
    GridLayout g = ComputeGridLayout(Panel().width);
    int rows = (static_cast<int>(shown_.size()) + g.columns - 1) / g.columns;
    return std::max(0.0f, rows * g.tileH - (ViewBottom() - ViewTop()));
}

void AppsGrid::EnsureSelectedVisible() {
    if (selected_ < 0) return;
    GridLayout g = ComputeGridLayout(Panel().width);
    scroll_ = ScrollToRow(scroll_, selected_ / g.columns, g.tileH, ViewBottom() - ViewTop());
    scroll_ = std::clamp(scroll_, 0.0f, MaxScroll());
}

int AppsGrid::HitTile(float x, float y) const {
    PanelGeometry pg = Panel();
    GridLayout g = ComputeGridLayout(pg.width);
    if (y < ViewTop() || y >= ViewBottom() || x < pg.left || x >= pg.left + pg.width) return -1;
    float gx = x - pg.left - g.left, gy = y - ViewTop() + scroll_;
    if (gx < 0 || gy < 0) return -1;
    int col = static_cast<int>(gx / g.tileW), row = static_cast<int>(gy / g.tileH);
    if (col >= g.columns) return -1;
    int idx = row * g.columns + col;
    return idx < static_cast<int>(shown_.size()) ? idx : -1;
}

void AppsGrid::Tick() {
    if (!hwnd_) return;
    bool more = anim_.Tick(NowMs());
    if (closeDone_) {  // the closing spring settled
        Hide();
        return;
    }
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

// Reads the wallpaper behind the panel, blurs it (at reduced size) and keeps it as a bitmap. Null, and a stronger veil,
// when transparency is off or the wallpaper is a colour, a slideshow or unreadable.
void AppsGrid::BuildBackdrop(ID2D1DeviceContext* dc) {
    wallTried_ = true;
    wall_.Reset();
    if (!hwnd_ || !dc || !ui::TransparencyEnabled()) return;
    const std::uint64_t t0 = NowMs();
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const float s = ui::Scale(dpi_);
    const PanelGeometry g = Panel();
    const RECT region{win.left + static_cast<LONG>(std::lround(g.left * s)), win.top + static_cast<LONG>(std::lround(g.top * s)),
                      win.left + static_cast<LONG>(std::lround((g.left + g.width) * s)),
                      win.top + static_cast<LONG>(std::lround((g.top + g.height) * s))};
    const int longest = static_cast<int>(std::max(region.right - region.left, region.bottom - region.top));
    const int shrink = std::max(1, (longest + kBackdropMaxPx - 1) / kBackdropMaxPx);
    ui::Pixels px;
    if (!ui::BlurredWallpaper(mi.rcMonitor, region, shrink, ap::kBlurSigma, dpi_, &px)) {
        log::Info("apps grid: no wallpaper image behind the panel; stronger veil instead of a blurred backdrop");
        return;
    }
    wall_ = ui::MakeBitmap(dc, px);
    log::Info("apps grid: backdrop " + std::to_string(px.width) + "x" + std::to_string(px.height) + " px (1/" + std::to_string(shrink) +
              " size), " + std::to_string(NowMs() - t0) + " ms");
}

void AppsGrid::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const tokens::Palette& p = tokens::PaletteFor(ui::SystemTheme());
    (void)p;
    float s = ui::Scale(dpi_);
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    if (!wallTried_) BuildBackdrop(dc);
    const float t = open_;
    const PanelGeometry pg = Panel();
    const D2D1_RECT_F panelRect = D2D1::RectF(pg.left, pg.top, pg.left + pg.width, pg.top + pg.height);
    const float radius = std::min(ap::kRadius, std::min(pg.width, pg.height) / 2);
    const float scale = ap::kOpenFrom + (1.0f - ap::kOpenFrom) * t;
    dc->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(pg.left + pg.width / 2, pg.top + pg.height / 2)));

    // The text colours are fixed: the glass is dark-tinted in light and dark mode (docs/MACOS_UI_SPEC.md).
    const D2D1_COLOR_F white = D2D1::ColorF(1, 1, 1, 0.96f * t), whiteDim = D2D1::ColorF(1, 1, 1, 0.62f * t);
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &brush);

    // Soft shadow: nested rounded rectangles that fade with distance.
    {
        constexpr int kLayers = 8;
        const float a = 1.0f - std::pow(1.0f - 0.30f * t, 1.0f / kLayers);
        for (int k = kLayers; k >= 1; --k) {
            const float grow = 18.0f * k / kLayers;
            brush->SetColor(D2D1::ColorF(0, 0, 0, a));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panelRect.left - grow, panelRect.top - grow + 8, panelRect.right + grow,
                                                                    panelRect.bottom + grow + 8),
                                                       radius + grow, radius + grow),
                                     brush.Get());
        }
    }
    const D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(panelRect, radius, radius);
    bool blurred = false;
    if (wall_) {
        ComPtr<ID2D1BitmapBrush> bb;
        if (SUCCEEDED(dc->CreateBitmapBrush(wall_.Get(), &bb))) {
            D2D1_SIZE_F bs = wall_->GetSize();  // DIPs at the bitmap's own DPI (96): one bitmap pixel is one unit
            D2D1_SIZE_U bp = wall_->GetPixelSize();
            (void)bs;
            bb->SetTransform(D2D1::Matrix3x2F::Scale(pg.width / static_cast<float>(bp.width), pg.height / static_cast<float>(bp.height)) *
                             D2D1::Matrix3x2F::Translation(pg.left, pg.top));
            bb->SetOpacity(t);
            dc->FillRoundedRectangle(panel, bb.Get());
            blurred = true;
        }
    }
    brush->SetColor(ui::Rgba({blurred ? ap::kVeil : ap::kVeilUnblurred}, t));
    dc->FillRoundedRectangle(panel, brush.Get());
    brush->SetColor(D2D1::ColorF(1, 1, 1, 0.16f * t));  // 1 DIP inner rim
    dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panelRect.left + 0.5f, panelRect.top + 0.5f, panelRect.right - 0.5f, panelRect.bottom - 0.5f),
                                               radius - 0.5f, radius - 0.5f),
                             brush.Get(), 1.0f);

    // Search field.
    const float sx = pg.left + 24, sy = pg.top + kSearchTop, sw = pg.width - 48;
    brush->SetColor(D2D1::ColorF(1, 1, 1, 0.14f * t));
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx, sy, sx + sw, sy + kSearchHeight), kSearchHeight / 2, kSearchHeight / 2), brush.Get());
    bool empty = query_.empty();
    brush->SetColor(empty ? whiteDim : white);
    ui::DrawLabel(dc, brush.Get(), empty ? L"Search apps" : query_, D2D1::RectF(sx + 20, sy, sx + sw - 20, sy + kSearchHeight), ui::Format(15));

    const float top = ViewTop(), bottom = ViewBottom();
    if (apps_.empty() || shown_.empty()) {
        brush->SetColor(whiteDim);
        ui::DrawLabel(dc, brush.Get(), apps_.empty() ? L"Indexing apps..." : L"No apps match",
                      D2D1::RectF(pg.left, top, pg.left + pg.width, bottom), ui::Format(15), DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    GridLayout g = ComputeGridLayout(pg.width);
    dc->PushAxisAlignedClip(D2D1::RectF(pg.left, top, pg.left + pg.width, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = 0; i < static_cast<int>(shown_.size()); ++i) {
        int row = i / g.columns, col = i % g.columns;
        float x = pg.left + g.left + col * g.tileW, y = top + row * g.tileH - scroll_;
        if (y + g.tileH < top || y > bottom) continue;
        const AppEntry& app = apps_[shown_[i]];
        if (i == selected_ || i == hover_) {
            brush->SetColor(D2D1::ColorF(1, 1, 1, (i == selected_ ? 0.22f : 0.14f) * t));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 4, y + 2, x + g.tileW - 4, y + g.tileH - 2), 12, 12), brush.Get());
        }
        float ix = x + (g.tileW - kIconDip) / 2, iy = y + 8;
        D2D1_RECT_F ir = D2D1::RectF(ix, iy, ix + kIconDip, iy + kIconDip);
        ComPtr<ID2D1Bitmap> bmp;
        const int tier = ui::IconTierFor(kIconDip * s);  // decoded at the device size: no upscaling
        if (auto it = bitmaps_.find(app.icon); it != bitmaps_.end()) {
            bmp = it->second;
        } else if (auto px = ui::IconStore::Get().Find(app.icon, tier)) {
            bmp = ui::MakeBitmap(dc, *px);
            bitmaps_[app.icon] = bmp;
        } else {
            ui::IconStore::Get().Request(app.icon, tier);
        }
        if (bmp) {
            ui::DrawIcon(dc, bmp.Get(), ir, t);
        } else if (ui::IconStore::Get().Failed(app.icon, tier)) {
            ui::DrawFallbackTile(dc, ir, app.name, t);  // no icon exists anywhere (not even a generic one)
        } else {
            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.14f * t));  // placeholder until the icon arrives
            dc->FillRoundedRectangle(D2D1::RoundedRect(ir, kIconDip * tokens::dock::kIconRadiusFraction, kIconDip * tokens::dock::kIconRadiusFraction),
                                     brush.Get());
        }
        brush->SetColor(white);
        ui::DrawLabel(dc, brush.Get(), app.name, D2D1::RectF(x + 6, iy + kIconDip + 4, x + g.tileW - 6, iy + kIconDip + 26),
                      ui::Format(12), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    dc->PopAxisAlignedClip();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
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
            int cols = ComputeGridLayout(Panel().width).columns, n = static_cast<int>(shown_.size());
            int step = 0;
            switch (wp) {
                case VK_ESCAPE:
                    if (query_.empty()) {
                        Close();
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
            } else {
                const PanelGeometry pg = Panel();
                const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
                if (x < pg.left || x >= pg.left + pg.width || y < pg.top || y >= pg.top + pg.height) Close();  // outside the panel dismisses
            }
            return 0;
        }
        case WM_RBUTTONUP:
            Close();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::apps
