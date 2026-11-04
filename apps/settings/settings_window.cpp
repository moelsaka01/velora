#include "settings/settings_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <thread>

#include "core/contracts/ipc.h"
#include "core/ipc_pipe.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"
#include "render/tokens.h"
#include "ui/ui_common.h"
#include "ui/window_chrome.h"

namespace velora::settings {

namespace tw = tokens::window;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"VeloraSettingsWindow";
constexpr float kResizeBorder = 6.0f;
constexpr float kSidebarW = tw::kSidebarWidth, kToolbarH = tw::kToolbarHeight, kSidebarRow = tw::kSidebarRowPitch;
constexpr float kPad = 32.0f, kRowH = 58.0f, kNoteH = 44.0f, kEntryH = 34.0f, kAddH = 44.0f, kSwitchW = 42.0f, kSwitchH = 24.0f;
constexpr float kPillW = 168.0f, kPillH = 28.0f, kButtonW = 168.0f;
constexpr float kTitleY = 60.0f, kRowsTop = 112.0f;
constexpr UINT kTimerCaret = 1;

struct Colors {
    D2D1_COLOR_F content, sidebar, text, dim, line, accent, hover, onAccent, control, controlOff;
};

Colors ColorsFor(tokens::Theme t) {
    const tokens::Palette& p = tokens::PaletteFor(t);
    const bool dark = t == tokens::Theme::Dark;
    Colors c;
    c.content = ui::Rgba({dark ? tw::kContentDark : tw::kContentLight});
    c.sidebar = ui::Rgba({dark ? tw::kSidebarDark : tw::kSidebarLight});
    c.text = ui::Rgba(p.text);
    c.dim = ui::Rgba(p.textSecondary);
    c.line = dark ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(0, 0, 0, 0.10f);
    c.accent = ui::Rgba(p.accent);
    c.hover = dark ? D2D1::ColorF(1, 1, 1, 0.07f) : D2D1::ColorF(0, 0, 0, 0.05f);
    c.onAccent = D2D1::ColorF(1, 1, 1, 1);
    c.control = dark ? D2D1::ColorF(1, 1, 1, 0.12f) : D2D1::ColorF(0, 0, 0, 0.07f);
    c.controlOff = dark ? D2D1::ColorF(1, 1, 1, 0.22f) : D2D1::ColorF(0, 0, 0, 0.18f);
    return c;
}

bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

std::filesystem::path ExePath(const wchar_t* name) { return ExeDir() / name; }

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance) : instance_(instance), popup_(instance) {}

SettingsWindow::~SettingsWindow() {
    popup_.Close();
    if (hwnd_) DestroyWindow(hwnd_);
}

float SettingsWindow::Scale() const { return ui::Scale(dpi_); }

bool SettingsWindow::Create(const std::filesystem::path& file, int page, int showCmd) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = &SettingsWindow::WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    store_ = Store(file);
    if (!store_.Load()) {
        status_ = L"settings.json could not be read; showing defaults. Your next change replaces it.";
        log::Warn("settings: settings.json is not valid JSON; defaults shown");
    }
    pages_ = BuildPages();
    page_ = std::clamp(page, 0, static_cast<int>(pages_.size()) - 1);

    const UINT sysDpi = GetDpiForSystem();
    const int w = static_cast<int>(820 * sysDpi / 96.0f), h = static_cast<int>(560 * sysDpi / 96.0f);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClass, L"Velora Settings", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("settings: CreateWindowEx failed");
        return false;
    }
    ShowWindow(hwnd_, showCmd);
    UpdateWindow(hwnd_);
    return true;
}

void SettingsWindow::ShowPage(int page) {
    page_ = std::clamp(page, 0, static_cast<int>(pages_.size()) - 1);
    scroll_ = 0;
    focus_ = hover_ = -1;
    fieldFocus_ = false;
    popup_.Close();
    Invalidate();
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (!self->hwnd_) self->hwnd_ = h;
    return self->OnMessage(msg, wp, lp);
}

// ---- layout ---------------------------------------------------------------------------------------------------------------

std::vector<SettingsWindow::Hit> SettingsWindow::Hits() {
    Layout();
    return hits_;
}

void SettingsWindow::Layout() {
    hits_.clear();
    const float s = Scale();
    const float width = host_.Ready() ? static_cast<float>(host_.Width()) / s : 820.0f;
    // Traffic lights and page list (fixed).
    for (int i = 0; i < 3; ++i) {
        const float cx = tw::kTrafficLeft + tw::kTrafficDiameter / 2 + i * tw::kTrafficPitch;
        hits_.push_back({HitKind::Traffic, -1, i, D2D1::RectF(cx - 9, tw::kTrafficCenterY - 9, cx + 9, tw::kTrafficCenterY + 9)});
    }
    for (size_t i = 0; i < pages_.size(); ++i) {
        const float y = kToolbarH + static_cast<float>(i) * kSidebarRow;
        hits_.push_back({HitKind::Page, -1, static_cast<int>(i), D2D1::RectF(8, y, kSidebarW - 8, y + kSidebarRow)});
    }
    // Rows of the page.
    const float left = kSidebarW + kPad, right = width - kPad;
    float y = kRowsTop - scroll_;
    const Page& page = pages_[page_];
    for (size_t idx = 0; idx < page.items.size(); ++idx) {
        const Item& it = page.items[idx];
        const int ii = static_cast<int>(idx);
        switch (it.kind) {
            case ItemKind::Toggle:
                hits_.push_back({HitKind::Toggle, ii, -1, D2D1::RectF(right - kSwitchW, y + (kRowH - kSwitchH) / 2, right, y + (kRowH + kSwitchH) / 2)});
                y += kRowH;
                break;
            case ItemKind::Choice:
                hits_.push_back({HitKind::ChoiceButton, ii, -1, D2D1::RectF(right - kPillW, y + (kRowH - kPillH) / 2, right, y + (kRowH + kPillH) / 2)});
                y += kRowH;
                break;
            case ItemKind::Button:
                hits_.push_back({HitKind::ActionButton, ii, -1, D2D1::RectF(right - kButtonW, y + (kRowH - kPillH) / 2, right, y + (kRowH + kPillH) / 2)});
                y += kRowH;
                break;
            case ItemKind::Note:
                y += kNoteH;
                break;
            case ItemKind::List: {
                y += kRowH;
                const auto entries = store_.GetList(it.key);
                for (size_t e = 0; e < entries.size(); ++e) {
                    hits_.push_back({HitKind::ListRemove, ii, static_cast<int>(e), D2D1::RectF(right - 30, y + 4, right - 2, y + kEntryH - 4)});
                    y += kEntryH;
                }
                hits_.push_back({HitKind::ListField, ii, -1, D2D1::RectF(left, y + 6, right - kButtonW / 2 - 12, y + kAddH - 6)});
                hits_.push_back({HitKind::ListAdd, ii, -1, D2D1::RectF(right - kButtonW / 2, y + 6, right, y + kAddH - 6)});
                y += kAddH;
                break;
            }
        }
    }
    contentH_ = y + scroll_ - kRowsTop + 24;
}

int SettingsWindow::HitIndexAt(float x, float y) const {
    for (size_t i = 0; i < hits_.size(); ++i)
        if (In(hits_[i].rect, x, y)) return static_cast<int>(i);
    return -1;
}

// ---- actions --------------------------------------------------------------------------------------------------------------

void SettingsWindow::Changed() {
    if (!store_.Save()) {
        status_ = L"Could not save settings.json (is the folder writable?).";
        log::Error("settings: could not write settings.json");
    } else if (status_.rfind(L"Could not", 0) == 0 || status_.rfind(L"settings.json could", 0) == 0) {
        status_.clear();
    }
    if (notifyHook_) {
        notifyHook_();
    } else {
        // Tell the running shell (if any) to re-read what it uses. Off the UI thread: a busy shell must not stall this window.
        std::thread([] {
            std::string resp;
            ipc::Call(ShellPipeName(), ipc::MakeMessage("request", ipc::msg::kSettingsChanged, 1), &resp, 1500);
        }).detach();
    }
    Invalidate();
}

void SettingsWindow::FlipToggle(const Item& item) {
    const bool now = !store_.GetBool(item.key, item.defBool);
    store_.SetBool(item.key, now);
    if (item.key == "startup.launchAtLogin") {
        if (startupHook_) {
            startupHook_(now);
        } else if (!CurrentProfile().IsTest()) {
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
                if (now) {
                    const std::wstring cmd = L"\"" + ExePath(L"velora-shell.exe").wstring() + L"\"";
                    RegSetValueExW(key, L"Velora", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
                } else {
                    RegDeleteValueW(key, L"Velora");
                }
                RegCloseKey(key);
            }
        }
    }
    Changed();
}

void SettingsWindow::OpenChoice(const Item& item, const D2D1_RECT_F& rect) {
    std::vector<menubar::MenuItem> rows;
    const int cur = ChoiceIndex(store_, item);
    for (size_t i = 0; i < item.choices.size(); ++i) {
        menubar::MenuItem m;
        m.id = static_cast<unsigned>(i + 1);
        m.label = item.choices[i].label;
        if (static_cast<int>(i) == cur) m.label = L"\u2713  " + m.label;  // marks the current value
        rows.push_back(std::move(m));
    }
    const float s = Scale();
    POINT pt{static_cast<LONG>(rect.left * s), static_cast<LONG>(rect.bottom * s + 4 * s)};
    ClientToScreen(hwnd_, &pt);
    const std::string key = item.key;
    const std::vector<Choice> choices = item.choices;
    popup_.Open(hwnd_, pt.x, pt.y, std::move(rows), [this, key, choices](unsigned id) {
        if (id == 0 || id > choices.size()) return;
        store_.SetString(key, choices[id - 1].value);
        Changed();
    }, [] {});
}

void SettingsWindow::RunAction(const std::string& action) {
    if (actionHook_) {
        actionHook_(action);
        return;
    }
    if (action == "restore-windows") {
        // Velora's own watchdog restores the taskbar, wallpaper and window styles (the same path as the Start-menu shortcut).
        std::wstring cmd = L"\"" + ExePath(L"velora-watchdog.exe").wstring() + L"\" --restore";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            status_ = L"Restoring Windows...";
        } else {
            status_ = L"Could not start velora-watchdog.exe --restore.";
            log::Error("settings: could not start velora-watchdog --restore");
        }
    } else if (action == "check-updates") {
        ShellExecuteW(nullptr, L"open", L"https://github.com/moelsaka01/velora/releases", nullptr, nullptr, SW_SHOWNORMAL);
    }
    Invalidate();
}

void SettingsWindow::AddListEntry(const Item& item) {
    const std::string name = NormalizeExeName(WideToUtf8(field_.Text()));
    if (name.empty()) {
        status_ = L"Type an app's file name, for example notepad++.exe.";
        Invalidate();
        return;
    }
    auto list = store_.GetList(item.key);
    if (std::find(list.begin(), list.end(), name) == list.end() && list.size() < 256) list.push_back(name);
    store_.SetList(item.key, list);
    field_.Clear();
    status_.clear();
    Changed();
}

void SettingsWindow::TypeInField(const std::wstring& text) {
    fieldFocus_ = true;
    for (wchar_t c : text) field_.Insert(c);
    Invalidate();
}

void SettingsWindow::Activate(const Hit& hit) {
    Layout();
    const Page& page = pages_[page_];
    const Item* item = hit.item >= 0 && hit.item < static_cast<int>(page.items.size()) ? &page.items[hit.item] : nullptr;
    if (item && !ItemEnabled(store_, *item) && hit.kind != HitKind::Page && hit.kind != HitKind::Traffic) return;
    switch (hit.kind) {
        case HitKind::Page: ShowPage(hit.sub); break;
        case HitKind::Traffic:
            if (hit.sub == 0) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            else if (hit.sub == 1) ShowWindow(hwnd_, SW_MINIMIZE);
            else ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            break;
        case HitKind::Toggle: if (item) FlipToggle(*item); break;
        case HitKind::ChoiceButton: if (item) OpenChoice(*item, hit.rect); break;
        case HitKind::ActionButton: if (item) RunAction(item->action); break;
        case HitKind::ListRemove:
            if (item) {
                auto list = store_.GetList(item->key);
                if (hit.sub >= 0 && hit.sub < static_cast<int>(list.size())) list.erase(list.begin() + hit.sub);
                store_.SetList(item->key, list);
                Changed();
            }
            break;
        case HitKind::ListField: fieldFocus_ = true; Invalidate(); break;
        case HitKind::ListAdd: if (item) AddListEntry(*item); break;
        case HitKind::None: break;
    }
}

void SettingsWindow::MoveFocus(int dir) {
    Layout();
    std::vector<int> order;
    for (size_t i = 0; i < hits_.size(); ++i) {
        const HitKind k = hits_[i].kind;
        if (k == HitKind::Toggle || k == HitKind::ChoiceButton || k == HitKind::ActionButton || k == HitKind::ListField) order.push_back(static_cast<int>(i));
    }
    if (order.empty()) return;
    auto it = std::find(order.begin(), order.end(), focus_);
    size_t pos = it == order.end() ? (dir > 0 ? order.size() - 1 : 0) : static_cast<size_t>(it - order.begin());
    pos = (pos + order.size() + (dir > 0 ? 1 : order.size() - 1)) % order.size();
    focus_ = order[pos];
    fieldFocus_ = hits_[focus_].kind == HitKind::ListField;
    // Keep the focused control visible.
    const float h = static_cast<float>(host_.Height()) / Scale();
    const D2D1_RECT_F r = hits_[focus_].rect;
    if (r.bottom > h - 12) scroll_ += r.bottom - (h - 12);
    if (r.top < kRowsTop) scroll_ -= kRowsTop - r.top;
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, contentH_ - (h - kRowsTop)));
    Invalidate();
}

// ---- drawing --------------------------------------------------------------------------------------------------------------

void SettingsWindow::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const Colors c = ColorsFor(theme_);
    ID2D1DeviceContext* dc = host_.BeginDraw(c.content);
    if (!dc) return;
    Layout();
    const float s = Scale();
    const float width = static_cast<float>(host_.Width()) / s, height = static_cast<float>(host_.Height()) / s;
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c.text, &b);
    IDWriteTextFormat* body = ui::Format(14);
    IDWriteTextFormat* tiny = ui::Format(12);
    IDWriteTextFormat* title = ui::Format(24, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    const bool dark = theme_ == tokens::Theme::Dark;
    const Page& page = pages_[page_];

    // Content: title, rows.
    dc->PushAxisAlignedClip(D2D1::RectF(kSidebarW, kToolbarH, width, height), D2D1_ANTIALIAS_MODE_ALIASED);
    float y = kRowsTop - scroll_;
    const float left = kSidebarW + kPad, right = width - kPad;
    size_t hi = 3 + pages_.size();  // hits_ for the rows start after the traffic lights and the page list
    for (size_t idx = 0; idx < page.items.size(); ++idx) {
        const Item& it = page.items[idx];
        const bool enabled = ItemEnabled(store_, it);
        const float alpha = enabled ? 1.0f : 0.4f;
        auto labelColor = [&](bool dim) {
            D2D1_COLOR_F col = dim ? c.dim : c.text;
            col.a *= alpha;
            return col;
        };
        auto drawRowText = [&](float rowH, float textRight) {
            const bool hasDetail = !it.detail.empty();
            const float ty = y + (rowH - (hasDetail ? 38.0f : 20.0f)) / 2;
            b->SetColor(labelColor(false));
            ui::DrawLabel(dc, b.Get(), it.label, D2D1::RectF(left, ty, textRight, ty + 20), body);
            if (hasDetail) {
                b->SetColor(labelColor(true));
                ui::DrawLabel(dc, b.Get(), it.detail, D2D1::RectF(left, ty + 19, textRight, ty + 37), tiny);
            }
        };
        auto separator = [&](float at) {
            b->SetColor(c.line);
            dc->FillRectangle(D2D1::RectF(left, at - 0.5f, right, at + 0.5f), b.Get());
        };
        auto focusRing = [&](const D2D1_RECT_F& r, float radius) {
            if (focus_ >= 0 && focus_ < static_cast<int>(hits_.size()) && In(hits_[focus_].rect, (r.left + r.right) / 2, (r.top + r.bottom) / 2) && hits_[focus_].item == static_cast<int>(idx)) {
                b->SetColor(c.accent);
                dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2), radius + 2, radius + 2), b.Get(), 2.0f);
            }
        };
        switch (it.kind) {
            case ItemKind::Toggle: {
                const D2D1_RECT_F r = hits_[hi++].rect;
                drawRowText(kRowH, r.left - 16);
                const bool on = store_.GetBool(it.key, it.defBool);
                D2D1_COLOR_F track = on ? c.accent : c.controlOff;
                track.a *= alpha;
                b->SetColor(track);
                dc->FillRoundedRectangle(D2D1::RoundedRect(r, kSwitchH / 2, kSwitchH / 2), b.Get());
                const float knob = kSwitchH - 6;
                const float kx = on ? r.right - 3 - knob : r.left + 3;
                b->SetColor(D2D1::ColorF(1, 1, 1, alpha));
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx + knob / 2, (r.top + r.bottom) / 2), knob / 2, knob / 2), b.Get());
                focusRing(r, kSwitchH / 2);
                y += kRowH;
                separator(y);
                break;
            }
            case ItemKind::Choice: {
                const D2D1_RECT_F r = hits_[hi++].rect;
                drawRowText(kRowH, r.left - 16);
                b->SetColor(c.control);
                dc->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), b.Get());
                const int cur = ChoiceIndex(store_, it);
                b->SetColor(labelColor(false));
                ui::DrawLabel(dc, b.Get(), cur >= 0 ? it.choices[cur].label : L"", D2D1::RectF(r.left + 12, r.top, r.right - 28, r.bottom), body);
                b->SetColor(labelColor(true));  // chevron
                const float cx = r.right - 16, cy = (r.top + r.bottom) / 2;
                dc->DrawLine(D2D1::Point2F(cx - 4, cy - 2), D2D1::Point2F(cx, cy + 2), b.Get(), 1.5f);
                dc->DrawLine(D2D1::Point2F(cx, cy + 2), D2D1::Point2F(cx + 4, cy - 2), b.Get(), 1.5f);
                focusRing(r, 7);
                y += kRowH;
                separator(y);
                break;
            }
            case ItemKind::Button: {
                const D2D1_RECT_F r = hits_[hi++].rect;
                drawRowText(kRowH, r.left - 16);
                b->SetColor(c.control);
                dc->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), b.Get());
                b->SetColor(c.text);
                ui::DrawLabel(dc, b.Get(), it.action == "restore-windows" ? L"Restore now" : L"Open", r, body, DWRITE_TEXT_ALIGNMENT_CENTER);
                focusRing(r, 7);
                y += kRowH;
                separator(y);
                break;
            }
            case ItemKind::Note: {
                b->SetColor(c.dim);
                ui::DrawLabel(dc, b.Get(), it.detail, D2D1::RectF(left, y, right, y + kNoteH), tiny);
                y += kNoteH;
                break;
            }
            case ItemKind::List: {
                drawRowText(kRowH, right);
                y += kRowH;
                const auto entries = store_.GetList(it.key);
                for (size_t e = 0; e < entries.size(); ++e) {
                    const D2D1_RECT_F r = hits_[hi++].rect;
                    b->SetColor(labelColor(false));
                    ui::DrawLabel(dc, b.Get(), Utf8ToWide(entries[e]), D2D1::RectF(left + 12, y, right - 40, y + kEntryH), body);
                    b->SetColor(labelColor(true));
                    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
                    dc->DrawLine(D2D1::Point2F(cx - 4, cy - 4), D2D1::Point2F(cx + 4, cy + 4), b.Get(), 1.5f);
                    dc->DrawLine(D2D1::Point2F(cx - 4, cy + 4), D2D1::Point2F(cx + 4, cy - 4), b.Get(), 1.5f);
                    y += kEntryH;
                }
                const D2D1_RECT_F field = hits_[hi++].rect, add = hits_[hi++].rect;
                b->SetColor(c.control);
                dc->FillRoundedRectangle(D2D1::RoundedRect(field, 7, 7), b.Get());
                if (fieldFocus_) {
                    b->SetColor(c.accent);
                    dc->DrawRoundedRectangle(D2D1::RoundedRect(field, 7, 7), b.Get(), 1.5f);
                }
                if (field_.Text().empty()) {
                    b->SetColor(c.dim);
                    ui::DrawLabel(dc, b.Get(), L"app.exe", D2D1::RectF(field.left + 12, field.top, field.right - 8, field.bottom), body);
                } else {
                    b->SetColor(labelColor(false));
                    ui::DrawLabel(dc, b.Get(), field_.Text(), D2D1::RectF(field.left + 12, field.top, field.right - 8, field.bottom), body);
                    if (fieldFocus_) {
                        const float cx = field.left + 12 + ui::MeasureWidth(field_.Text().substr(0, field_.Caret()), body);
                        b->SetColor(c.text);
                        dc->FillRectangle(D2D1::RectF(cx, field.top + 6, cx + 1.2f, field.bottom - 6), b.Get());
                    }
                }
                b->SetColor(c.control);
                dc->FillRoundedRectangle(D2D1::RoundedRect(add, 7, 7), b.Get());
                b->SetColor(c.text);
                ui::DrawLabel(dc, b.Get(), L"Add", add, body, DWRITE_TEXT_ALIGNMENT_CENTER);
                y += kAddH;
                separator(y);
                break;
            }
        }
    }
    dc->PopAxisAlignedClip();

    // Title strip (stays put while the rows scroll).
    b->SetColor(c.content);
    dc->FillRectangle(D2D1::RectF(kSidebarW, 0, width, kRowsTop - 6), b.Get());
    b->SetColor(c.text);
    ui::DrawLabel(dc, b.Get(), page.title, D2D1::RectF(left, kTitleY - 6, right, kTitleY + 30), title);
    if (!status_.empty()) {
        b->SetColor(c.dim);
        ui::DrawLabel(dc, b.Get(), status_, D2D1::RectF(left, kTitleY + 30, right, kTitleY + 50), tiny);
    }

    // Sidebar: pages and the traffic lights.
    b->SetColor(c.sidebar);
    dc->FillRectangle(D2D1::RectF(0, 0, kSidebarW, height), b.Get());
    b->SetColor(c.line);
    dc->FillRectangle(D2D1::RectF(kSidebarW - 1, 0, kSidebarW, height), b.Get());
    ui::chrome::DrawTrafficLights(dc, b.Get(), active_, dark, hover_ >= 0 && hover_ < 3);
    for (size_t i = 0; i < pages_.size(); ++i) {
        const D2D1_RECT_F r = hits_[3 + i].rect;
        const bool sel = static_cast<int>(i) == page_;
        if (sel || hover_ == static_cast<int>(3 + i)) {
            b->SetColor(sel ? D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, active_ ? 1.0f : 0.5f) : c.hover);
            dc->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), b.Get());
        }
        b->SetColor(sel ? c.onAccent : c.text);
        ui::DrawLabel(dc, b.Get(), pages_[i].title, D2D1::RectF(r.left + 12, r.top, r.right - 8, r.bottom), body);
    }

    if (!host_.EndDraw()) {
        log::Warn("settings: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        Invalidate();
    }
}

// ---- messages -------------------------------------------------------------------------------------------------------------

LRESULT SettingsWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const float s = Scale();
    switch (msg) {
        case WM_CREATE:
            dpi_ = GetDpiForWindow(hwnd_);
            theme_ = ui::SystemTheme();
            if (!host_.Initialize(hwnd_)) log::Warn("settings: render host unavailable; the window stays blank");
            render::ApplyRoundedCorners(hwnd_, true);
            {
                MARGINS m{1, 1, 1, 1};
                DwmExtendFrameIntoClientArea(hwnd_, &m);  // the system shadow of the borderless window
            }
            SetTimer(hwnd_, kTimerCaret, 530, nullptr);
            return 0;
        case WM_NCCALCSIZE:
            if (wp) return 0;
            break;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT wr{};
            GetWindowRect(hwnd_, &wr);
            if (!IsZoomed(hwnd_))
                if (const LRESULT edge = ui::chrome::ResizeHitTest(wr, pt, static_cast<int>(kResizeBorder * s))) return edge;
            const float x = (pt.x - wr.left) / s, y = (pt.y - wr.top) / s;
            if (y < kToolbarH && ui::chrome::TrafficHit(x, y) < 0) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO:
            ui::chrome::FillMinMaxInfo(hwnd_, reinterpret_cast<MINMAXINFO*>(lp), static_cast<int>(620 * s), static_cast<int>(420 * s));
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Draw();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && host_.Ready()) {
                host_.Resize(LOWORD(lp), HIWORD(lp));
                Layout();
                scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, contentH_ - (static_cast<float>(host_.Height()) / s - kRowsTop)));
                Invalidate();
            }
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Invalidate();
            return 0;
        }
        case WM_SETTINGCHANGE:
            theme_ = ui::SystemTheme();
            Invalidate();
            return 0;
        case WM_ACTIVATE:
            active_ = LOWORD(wp) != WA_INACTIVE;
            Invalidate();
            return 0;
        case WM_TIMER:
            if (wp == kTimerCaret && fieldFocus_) Invalidate();
            return 0;
        case WM_MOUSEMOVE: {
            Layout();
            const int h = HitIndexAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (h != hover_) {
                hover_ = h;
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&t);
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hover_ = -1;
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            Layout();
            const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
            const int h = HitIndexAt(x, y);
            if (h >= 0) {
                focus_ = hits_[h].kind == HitKind::Page || hits_[h].kind == HitKind::Traffic ? focus_ : h;
                fieldFocus_ = hits_[h].kind == HitKind::ListField;
                const Hit hit = hits_[h];  // Activate() rebuilds hits_
                Activate(hit);
            } else {
                fieldFocus_ = false;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            Layout();
            const float h = static_cast<float>(host_.Height()) / s;
            scroll_ = std::clamp(scroll_ - static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 48.0f, 0.0f, std::max(0.0f, contentH_ - (h - kRowsTop)));
            Invalidate();
            return 0;
        }
        case WM_CHAR:
            if (fieldFocus_) {
                if (wp == VK_RETURN) {
                    Layout();
                    for (const Hit& hit : hits_)
                        if (hit.kind == HitKind::ListAdd) {
                            Activate(hit);
                            break;
                        }
                } else if (field_.Insert(static_cast<wchar_t>(wp))) {
                    Invalidate();
                }
            }
            return 0;
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0;
            if (ctrl && (wp == 'W')) {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return 0;
            }
            if (fieldFocus_) {
                switch (wp) {
                    case VK_BACK: field_.Backspace(); break;
                    case VK_DELETE: field_.Delete(); break;
                    case VK_LEFT: field_.Left(GetKeyState(VK_SHIFT) < 0); break;
                    case VK_RIGHT: field_.Right(GetKeyState(VK_SHIFT) < 0); break;
                    case VK_ESCAPE: fieldFocus_ = false; break;
                    case VK_UP: MoveFocus(-1); return 0;
                    case VK_DOWN: case VK_TAB: MoveFocus(1); return 0;
                    default: return 0;
                }
                Invalidate();
                return 0;
            }
            switch (wp) {
                case VK_DOWN: case VK_TAB: MoveFocus(1); break;
                case VK_UP: MoveFocus(-1); break;
                case VK_SPACE: case VK_RETURN:
                    Layout();
                    if (focus_ >= 0 && focus_ < static_cast<int>(hits_.size())) {
                        const Hit hit = hits_[focus_];
                        Activate(hit);
                    }
                    break;
                case VK_PRIOR: ShowPage(page_ - 1); break;
                case VK_NEXT: ShowPage(page_ + 1); break;
                default: break;
            }
            return 0;
        }
        case WM_CLOSE:
            popup_.Close();
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kTimerCaret);
            popup_.Close();
            host_.Shutdown();
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::settings
