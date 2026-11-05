#include "overview/overview.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "render/tokens.h"
#include "ui/backdrop.h"
#include "ui/ui_common.h"

namespace velora::overview {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"VeloraOverview";
constexpr float kTitleSize = 13.0f;
constexpr int kBackdropMaxPx = 640;

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

BOOL CALLBACK CollectZOrder(HWND h, LPARAM lp) {
    reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
    return TRUE;
}

}  // namespace

Overview::Overview(HINSTANCE instance, win::WindowTracker& tracker) : instance_(instance), tracker_(tracker) {}

Overview::~Overview() { Hide(); }

float Overview::Scale() const { return ui::Scale(dpi_); }

void Overview::Collect(HMONITOR monitor) {
    items_.clear();
    std::vector<HWND> ordered;
    if (!override_.empty()) {
        ordered = override_;
    } else {
        std::vector<HWND> z;
        EnumWindows(&CollectZOrder, reinterpret_cast<LPARAM>(&z));  // top of the stack first
        for (HWND h : z) {
            const win::TrackedWindow* w = tracker_.Find(h);
            if (!w || w->minimized || w->cloaked) continue;
            if (MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) != monitor) continue;
            ordered.push_back(h);
        }
    }
    for (HWND h : ordered) {
        Item it;
        it.hwnd = h;
        if (const win::TrackedWindow* w = tracker_.Find(h)) it.title = w->title;
        if (it.title.empty()) {
            wchar_t buf[256] = {};
            GetWindowTextW(h, buf, static_cast<int>(std::size(buf)));
            it.title = buf;
        }
        items_.push_back(std::move(it));
    }
}

void Overview::Show() {
    if (hwnd_) return;
    if (!RegisterOnce(instance_, &Overview::WndProc)) return;
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    Collect(mon);
    // Over the work area: the menu bar and the Dock (appbars) stay visible.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Velora Overview", WS_POPUP, mi.rcWork.left,
                            mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("overview: CreateWindowEx failed");
        items_.clear();
        return;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    if (!host_.Initialize(hwnd_)) {
        log::Warn("overview: render host unavailable");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        items_.clear();
        return;
    }
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, ui::SystemTheme());
    render::ApplyRoundedCorners(hwnd_, false);
    hover_ = selected_ = -1;
    wallTried_ = false;
    // Thumbnails: registered once, positioned from the layout.
    for (Item& it : items_) {
        if (FAILED(DwmRegisterThumbnail(hwnd_, it.hwnd, &it.thumb))) {
            it.thumb = nullptr;
            it.placeholder = true;
        }
    }
    Relayout();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    Draw();
    log::Info("overview: " + std::to_string(items_.size()) + " window(s)");
}

void Overview::ReleaseThumbnails() {
    for (Item& it : items_) {
        if (it.thumb) DwmUnregisterThumbnail(it.thumb);
        it.thumb = nullptr;
    }
}

void Overview::Hide() {
    if (!hwnd_) return;
    HWND h = hwnd_;
    hwnd_ = nullptr;  // before DestroyWindow: WM_ACTIVATE during teardown must not re-enter
    ReleaseThumbnails();
    items_.clear();
    wall_.Reset();
    host_.Shutdown();
    DestroyWindow(h);
    if (onHidden_) onHidden_();
}

bool Overview::Activate(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return false;
    HWND target = items_[index].hwnd;
    Hide();
    if (!IsWindow(target)) return false;
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
    return true;
}

void Overview::Relayout() {
    if (!hwnd_ || !host_.Ready()) return;
    const float s = Scale();
    std::vector<Size> src;
    for (Item& it : items_) {
        SIZE px{};
        if (it.thumb && SUCCEEDED(DwmQueryThumbnailSourceSize(it.thumb, &px)) && px.cx > 0 && px.cy > 0) {
            it.src = {static_cast<float>(px.cx) / s, static_cast<float>(px.cy) / s};
        } else {
            RECT r{};
            GetWindowRect(it.hwnd, &r);
            it.src = {std::max(1.0f, static_cast<float>(r.right - r.left) / s), std::max(1.0f, static_cast<float>(r.bottom - r.top) / s)};
        }
        src.push_back(it.src);
    }
    LayoutParams p;
    p.areaW = static_cast<float>(host_.Width()) / s;
    p.areaH = static_cast<float>(host_.Height()) / s;
    const auto cells = ComputeLayout(src, p);
    for (size_t i = 0; i < items_.size(); ++i) items_[i].cell = cells[i];
    UpdateThumbnails();
}

void Overview::UpdateThumbnails() {
    const float s = Scale();
    for (const Item& it : items_) {
        if (!it.thumb) continue;
        DWM_THUMBNAIL_PROPERTIES props{};
        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
        props.rcDestination = RECT{static_cast<LONG>(std::lround(it.cell.x * s)), static_cast<LONG>(std::lround(it.cell.y * s)),
                                   static_cast<LONG>(std::lround((it.cell.x + it.cell.w) * s)), static_cast<LONG>(std::lround((it.cell.y + it.cell.h) * s))};
        props.fVisible = TRUE;
        props.opacity = 255;
        DwmUpdateThumbnailProperties(it.thumb, &props);
    }
}

void Overview::BuildBackdrop(ID2D1DeviceContext* dc) {
    wallTried_ = true;
    wall_.Reset();
    if (!hwnd_ || !dc || !ui::TransparencyEnabled()) return;
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const int longest = std::max<int>(win.right - win.left, win.bottom - win.top);
    const int shrink = std::max(1, (longest + kBackdropMaxPx - 1) / kBackdropMaxPx);
    ui::Pixels px;
    if (!ui::BlurredWallpaper(mi.rcMonitor, win, shrink, 24.0f, dpi_, &px)) return;
    wall_ = ui::MakeBitmap(dc, px);
}

void Overview::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    if (!wallTried_) BuildBackdrop(dc);
    const float s = Scale();
    const float w = static_cast<float>(host_.Width()) / s, h = static_cast<float>(host_.Height()) / s;
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
    if (wall_) {
        ComPtr<ID2D1BitmapBrush> bb;
        if (SUCCEEDED(dc->CreateBitmapBrush(wall_.Get(), &bb))) {
            float bx = 96, by = 96;
            wall_->GetDpi(&bx, &by);
            const D2D1_SIZE_F sz = wall_->GetSize();
            bb->SetTransform(D2D1::Matrix3x2F::Scale(w / sz.width, h / sz.height));
            dc->FillRectangle(D2D1::RectF(0, 0, w, h), bb.Get());
        }
        b->SetColor(D2D1::ColorF(0.04f, 0.04f, 0.05f, 0.46f));
    } else {
        b->SetColor(D2D1::ColorF(0.08f, 0.08f, 0.09f, 0.94f));  // transparency off or no image: opaque dark
    }
    dc->FillRectangle(D2D1::RectF(0, 0, w, h), b.Get());

    IDWriteTextFormat* fmt = ui::Format(kTitleSize, DWRITE_FONT_WEIGHT_MEDIUM);
    if (items_.empty()) {
        b->SetColor(D2D1::ColorF(1, 1, 1, 0.8f));
        ui::DrawLabel(dc, b.Get(), L"No open windows", D2D1::RectF(0, h / 2 - 20, w, h / 2 + 20), ui::Format(20), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        const D2D1_RECT_F r = D2D1::RectF(it.cell.x, it.cell.y, it.cell.x + it.cell.w, it.cell.y + it.cell.h);
        const bool hot = static_cast<int>(i) == hover_ || static_cast<int>(i) == selected_;
        if (it.placeholder) {  // no thumbnail (access denied, protected content): a tile with the title
            b->SetColor(D2D1::ColorF(1, 1, 1, 0.14f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(r, 10, 10), b.Get());
        }
        if (hot) {
            const tokens::Palette& p = tokens::PaletteFor(ui::SystemTheme());
            b->SetColor(ui::Rgba(p.accent));
            dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - 5, r.top - 5, r.right + 5, r.bottom + 5), 12, 12), b.Get(), 3.0f);
        }
        b->SetColor(D2D1::ColorF(1, 1, 1, 0.92f));
        ui::DrawLabel(dc, b.Get(), it.title, D2D1::RectF(it.cell.x - 8, r.bottom + 10, r.right + 8, r.bottom + 10 + 20), fmt, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (!host_.EndDraw()) {
        log::Warn("overview: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        wallTried_ = false;
    }
}

LRESULT CALLBACK Overview::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<Overview*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || (!self->hwnd_ && msg != WM_NCCREATE && msg != WM_CREATE && msg != WM_NCCALCSIZE && msg != WM_GETMINMAXINFO)) return DefWindowProcW(h, msg, wp, lp);
    if (!self->hwnd_) self->hwnd_ = h;
    return self->OnMessage(msg, wp, lp);
}

LRESULT Overview::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const float s = Scale();
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Draw();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (host_.Ready() && wp != SIZE_MINIMIZED) {
                host_.Resize(LOWORD(lp), HIWORD(lp));
                wallTried_ = false;
                Relayout();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) Hide();  // switched away
            return 0;
        case WM_MOUSEMOVE: {
            std::vector<Cell> cells;
            for (const Item& it : items_) cells.push_back(it.cell);
            const int i = CellAt(cells, GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (i != hover_) {
                hover_ = i;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            std::vector<Cell> cells;
            for (const Item& it : items_) cells.push_back(it.cell);
            const int i = CellAt(cells, GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (i >= 0) Activate(i);
            else Hide();
            return 0;
        }
        case WM_RBUTTONUP:
            Hide();
            return 0;
        case WM_KEYDOWN: {
            std::vector<Cell> cells;
            for (const Item& it : items_) cells.push_back(it.cell);
            int dx = 0, dy = 0;
            switch (wp) {
                case VK_ESCAPE: Hide(); return 0;
                case VK_RETURN:
                    if (selected_ >= 0) Activate(selected_);
                    return 0;
                case VK_LEFT: dx = -1; break;
                case VK_RIGHT: case VK_TAB: dx = 1; break;
                case VK_UP: dy = -1; break;
                case VK_DOWN: dy = 1; break;
                default: return 0;
            }
            selected_ = Neighbour(cells, selected_ >= 0 ? selected_ : hover_, dx, dy);
            hover_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY: return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::overview
