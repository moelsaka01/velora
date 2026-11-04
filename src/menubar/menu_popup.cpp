#include "menubar/menu_popup.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "ui/backdrop.h"
#include "ui/ui_common.h"

namespace velora::menubar {

using Microsoft::WRL::ComPtr;
namespace tm = velora::tokens::menu;

namespace {

constexpr wchar_t kClass[] = L"VeloraMenuPopup";

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

MenuPopup::MenuPopup(HINSTANCE instance) : instance_(instance) {}

MenuPopup::~MenuPopup() {
    onClosed_ = nullptr;
    onChoose_ = nullptr;
    Close();
}

bool MenuPopup::Open(HWND owner, int x, int y, std::vector<MenuItem> items, std::function<void(unsigned)> onChoose,
                     std::function<void()> onClosed) {
    if (hwnd_ || !RegisterOnce(instance_, &MenuPopup::WndProc)) return false;
    items_ = std::move(items);
    layout_ = ComputeMenuLayout(items_);
    // App menus have long labels and shortcuts: widen the popup to fit (never narrower than the measured 201.5 pt).
    {
        anyChecked_ = false;
        IDWriteTextFormat* f = ui::Format(tm::kFontSize);
        float need = layout_.width;
        for (const MenuItem& it : items_) {
            anyChecked_ = anyChecked_ || it.checked;
            if (it.separator) continue;
            float w = 2 * tm::kPopupPadding + tm::kRowTextInset + ui::MeasureWidth(it.label, f) + 18;
            if (!it.shortcut.empty()) w += 28 + ui::MeasureWidth(it.shortcut, f);
            if (it.submenu) w += 14;
            need = std::max(need, w);
        }
        if (anyChecked_) need += 14;
        layout_.width = std::min(need, 460.0f);
    }
    onChoose_ = std::move(onChoose);
    onClosed_ = std::move(onClosed);
    selected_ = pressed_ = -1;

    dpi_ = owner ? GetDpiForWindow(owner) : 96;
    const float s = ui::Scale(dpi_);
    const int w = static_cast<int>(std::lround(layout_.width * s)), h = static_cast<int>(std::lround(layout_.height * s));
    HMONITOR mon = MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    x = static_cast<int>(PopupLeft(static_cast<float>(x), static_cast<float>(w), static_cast<float>(mi.rcWork.left),
                                   static_cast<float>(mi.rcWork.right)));
    y = std::min(y, static_cast<int>(mi.rcWork.bottom) - h);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Velora Menu", WS_POPUP, x, y, w,
                            h, owner, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("menu popup: CreateWindowEx failed");
        return false;
    }
    if (!host_.Initialize(hwnd_)) {
        log::Warn("menu popup: render host unavailable");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    const tokens::Theme theme = ui::SystemTheme();
    // Glass drawn like the Dock's shelf: blurred wallpaper + neutral veil + rim, clipped to our own rounded rectangle
    // (the window is per-pixel transparent), so the corner radius is the spec's and not the 8 px system one.
    translucent_ = ui::TransparencyEnabled() && host_.EffectsEnabled();
    wall_.Reset();
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, theme);
    render::ApplyRoundedCorners(hwnd_, false);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetForegroundWindow(hwnd_);
    Draw();
    return true;
}

void MenuPopup::Close() {
    if (!hwnd_) return;
    HWND h = hwnd_;
    hwnd_ = nullptr;  // before DestroyWindow: WM_ACTIVATE during teardown must not re-enter
    host_.Shutdown();
    DestroyWindow(h);
    items_.clear();
    onHorizontal_ = nullptr;
    auto closed = std::move(onClosed_);
    onClosed_ = nullptr;
    onChoose_ = nullptr;
    if (closed) closed();
}

void MenuPopup::Choose(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    unsigned id = items_[index].id;
    auto choose = std::move(onChoose_);
    Close();  // the popup is gone before the command runs (a command may open a modal dialog)
    if (choose) choose(id);
}

void MenuPopup::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const tokens::Theme theme = ui::SystemTheme();
    const tokens::Palette& p = tokens::PaletteFor(theme);
    const float s = ui::Scale(dpi_);
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    const float w = static_cast<float>(host_.Width()) / s, h = static_cast<float>(host_.Height()) / s;
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(ui::Rgba({theme == tokens::Theme::Dark ? tm::kPopupFillDark : tm::kPopupFillLight}), &brush);
    const D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), tm::kPopupRadius, tm::kPopupRadius);
    if (translucent_) {
        if (!wall_) {
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
        // Menu fill over the blur (the spec's ~77 % white-ish / dark estimate); stronger when there is nothing to blur.
        if (!blurred) brush->SetColor(ui::Rgba(p.surfaceStrong));  // no wallpaper image: opaque instead of a see-through veil
        dc->FillRoundedRectangle(body, brush.Get());
        // Light rim, as on the Dock shelf.
        const tokens::Glass& glass = tokens::GlassFor(theme);
        brush->SetColor(ui::Rgba(glass.rimTop));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), tm::kPopupRadius, tm::kPopupRadius),
                                 brush.Get(), 1.0f);
    } else {
        brush->SetColor(ui::Rgba(p.surfaceStrong));  // transparency effects off / gaming mode: opaque
        dc->FillRoundedRectangle(body, brush.Get());
    }

    IDWriteTextFormat* fmt = ui::Format(tm::kFontSize);
    for (size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& it = items_[i];
        const float top = layout_.top[i], bottom = layout_.bottom[i];
        if (it.separator) {
            const float y = (top + bottom) / 2;
            brush->SetColor(ui::Rgba(p.separator));
            dc->FillRectangle(D2D1::RectF(tm::kPopupPadding + 5, y - 0.5f, w - tm::kPopupPadding - 5, y + 0.5f), brush.Get());
            continue;
        }
        const bool hot = static_cast<int>(i) == selected_ && it.enabled;
        D2D1_RECT_F row = D2D1::RectF(tm::kPopupPadding, top, w - tm::kPopupPadding, bottom);
        if (hot) {
            brush->SetColor(ui::Rgba(p.accent));
            dc->FillRoundedRectangle(D2D1::RoundedRect(row, tm::kHighlightRadius, tm::kHighlightRadius), brush.Get());
        }
        brush->SetColor(hot ? D2D1::ColorF(1, 1, 1, 1) : ui::Rgba(it.enabled ? p.text : p.textSecondary));
        const float mid = (top + bottom) / 2;
        float right = row.right - 6;
        if (it.submenu) {  // arrow
            dc->DrawLine(D2D1::Point2F(row.right - 15, mid - 4), D2D1::Point2F(row.right - 11, mid), brush.Get(), 1.4f);
            dc->DrawLine(D2D1::Point2F(row.right - 11, mid), D2D1::Point2F(row.right - 15, mid + 4), brush.Get(), 1.4f);
            right -= 14;
        }
        if (!it.shortcut.empty()) {
            const float sw = ui::MeasureWidth(it.shortcut, fmt);
            brush->SetColor(hot ? D2D1::ColorF(1, 1, 1, 0.85f) : ui::Rgba(p.textSecondary));
            ui::DrawLabel(dc, brush.Get(), it.shortcut, D2D1::RectF(right - sw - 8, top, right, bottom), fmt, DWRITE_TEXT_ALIGNMENT_TRAILING);
            right -= sw + 20;
            brush->SetColor(hot ? D2D1::ColorF(1, 1, 1, 1) : ui::Rgba(it.enabled ? p.text : p.textSecondary));
        }
        if (it.checked) {  // tick
            dc->DrawLine(D2D1::Point2F(row.left + 9, mid), D2D1::Point2F(row.left + 12.5f, mid + 3.5f), brush.Get(), 1.6f);
            dc->DrawLine(D2D1::Point2F(row.left + 12.5f, mid + 3.5f), D2D1::Point2F(row.left + 18, mid - 4), brush.Get(), 1.6f);
        }
        ui::DrawLabel(dc, brush.Get(), it.label, D2D1::RectF(row.left + tm::kRowTextInset + (anyChecked_ ? 14.0f : 0.0f), top, right, bottom), fmt);
    }
    if (!host_.EndDraw()) {
        log::Warn("menu popup: present failed; closing");
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK MenuPopup::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<MenuPopup*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || self->hwnd_ != h) return DefWindowProcW(h, msg, wp, lp);
    return self->OnMessage(msg, wp, lp);
}

LRESULT MenuPopup::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const float s = ui::Scale(dpi_);
    auto hit = [&] { return HitTestMenu(layout_, items_, GET_Y_LPARAM(lp) / s); };
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
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) Close();  // clicked elsewhere or switched away
            return 0;
        case WM_CLOSE:
            Close();
            return 0;
        case WM_MOUSEMOVE: {
            int i = hit();
            if (i != selected_) {
                selected_ = i;
                Draw();
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&t);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            selected_ = -1;
            Draw();
            return 0;
        case WM_LBUTTONDOWN:
            pressed_ = hit();
            return 0;
        case WM_LBUTTONUP: {
            int i = hit();
            if (i >= 0 && i == pressed_) Choose(i);
            pressed_ = -1;
            return 0;
        }
        case WM_KEYDOWN:
            switch (wp) {
                case VK_ESCAPE: Close(); return 0;
                case VK_RETURN: Choose(selected_); return 0;
                case VK_RIGHT:
                    if (selected_ >= 0 && items_[selected_].submenu) Choose(selected_);
                    else if (onHorizontal_) {
                        auto cb = onHorizontal_;
                        Close();
                        cb(1);
                    }
                    return 0;
                case VK_LEFT:
                    if (onHorizontal_) {
                        auto cb = onHorizontal_;
                        Close();
                        cb(-1);
                    }
                    return 0;
                case VK_DOWN: selected_ = NextSelectable(items_, selected_, 1); break;
                case VK_UP: selected_ = NextSelectable(items_, selected_, -1); break;
                case VK_HOME: selected_ = NextSelectable(items_, -1, 1); break;
                case VK_END: selected_ = NextSelectable(items_, -1, -1); break;
                default: return 0;
            }
            Draw();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::menubar
