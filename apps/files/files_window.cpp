#include "files/files_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <mutex>
#include <thread>

#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"
#include "ui/ui_common.h"

#pragma comment(lib, "dwmapi.lib")

namespace velora::files {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"VeloraFilesWindow";
constexpr UINT kMsgListed = WM_APP + 1;
constexpr UINT kMsgIcons = WM_APP + 2;
constexpr UINT_PTR kTimerCaret = 1, kTimerChange = 2;
constexpr float kResizeBorder = 6.0f;  // DIPs

enum Glyph {
    kHome, kDesktop, kDocument, kDownload, kPicture, kMusic, kVideo,  // FavoritePlaces() numbers 0..6
    kDrive, kFolder, kChevronLeft, kChevronRight, kViewList, kViewIcons, kFileSheet,
};

enum MenuCmd : unsigned {
    kcOpen = 1, kcCopy, kcCut, kcPaste, kcRename, kcDelete, kcCopyPath, kcNewFolder, kcViewList, kcViewIcons, kcHidden, kcRefresh,
};

struct Colors {
    D2D1_COLOR_F content, sidebar, text, dim, line, accent, hover, selActive, selInactive, onAccent;
};

Colors ColorsFor(tokens::Theme t) {
    const tokens::Palette& p = tokens::PaletteFor(t);
    const bool dark = t == tokens::Theme::Dark;
    Colors c;
    c.content = ui::Rgba({dark ? tokens::window::kContentDark : tokens::window::kContentLight});
    c.sidebar = ui::Rgba({dark ? tokens::window::kSidebarDark : tokens::window::kSidebarLight});
    c.text = ui::Rgba(p.text);
    c.dim = ui::Rgba(p.textSecondary);
    c.line = dark ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(0, 0, 0, 0.10f);
    c.accent = ui::Rgba(p.accent);
    c.hover = dark ? D2D1::ColorF(1, 1, 1, 0.07f) : D2D1::ColorF(0, 0, 0, 0.05f);
    c.selActive = ui::Rgba(p.accent);
    c.selInactive = dark ? D2D1::ColorF(1, 1, 1, 0.16f) : D2D1::ColorF(0, 0, 0, 0.12f);
    c.onAccent = D2D1::ColorF(1, 1, 1, 1);
    return c;
}

std::uint64_t NowFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
        if (auto* p = static_cast<wchar_t*>(GlobalLock(h))) {
            memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
    return h != nullptr;
}

std::wstring GetClipboardText(HWND owner) {
    std::wstring out;
    if (!OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

// Errors the shell never saw (we refused before handing the work over) need our own message; errors it did see were shown by its dialog.
bool NeedsOwnDialog(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_INVALID_NAME) || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) ||
           hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
           hr == HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
}

// ---- glyphs: simple original line drawings, 18 DIP box centred on (cx, cy) ----
void DrawGlyph(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b, int glyph, float cx, float cy, float w = 1.5f) {
    auto line = [&](float x1, float y1, float x2, float y2) {
        dc->DrawLine(D2D1::Point2F(cx + x1, cy + y1), D2D1::Point2F(cx + x2, cy + y2), b, w, nullptr);
    };
    auto rect = [&](float l, float t, float r, float bo, float rad) {
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + l, cy + t, cx + r, cy + bo), rad, rad), b, w);
    };
    switch (glyph) {
        case kHome:
            line(-8, 0, 0, -7.5f); line(0, -7.5f, 8, 0);
            line(-5.5f, -2, -5.5f, 7); line(5.5f, -2, 5.5f, 7); line(-5.5f, 7, 5.5f, 7);
            line(-1.8f, 7, -1.8f, 2.5f); line(-1.8f, 2.5f, 1.8f, 2.5f); line(1.8f, 2.5f, 1.8f, 7);
            break;
        case kDesktop:
            rect(-8, -7, 8, 3, 1.5f); line(0, 3, 0, 6.5f); line(-4, 7, 4, 7);
            break;
        case kDocument:
        case kFileSheet:
            line(-5, -8, 2, -8); line(2, -8, 5, -5); line(5, -5, 5, 8); line(5, 8, -5, 8); line(-5, 8, -5, -8);
            line(2, -8, 2, -5); line(2, -5, 5, -5);
            if (glyph == kDocument) { line(-2, 0, 2, 0); line(-2, 3.5f, 2, 3.5f); }
            break;
        case kDownload:
            line(0, -7, 0, 2.5f); line(-4, -1.5f, 0, 2.5f); line(0, 2.5f, 4, -1.5f); line(-7, 6.5f, 7, 6.5f);
            break;
        case kPicture:
            rect(-8, -6, 8, 6, 1.5f); dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 3.5f, cy - 2), 1.5f, 1.5f), b, w);
            line(-8, 5, -2.5f, 0.5f); line(-2.5f, 0.5f, 1.5f, 3.5f); line(1.5f, 3.5f, 4.5f, 0); line(4.5f, 0, 8, 3.5f);
            break;
        case kMusic:
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 3, cy + 5), 2.6f, 2.2f), b, w);
            line(-0.4f, 5, -0.4f, -6.5f); line(-0.4f, -6.5f, 5.5f, -4.5f); line(5.5f, -4.5f, 5.5f, -2.5f); line(5.5f, -2.5f, -0.4f, -4.5f);
            break;
        case kVideo:
            rect(-8, -5.5f, 8, 5.5f, 1.5f); line(-2, -3, -2, 3); line(-2, -3, 3.2f, 0); line(3.2f, 0, -2, 3);
            break;
        case kDrive:
            rect(-8, -3, 8, 5, 2); line(-8, 1.5f, 8, 1.5f); line(3, 3.3f, 5, 3.3f);
            break;
        case kFolder:
            line(-8, -5, -8, 6.5f); line(-8, 6.5f, 8, 6.5f); line(8, 6.5f, 8, -3); line(8, -3, 0.5f, -3); line(0.5f, -3, -1, -5); line(-1, -5, -8, -5);
            break;
        case kChevronLeft: line(2.5f, -5.5f, -2.5f, 0); line(-2.5f, 0, 2.5f, 5.5f); break;
        case kChevronRight: line(-2.5f, -5.5f, 2.5f, 0); line(2.5f, 0, -2.5f, 5.5f); break;
        case kViewList:
            for (float y : {-4.5f, 0.0f, 4.5f}) { line(-5, y, -3.5f, y); line(-1, y, 6, y); }
            break;
        case kViewIcons:
            for (float x : {-4.0f, 1.0f}) for (float y : {-4.0f, 1.0f}) rect(x - 0.5f, y - 0.5f, x + 3.5f, y + 3.5f, 1.0f);
            break;
        default: break;
    }
}

}  // namespace

// ---- icon worker ----
class FileIcons {
public:
    void Start(HWND notify) {
        notify_ = notify;
        worker_ = std::thread([this] { Run(); });
    }
    ~FileIcons() { Stop(); }
    void Stop() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    std::shared_ptr<const ui::Pixels> Find(const std::wstring& key, int tier) {
        std::lock_guard lock(mutex_);
        auto it = done_.find({key, tier});
        return it == done_.end() ? nullptr : it->second;
    }
    bool Failed(const std::wstring& key, int tier) {
        std::lock_guard lock(mutex_);
        return failed_.count({key, tier}) != 0;
    }
    void Request(const std::wstring& key, const std::wstring& source, int tier) {
        std::lock_guard lock(mutex_);
        const Key k{key, tier};
        if (done_.count(k) || failed_.count(k) || requested_.count(k)) return;
        requested_.insert(k);
        queue_.push_back({k, source});
        cv_.notify_one();
    }
    // Forget everything (a new folder): the bitmaps of the old one are not kept.
    void Clear() {
        std::lock_guard lock(mutex_);
        done_.clear();
        failed_.clear();
        requested_.clear();
        queue_.clear();
    }

private:
    using Key = std::pair<std::wstring, int>;
    void Run() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (;;) {
            std::pair<Key, std::wstring> job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (stop_) break;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            auto px = std::make_shared<ui::Pixels>();
            // Real file and folder icons as the shell draws them (no tile: this is a file browser, not the Dock).
            const bool ok = ui::LoadIconPixels(job.second, px.get(), nullptr, false, job.first.second);
            HWND n;
            {
                std::lock_guard lock(mutex_);
                if (ok) done_[job.first] = px;
                else failed_.insert(job.first);
                requested_.erase(job.first);
                n = notify_;
            }
            if (n) PostMessageW(n, kMsgIcons, 0, 0);
        }
        CoUninitialize();
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<Key, std::shared_ptr<const ui::Pixels>> done_;
    std::set<Key> failed_, requested_;
    std::deque<std::pair<Key, std::wstring>> queue_;
    std::thread worker_;
    HWND notify_ = nullptr;
    bool stop_ = false;
};

struct FilesWindow::ListMessage {
    unsigned generation = 0;
    std::wstring dir;
    Listing listing;
    std::wstring select;
    std::set<std::wstring> keep;
};

// IDropTarget: files dropped from Explorer, Velora Files or any shell source. Lives inside the window (never heap-freed by Release).
struct FilesWindow::DropTarget : IDropTarget {
    explicit DropTarget(FilesWindow* w) : w_(w) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        w_->dropSources_ = PathsFromDataObject(data);
        return DragOver(keys, pt, effect);
    }
    STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        POINT p{pt.x, pt.y};
        ScreenToClient(w_->hwnd_, &p);
        const float s = w_->Scale();
        int hotItem = -1, hotSide = -1;
        const std::wstring dest = w_->DropFolderAt(p.x / s, p.y / s, &hotItem, &hotSide);
        DWORD allowed = *effect, result = DROPEFFECT_NONE;
        if (!w_->dropSources_.empty() && !dest.empty()) {
            for (const std::wstring& src : w_->dropSources_) {
                DWORD e = DropEffectFor(src, dest, (keys & MK_CONTROL) != 0, (keys & MK_SHIFT) != 0);
                if (e & allowed) {
                    result = e & allowed;
                    break;
                }
            }
        }
        *effect = result;
        w_->dropEffect_ = result;
        if (hotItem != w_->dropHotItem_ || hotSide != w_->dropHotSide_) {
            w_->dropHotItem_ = result ? hotItem : -1;
            w_->dropHotSide_ = result ? hotSide : -1;
            w_->Invalidate();
        }
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        w_->dropSources_.clear();
        w_->dropHotItem_ = w_->dropHotSide_ = -1;
        w_->Invalidate();
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        DragOver(keys, pt, effect);
        const DWORD e = *effect;
        POINT p{pt.x, pt.y};
        ScreenToClient(w_->hwnd_, &p);
        const float s = w_->Scale();
        int a = -1, b = -1;
        const std::wstring dest = w_->DropFolderAt(p.x / s, p.y / s, &a, &b);
        w_->dropHotItem_ = w_->dropHotSide_ = -1;
        w_->dropSources_.clear();
        if (e != DROPEFFECT_NONE && !dest.empty()) w_->ExecuteDrop(data, dest, e);
        else *effect = DROPEFFECT_NONE;
        w_->Invalidate();
        return S_OK;
    }
    FilesWindow* w_;
};

namespace {
struct DropSource : IDropSource {
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
}  // namespace

FilesWindow::FilesWindow(HINSTANCE instance) : instance_(instance), popup_(instance) {}

FilesWindow::~FilesWindow() {
    if (change_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_);
}

bool FilesWindow::Create(const std::wstring& dir, const std::wstring& select, int showCmd) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = &FilesWindow::WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) return false;

    auto settings = DataDir();
    settingsPath_ = settings.empty() ? std::wstring{} : (settings / L"settings.json").wstring();
    prefs_ = LoadPrefs(settingsPath_);

    // Cascade from the primary monitor's centre: 960 x 620 DIPs by default.
    const UINT sysDpi = GetDpiForSystem();
    const int w = static_cast<int>(prefs_.width * sysDpi / 96.0f), h = static_cast<int>(prefs_.height * sysDpi / 96.0f);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClass, L"Files", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("files: CreateWindowEx failed");
        return false;
    }
    std::wstring start = dir;
    if (start.empty() || GetFileAttributesW(start.c_str()) == INVALID_FILE_ATTRIBUTES) {
        PWSTR home = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &home)) && home) start = home;
        CoTaskMemFree(home);
    }
    // Centre on the monitor under the cursor.
    POINT pt{};
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT r{};
    GetWindowRect(hwnd_, &r);
    const int ww = std::min<int>(r.right - r.left, mi.rcWork.right - mi.rcWork.left), hh = std::min<int>(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
    SetWindowPos(hwnd_, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2, mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - hh) / 2, ww, hh,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwnd_, showCmd == SW_HIDE ? SW_SHOWNORMAL : showCmd);
    Navigate(start, select);
    SetForegroundWindow(hwnd_);
    return true;
}

void FilesWindow::Open(const std::wstring& dir, const std::wstring& select) {
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    if (!dir.empty()) Navigate(dir, select);
    SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK FilesWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<FilesWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (!self->hwnd_) self->hwnd_ = h;
    return self->OnMessage(msg, wp, lp);
}

float FilesWindow::Scale() const { return ui::Scale(dpi_); }
float FilesWindow::ViewWidth() const { return static_cast<float>(host_.Width()) / Scale() - kSidebarW; }
float FilesWindow::ViewTop() const { return kToolbarH + (prefs_.iconView ? 0.0f : kHeaderH); }
float FilesWindow::ViewBottom() const { return static_cast<float>(host_.Height()) / Scale() - kStatusH; }
float FilesWindow::ViewHeight() const { return std::max(0.0f, ViewBottom() - ViewTop()); }
int FilesWindow::GridColumns() const { return prefs_.iconView ? ComputeIconGrid(ViewWidth()).columns : 1; }

float FilesWindow::ContentHeight() const {
    const int n = static_cast<int>(entries_.size());
    if (prefs_.iconView) return RowCount(n, GridColumns()) * ComputeIconGrid(ViewWidth()).cellH + 8;
    return n * kRowH + 4;
}

D2D1_RECT_F FilesWindow::ItemRect(int index) const {
    if (prefs_.iconView) {
        const IconGrid g = ComputeIconGrid(ViewWidth());
        const int row = index / g.columns, col = index % g.columns;
        const float x = kSidebarW + g.left + col * g.cellW, y = ViewTop() + 4 + row * g.cellH - scroll_;
        return D2D1::RectF(x, y, x + g.cellW, y + g.cellH);
    }
    const float y = ViewTop() + index * kRowH - scroll_;
    return D2D1::RectF(kSidebarW + 8, y, kSidebarW + ViewWidth() - 8, y + kRowH);
}

D2D1_RECT_F FilesWindow::NameRect(int index) const {
    D2D1_RECT_F r = ItemRect(index);
    if (prefs_.iconView) return D2D1::RectF(r.left + 2, r.top + 8 + kIconSize + 4, r.right - 2, r.top + 8 + kIconSize + 4 + 38);
    const Columns c = ComputeColumns(ViewWidth() - 16);
    return D2D1::RectF(r.left + 42, r.top, r.left + c.name, r.bottom);
}

int FilesWindow::HitItem(float x, float y) const {
    if (x < kSidebarW || y >= ViewBottom()) return -1;
    const int n = static_cast<int>(entries_.size());
    if (prefs_.iconView) return GridHit(x - kSidebarW, y, ViewTop() + 4, scroll_, ComputeIconGrid(ViewWidth()), n);
    if (x < kSidebarW + 8 || x >= kSidebarW + ViewWidth() - 8) return -1;
    return ListHit(y, ViewTop(), scroll_, n);
}

FilesWindow::Hit FilesWindow::HitTest(float x, float y) const {
    // Traffic lights.
    for (int i = 0; i < 3; ++i) {
        const float cx = kTrafficLeft + kTrafficD / 2 + i * kTrafficPitch;
        if (std::hypot(x - cx, y - kTrafficCenterY) <= kTrafficD / 2 + 3) return {i == 0 ? Zone::Close : i == 1 ? Zone::Minimize : Zone::Zoom, -1};
    }
    if (x < kSidebarW) {
        for (size_t i = 0; i < side_.size(); ++i)
            if (!side_[i].header && y >= side_[i].y && y < side_[i].y + kSidebarRow && x >= 8 && x < kSidebarW - 8) return {Zone::Sidebar, static_cast<int>(i)};
        return {y < kToolbarH ? Zone::Caption : Zone::Blank, -1};
    }
    const float cy = (kToolbarH - kControlH) / 2;
    if (y >= cy && y < cy + kControlH) {
        const float bx = kSidebarW + 16;
        if (x >= bx && x < bx + kControlH) return {Zone::Back, -1};
        if (x >= bx + kControlH && x < bx + 2 * kControlH) return {Zone::Forward, -1};
        const float vx = kSidebarW + ViewWidth() - 16 - 2 * kControlH;
        if (x >= vx && x < vx + kControlH) return {Zone::ViewList, -1};
        if (x >= vx + kControlH && x < vx + 2 * kControlH) return {Zone::ViewIcons, -1};
    }
    if (y < kToolbarH) return {Zone::Caption, -1};
    const float bottom = ViewBottom();
    if (y >= bottom) {  // status bar: breadcrumbs
        float cx = kSidebarW + 12;
        auto crumbs = Breadcrumbs(dir_);
        for (size_t i = 0; i < crumbs.size(); ++i) {
            const float w = ui::MeasureWidth(crumbs[i].label, ui::Format(12)) + 14;
            if (x >= cx && x < cx + w) return {Zone::Crumb, static_cast<int>(i)};
            cx += w + 10;
        }
        return {Zone::Blank, -1};
    }
    if (!prefs_.iconView && y < kToolbarH + kHeaderH) {
        const Columns c = ComputeColumns(ViewWidth() - 16);
        float cx = kSidebarW + 8 + 42 - 30;
        (void)cx;
        const float nameEnd = kSidebarW + 8 + c.name;
        if (x < nameEnd) return {Zone::Header, 0};
        float edge = nameEnd;
        for (int col = 1; col <= 3; ++col) {
            const float w = col == 1 ? c.modified : col == 2 ? c.size : c.kind;
            if (w > 0 && x >= edge && x < edge + w) return {Zone::Header, col};
            edge += w;
        }
        return {Zone::Blank, -1};
    }
    const int item = HitItem(x, y);
    return item >= 0 ? Hit{Zone::Item, item} : Hit{Zone::Blank, -1};
}

void FilesWindow::ClampScroll() { scroll_ = files::ClampScroll(scroll_, ContentHeight(), ViewHeight()); }

void FilesWindow::EnsureVisible(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    D2D1_RECT_F r = ItemRect(index);
    const float top = r.top + scroll_ - ViewTop();
    scroll_ = ScrollToVisible(scroll_, top, r.bottom - r.top, ViewHeight());
    ClampScroll();
}

// ---- data ----
void FilesWindow::OnCreate() {
    dpi_ = GetDpiForWindow(hwnd_);
    theme_ = ui::SystemTheme();
    if (!host_.Initialize(hwnd_)) log::Warn("files: render host unavailable; the window stays blank");
    render::ApplyRoundedCorners(hwnd_, true);
    MARGINS m{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &m);  // gives the borderless window its system shadow
    icons_ = std::make_unique<FileIcons>();
    icons_->Start(hwnd_);
    dropTarget_ = std::make_unique<DropTarget>(this);
    RegisterDragDrop(hwnd_, dropTarget_.get());
    ReloadSidebar();
}

void FilesWindow::OnDestroy() {
    prefs_.width = std::max(480, static_cast<int>(ViewWidth() + kSidebarW));
    prefs_.height = std::max(320, static_cast<int>(host_.Height() / Scale()));
    if (!IsZoomed(hwnd_) && !IsIconic(hwnd_)) SavePrefs(settingsPath_, prefs_);
    RevokeDragDrop(hwnd_);
    OleFlushClipboard();  // a copy stays on the clipboard after this process is gone
    popup_.Close();
    KillTimer(hwnd_, kTimerCaret);
    bitmaps_.clear();
    icons_.reset();
    host_.Shutdown();
    hwnd_ = nullptr;
    PostQuitMessage(0);
}

void FilesWindow::ReloadSidebar() {
    side_.clear();
    float y = 56;
    auto header = [&](const wchar_t* label) {
        side_.push_back({label, {}, 0, true, y});
        y += 28;
    };
    header(L"Favorites");
    for (const Place& p : FavoritePlaces()) {
        side_.push_back({p.label, p.path, p.glyph, false, y});
        y += kSidebarRow;
    }
    y += 10;
    auto drives = Drives();
    if (!drives.empty()) {
        header(L"Drives");
        for (const DriveInfo& d : drives) {
            side_.push_back({d.label, d.path, kDrive, false, y});
            y += kSidebarRow;
        }
    }
}

void FilesWindow::WatchFolder() {
    if (change_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_);
    change_ = FindFirstChangeNotificationW(dir_.c_str(), FALSE,
                                           FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
}

void FilesWindow::OnDirectoryChanged() {
    if (change_ != INVALID_HANDLE_VALUE) FindNextChangeNotification(change_);
    SetTimer(hwnd_, kTimerChange, 300, nullptr);  // coalesce bursts (a download writing)
}

void FilesWindow::Navigate(const std::wstring& dir, std::wstring select, bool record) {
    std::wstring target = dir;
    while (target.size() > 3 && (target.back() == L'\\' || target.back() == L'/')) target.pop_back();
    const DWORD attr = GetFileAttributesW(target.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(hwnd_, (L"The folder \"" + target + L"\" can't be opened.").c_str(), L"Files", MB_OK | MB_ICONINFORMATION);
        return;
    }
    CancelRename();
    dir_ = target;
    if (record) history_.Visit(dir_);
    SetWindowTextW(hwnd_, LeafOf(dir_).empty() ? dir_.c_str() : LeafOf(dir_).c_str());
    sel_.Clear();
    scroll_ = 0;
    typed_.clear();
    WatchFolder();
    StartListing(select, {});
}

void FilesWindow::StartListing(const std::wstring& select, std::set<std::wstring> keep) {
    loading_ = true;
    listError_ = 0;
    const unsigned gen = ++generation_;
    HWND hwnd = hwnd_;
    const std::wstring dir = dir_;
    const bool hidden = prefs_.showHidden;
    std::thread([=]() mutable {
        auto* m = new ListMessage;
        m->generation = gen;
        m->dir = dir;
        m->listing = ListDirectory(dir, hidden);
        m->select = select;
        m->keep = std::move(keep);
        if (!PostMessageW(hwnd, kMsgListed, 0, reinterpret_cast<LPARAM>(m))) delete m;  // the window is gone
    }).detach();
    Invalidate();
}

void FilesWindow::ApplyPrefsToListing() { SortEntries(&entries_, prefs_.sort, prefs_.ascending); }

void FilesWindow::OnListed(ListMessage* msg) {
    std::unique_ptr<ListMessage> m(msg);
    if (m->generation != generation_) return;  // an older request
    loading_ = false;
    listError_ = m->listing.error;
    entries_ = std::move(m->listing.entries);
    ApplyPrefsToListing();
    sel_.Clear();
    bitmaps_.clear();
    icons_->Clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (m->keep.count(Lower(entries_[i].path))) sel_.Click(static_cast<int>(i), true, false);
    }
    if (!m->select.empty()) {
        const int i = IndexOfPath(m->select);
        if (i >= 0) {
            sel_.Only(i);
            EnsureVisible(i);
        }
    }
    if (pendingRename_ && !pendingSelect_.empty()) {
        const int i = IndexOfPath(pendingSelect_);
        if (i >= 0) {
            sel_.Only(i);
            EnsureVisible(i);
            BeginRename(i);
        }
    }
    pendingRename_ = false;
    pendingSelect_.clear();
    ClampScroll();
    Invalidate();
}

void FilesWindow::Reload() {
    std::set<std::wstring> keep;
    for (const std::wstring& p : SelectedPaths()) keep.insert(Lower(p));
    StartListing({}, std::move(keep));
}

std::vector<std::wstring> FilesWindow::SelectedPaths() const {
    std::vector<std::wstring> out;
    for (int i : sel_.Items())
        if (i >= 0 && i < static_cast<int>(entries_.size())) out.push_back(entries_[i].path);
    return out;
}

int FilesWindow::IndexOfPath(const std::wstring& path) const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if (_wcsicmp(entries_[i].path.c_str(), path.c_str()) == 0) return static_cast<int>(i);
    return -1;
}

// ---- actions ----
OpOptions FilesWindow::Opts() const {
    OpOptions o;
    o.ui = true;  // the OS shows its own progress, conflict and permission dialogs
    o.owner = hwnd_;
    o.conflict = Conflict::Ask;
    return o;
}

void FilesWindow::FinishOperation(const OpResult& r) {
    if (!r.Ok() && !r.aborted) {
        for (const ItemResult& it : r.items) {
            if (!it.skipped && FAILED(it.hr) && NeedsOwnDialog(it.hr)) {
                MessageBoxW(hwnd_, r.Describe().c_str(), L"Files", MB_OK | MB_ICONINFORMATION);
                break;
            }
        }
        log::Warn("files: operation did not finish cleanly: " + WideToUtf8(r.Describe()));
    }
    Reload();
}

void FilesWindow::OpenIndex(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    const Entry& e = entries_[index];
    if (e.isDir) {
        Navigate(e.path);
        return;
    }
    HINSTANCE r = ShellExecuteW(hwnd_, nullptr, e.path.c_str(), nullptr, dir_.c_str(), SW_SHOWNORMAL);  // the default app
    if (reinterpret_cast<INT_PTR>(r) <= 32) log::Warn("files: ShellExecute failed for " + WideToUtf8(e.path) + " code " + std::to_string(reinterpret_cast<INT_PTR>(r)));
}

void FilesWindow::GoBack() {
    if (std::wstring p = history_.Back(); !p.empty()) Navigate(p, {}, false);
}

void FilesWindow::GoForward() {
    if (std::wstring p = history_.Forward(); !p.empty()) Navigate(p, {}, false);
}

void FilesWindow::GoUp() {
    const std::wstring parent = ParentOf(dir_);
    if (!parent.empty()) Navigate(parent, dir_);
}

void FilesWindow::SetView(bool icons) {
    if (prefs_.iconView == icons) return;
    prefs_.iconView = icons;
    SavePrefs(settingsPath_, prefs_);
    bitmaps_.clear();
    scroll_ = 0;
    EnsureVisible(sel_.Focus());
    Invalidate();
}

void FilesWindow::SetSort(SortKey key) {
    if (prefs_.sort == key) prefs_.ascending = !prefs_.ascending;
    else {
        prefs_.sort = key;
        prefs_.ascending = true;
    }
    std::set<std::wstring> keep;
    for (const std::wstring& p : SelectedPaths()) keep.insert(Lower(p));
    ApplyPrefsToListing();
    sel_.Clear();
    for (size_t i = 0; i < entries_.size(); ++i)
        if (keep.count(Lower(entries_[i].path))) sel_.Click(static_cast<int>(i), true, false);
    SavePrefs(settingsPath_, prefs_);
    Invalidate();
}

void FilesWindow::CopyToClipboard(bool cut) {
    const auto paths = SelectedPaths();
    if (paths.empty()) return;
    auto data = MakeDataObject(paths);
    if (!data) return;
    SetPreferredDropEffect(data.Get(), cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY);
    if (FAILED(OleSetClipboard(data.Get()))) return;
    cut_.clear();
    if (cut)
        for (const std::wstring& p : paths) cut_.insert(Lower(p));
    Invalidate();
}

void FilesWindow::Paste() {
    ComPtr<IDataObject> data;
    if (FAILED(OleGetClipboard(&data)) || !data) return;
    if (PathsFromDataObject(data.Get()).empty()) return;
    const bool move = GetPreferredDropEffect(data.Get()) == DROPEFFECT_MOVE;
    OpResult r = Transfer(data.Get(), dir_, move, Opts());
    if (move && r.Ok()) {
        OleSetClipboard(nullptr);
        cut_.clear();
    }
    FinishOperation(r);
}

void FilesWindow::MoveToBin() {
    const auto paths = SelectedPaths();
    if (paths.empty()) return;
    // Recycle Bin only. If the shell cannot recycle an item it asks before deleting it for good (it is never told to skip that question).
    FinishOperation(Recycle(paths, Opts()));
}

void FilesWindow::BeginRename(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    renaming_ = index;
    const std::wstring& name = entries_[index].name;
    rename_.SetText(name);
    const size_t dot = name.rfind(L'.');
    rename_.Select(0, (!entries_[index].isDir && dot != std::wstring::npos && dot > 0) ? dot : name.size());  // not the extension
    caretOn_ = true;
    SetTimer(hwnd_, kTimerCaret, 530, nullptr);
    EnsureVisible(index);
    Invalidate();
}

void FilesWindow::CancelRename() {
    if (renaming_ < 0) return;
    renaming_ = -1;
    KillTimer(hwnd_, kTimerCaret);
    Invalidate();
}

void FilesWindow::CommitRename() {
    if (renaming_ < 0 || renaming_ >= static_cast<int>(entries_.size())) return;
    const Entry e = entries_[renaming_];
    const std::wstring name = rename_.Text();
    if (name == e.name) {
        CancelRename();
        return;
    }
    OpResult r = Rename(e.path, name, Opts());
    if (!r.Ok()) {
        MessageBoxW(hwnd_, r.Describe().c_str(), L"Files", MB_OK | MB_ICONINFORMATION);  // stay in the field to fix the name
        return;
    }
    renaming_ = -1;
    KillTimer(hwnd_, kTimerCaret);
    StartListing(JoinPath(dir_, name), {});
}

void FilesWindow::NewFolder() {
    std::wstring created;
    OpResult r = files::NewFolder(dir_, L"untitled folder", Opts(), &created);
    if (!r.Ok() || created.empty()) {
        FinishOperation(r);
        return;
    }
    pendingSelect_ = created;
    pendingRename_ = true;
    StartListing({}, {});
}

void FilesWindow::SelectIndex(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    sel_.Only(index);
    EnsureVisible(index);
    Invalidate();
}

void FilesWindow::TypeRenameText(const std::wstring& text) {
    if (renaming_ < 0) return;
    rename_.SetText(text);
    Invalidate();
}

FilesWindow::Snapshot FilesWindow::GetSnapshot() const {
    Snapshot s;
    s.dir = dir_;
    s.items = static_cast<int>(entries_.size());
    s.selected = static_cast<int>(sel_.Items().size());
    s.renaming = renaming_;
    s.loading = loading_;
    s.iconView = prefs_.iconView;
    s.canBack = history_.CanBack();
    s.canForward = history_.CanForward();
    s.drawn = framesDrawn_ > 0;
    s.listError = listError_;
    return s;
}

void FilesWindow::SelectAllItems() {
    sel_.All(static_cast<int>(entries_.size()));
    Invalidate();
}

void FilesWindow::CopyPathText() {
    std::wstring text;
    for (const std::wstring& p : SelectedPaths()) text += (text.empty() ? L"" : L"\r\n") + p;
    if (text.empty()) text = dir_;
    SetClipboardText(hwnd_, text);
}

void FilesWindow::ShowContextMenu(POINT screen, int hit) {
    using menubar::MenuItem;
    std::vector<MenuItem> items;
    ComPtr<IDataObject> clip;
    const bool canPaste = SUCCEEDED(OleGetClipboard(&clip)) && clip && !PathsFromDataObject(clip.Get()).empty();
    if (hit >= 0) {
        if (!sel_.Has(hit)) sel_.Only(hit);
        const bool one = sel_.Items().size() == 1;
        items = {{kcOpen, L"Open", false, one},       menubar::Separator(),      {kcCopy, L"Copy"},
                 {kcCut, L"Cut"},                     {kcPaste, L"Paste", false, canPaste}, menubar::Separator(),
                 {kcRename, L"Rename", false, one},   {kcDelete, L"Move to Recycle Bin"}, menubar::Separator(),
                 {kcCopyPath, L"Copy as Path"}};
    } else {
        items = {{kcNewFolder, L"New Folder"}, {kcPaste, L"Paste", false, canPaste}, menubar::Separator(),
                 {kcViewList, prefs_.iconView ? L"as List" : L"✓ as List"},
                 {kcViewIcons, prefs_.iconView ? L"✓ as Icons" : L"as Icons"},
                 {kcHidden, prefs_.showHidden ? L"✓ Show Hidden Files" : L"Show Hidden Files"}, menubar::Separator(),
                 {kcRefresh, L"Refresh"}, {kcCopyPath, L"Copy Folder Path"}};
    }
    Invalidate();
    popup_.Open(
        hwnd_, screen.x, screen.y, std::move(items),
        [this](unsigned cmd) {
            switch (cmd) {
                case kcOpen: OpenIndex(sel_.Focus() >= 0 ? sel_.Focus() : *sel_.Items().begin()); break;
                case kcCopy: CopyToClipboard(false); break;
                case kcCut: CopyToClipboard(true); break;
                case kcPaste: Paste(); break;
                case kcRename: if (!sel_.Empty()) BeginRename(*sel_.Items().begin()); break;
                case kcDelete: MoveToBin(); break;
                case kcCopyPath: CopyPathText(); break;
                case kcNewFolder: NewFolder(); break;
                case kcViewList: SetView(false); break;
                case kcViewIcons: SetView(true); break;
                case kcHidden:
                    prefs_.showHidden = !prefs_.showHidden;
                    SavePrefs(settingsPath_, prefs_);
                    Reload();
                    break;
                case kcRefresh: Reload(); break;
                default: break;
            }
        },
        [this] { popupClosedAt_ = GetTickCount64(); });
}

// ---- drag and drop ----
std::wstring FilesWindow::DropFolderAt(float x, float y, int* hotItem, int* hotSide) const {
    *hotItem = *hotSide = -1;
    const Hit h = HitTest(x, y);
    if (h.zone == Zone::Sidebar && h.index >= 0 && !side_[h.index].path.empty()) {
        *hotSide = h.index;
        return side_[h.index].path;
    }
    if (h.zone == Zone::Item && h.index >= 0 && entries_[h.index].isDir) {
        *hotItem = h.index;
        return entries_[h.index].path;
    }
    if (h.zone == Zone::Crumb) {
        auto crumbs = Breadcrumbs(dir_);
        if (h.index >= 0 && h.index < static_cast<int>(crumbs.size())) return crumbs[h.index].path;
    }
    return x >= kSidebarW && y >= kToolbarH ? dir_ : std::wstring{};
}

void FilesWindow::ExecuteDrop(IDataObject* data, const std::wstring& destDir, DWORD effect) {
    OpResult r = Transfer(data, destDir, effect == DROPEFFECT_MOVE, Opts());
    FinishOperation(r);
}

void FilesWindow::StartDrag() {
    const auto paths = SelectedPaths();
    dragPending_ = false;
    if (paths.empty()) return;
    auto data = MakeDataObject(paths);
    if (!data) return;
    if (GetCapture() == hwnd_) ReleaseCapture();
    DropSource source;
    DWORD effect = DROPEFFECT_NONE;
    dragging_ = true;
    DoDragDrop(data.Get(), &source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    dragging_ = false;
    mouseDown_ = false;
    Reload();  // a move out of this folder changed it
}

// ---- input ----
void FilesWindow::OnMouseDown(float x, float y, WPARAM keys, bool right) {
    if (renaming_ >= 0) {
        const Hit h = HitTest(x, y);
        if (h.zone == Zone::Item && h.index == renaming_) return;  // clicks inside the field are ignored (no mouse caret placement)
        CommitRename();
        if (renaming_ >= 0) return;
    }
    SetFocus(hwnd_);
    const Hit h = HitTest(x, y);
    const bool ctrl = (keys & MK_CONTROL) != 0, shift = (keys & MK_SHIFT) != 0;
    if (right) {
        POINT p{static_cast<LONG>(x * Scale()), static_cast<LONG>(y * Scale())};
        ClientToScreen(hwnd_, &p);
        if (h.zone == Zone::Item) {
            ShowContextMenu(p, h.index);
        } else if (h.zone == Zone::Blank && x >= kSidebarW) {
            sel_.Clear();
            ShowContextMenu(p, -1);
        }
        return;
    }
    mouseDown_ = true;
    deselectOnUp_ = false;
    dragPending_ = false;
    SetCapture(hwnd_);
    switch (h.zone) {
        case Zone::Item:
            if (sel_.Has(h.index) && !ctrl && !shift) {  // may be the start of a drag of the whole selection
                deselectOnUp_ = true;
                dragIndex_ = h.index;
                dragPending_ = true;
                dragStart_ = POINT{static_cast<LONG>(x), static_cast<LONG>(y)};
            } else {
                sel_.Click(h.index, ctrl, shift);
                dragIndex_ = h.index;
                dragPending_ = true;
                dragStart_ = POINT{static_cast<LONG>(x), static_cast<LONG>(y)};
            }
            break;
        case Zone::Blank:
            if (x >= kSidebarW) sel_.Clear();
            break;
        default: break;
    }
    Invalidate();
}

void FilesWindow::OnMouseUp(float x, float y, WPARAM keys) {
    const bool wasDown = mouseDown_;
    mouseDown_ = false;
    dragPending_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (!wasDown) return;
    const Hit h = HitTest(x, y);
    switch (h.zone) {
        case Zone::Close: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        case Zone::Minimize: ShowWindow(hwnd_, SW_MINIMIZE); break;
        case Zone::Zoom: ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE); break;
        case Zone::Back: GoBack(); break;
        case Zone::Forward: GoForward(); break;
        case Zone::ViewList: SetView(false); break;
        case Zone::ViewIcons: SetView(true); break;
        case Zone::Sidebar:
            if (h.index >= 0) Navigate(side_[h.index].path);
            break;
        case Zone::Header: SetSort(static_cast<SortKey>(h.index)); break;
        case Zone::Crumb: {
            auto crumbs = Breadcrumbs(dir_);
            if (h.index >= 0 && h.index < static_cast<int>(crumbs.size())) Navigate(crumbs[h.index].path, dir_);
            break;
        }
        case Zone::Item: {
            if (deselectOnUp_ && h.index == dragIndex_) sel_.Only(h.index);  // a click on the selection narrows it
            // Two quick plain clicks on the same item open it (the window class has no CS_DBLCLKS).
            const ULONGLONG now = GetTickCount64();
            const bool plain = !(keys & (MK_CONTROL | MK_SHIFT));
            if (plain && h.index == lastClickIndex_ && now - lastClickAt_ <= GetDoubleClickTime()) {
                lastClickAt_ = 0;
                lastClickIndex_ = -1;
                OpenIndex(h.index);
            } else {
                lastClickAt_ = now;
                lastClickIndex_ = h.index;
            }
            break;
        }
        default: break;
    }
    deselectOnUp_ = false;
    Invalidate();
}

void FilesWindow::OnMouseMove(float x, float y, WPARAM keys) {
    const Hit h = HitTest(x, y);
    if (h.zone != hover_.zone || h.index != hover_.index) {
        hover_ = h;
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        Invalidate();
    }
    if (dragPending_ && (keys & MK_LBUTTON)) {
        const int dx = static_cast<int>(x) - dragStart_.x, dy = static_cast<int>(y) - dragStart_.y;
        if (std::abs(dx) * Scale() > GetSystemMetrics(SM_CXDRAG) || std::abs(dy) * Scale() > GetSystemMetrics(SM_CYDRAG)) {
            deselectOnUp_ = false;
            StartDrag();
        }
    }
}

bool FilesWindow::OnKeyDown(WPARAM vk) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
    if (renaming_ >= 0) {
        switch (vk) {
            case VK_RETURN: CommitRename(); break;
            case VK_ESCAPE: CancelRename(); break;
            case VK_LEFT: rename_.Left(shift); break;
            case VK_RIGHT: rename_.Right(shift); break;
            case VK_HOME: rename_.Home(shift); break;
            case VK_END: rename_.End(shift); break;
            case VK_BACK: rename_.Backspace(); break;
            case VK_DELETE: rename_.Delete(); break;
            case 'A': if (ctrl) rename_.SelectAll(); break;
            case 'V': if (ctrl) rename_.InsertText(GetClipboardText(hwnd_)); break;
            case 'C': if (ctrl) SetClipboardText(hwnd_, rename_.SelectedText()); break;
            default: return true;
        }
        caretOn_ = true;
        Invalidate();
        return true;
    }
    const int n = static_cast<int>(entries_.size());
    auto move = [&](Nav nav) {
        const int page = std::max(1, static_cast<int>(ViewHeight() / (prefs_.iconView ? ComputeIconGrid(ViewWidth()).cellH : kRowH)));
        const int to = NextIndex(sel_.Focus(), nav, GridColumns(), n, page);
        sel_.Move(to, shift);
        EnsureVisible(to);
        Invalidate();
    };
    switch (vk) {
        case VK_UP: if (alt) GoUp(); else move(Nav::Up); return true;
        case VK_DOWN: if (ctrl && sel_.Focus() >= 0) OpenIndex(sel_.Focus()); else move(Nav::Down); return true;
        case VK_LEFT: if (alt) GoBack(); else move(Nav::Left); return true;
        case VK_RIGHT: if (alt) GoForward(); else move(Nav::Right); return true;
        case VK_HOME: move(Nav::Home); return true;
        case VK_END: move(Nav::End); return true;
        case VK_PRIOR: move(Nav::PageUp); return true;
        case VK_NEXT: move(Nav::PageDown); return true;
        case VK_RETURN:
            if (alt) return true;
            if (sel_.Items().size() == 1) OpenIndex(*sel_.Items().begin());
            else for (int i : std::set<int>(sel_.Items())) OpenIndex(i);
            return true;
        case VK_BACK: GoUp(); return true;
        case VK_ESCAPE: sel_.Clear(); typed_.clear(); Invalidate(); return true;
        case VK_F2: if (sel_.Items().size() == 1) BeginRename(*sel_.Items().begin()); return true;
        case VK_F5: Reload(); return true;
        case VK_DELETE: MoveToBin(); return true;
        case 'A': if (ctrl) SelectAllItems(); return true;
        case 'C': if (ctrl) CopyToClipboard(false); return true;
        case 'X': if (ctrl) CopyToClipboard(true); return true;
        case 'V': if (ctrl) Paste(); return true;
        case 'N': if (ctrl && shift) NewFolder(); return true;
        case 'W': if (ctrl) PostMessageW(hwnd_, WM_CLOSE, 0, 0); return true;
        case '1': if (ctrl) SetView(true); return true;
        case '2': if (ctrl) SetView(false); return true;
        case VK_OEM_PERIOD:
            if (ctrl && shift) {
                prefs_.showHidden = !prefs_.showHidden;
                SavePrefs(settingsPath_, prefs_);
                Reload();
            }
            return true;
        default: return false;
    }
}

void FilesWindow::OnChar(wchar_t c) {
    if (renaming_ >= 0) {
        if (rename_.Insert(c)) {
            caretOn_ = true;
            Invalidate();
        }
        return;
    }
    if (c < 32 || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) return;
    // Type-ahead: letters typed in quick succession jump to the first name that starts with them.
    const ULONGLONG now = GetTickCount64();
    if (now - typedAt_ > 1000) typed_.clear();
    typedAt_ = now;
    typed_.push_back(c);
    const std::wstring want = Lower(typed_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (Lower(entries_[i].name).compare(0, want.size(), want) == 0) {
            sel_.Only(static_cast<int>(i));
            EnsureVisible(static_cast<int>(i));
            Invalidate();
            return;
        }
    }
}

void FilesWindow::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- drawing ----
void FilesWindow::DrawItemIcon(ID2D1DeviceContext* dc, const Entry& e, const D2D1_RECT_F& r) {
    const IconKey key = IconKeyFor(e);
    const int tier = ui::IconTierFor((r.right - r.left) * Scale());
    const std::wstring cacheKey = key.key + L"@" + std::to_wstring(tier);
    ComPtr<ID2D1Bitmap> bmp;
    if (auto it = bitmaps_.find(cacheKey); it != bitmaps_.end()) {
        bmp = it->second;
    } else if (auto px = icons_->Find(key.key, tier)) {
        bmp = ui::MakeBitmap(dc, *px);
        bitmaps_[cacheKey] = bmp;
    } else if (!icons_->Failed(key.key, tier)) {
        icons_->Request(key.key, key.source, tier);
    }
    if (bmp) {
        ui::DrawIcon(dc, bmp.Get(), r);
        return;
    }
    // Not loaded yet or none exists: an original line glyph.
    ComPtr<ID2D1SolidColorBrush> b;
    const Colors c = ColorsFor(theme_);
    dc->CreateSolidColorBrush(e.isDir ? c.accent : c.dim, &b);
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, k = (r.right - r.left) / 18.0f;
    dc->SetTransform(D2D1::Matrix3x2F::Scale(k, k, D2D1::Point2F(cx, cy)));
    DrawGlyph(dc, b.Get(), e.isDir ? kFolder : kFileSheet, cx, cy, 1.4f);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
}

void FilesWindow::DrawRenameField(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, bool centered) {
    const Colors c = ColorsFor(theme_);
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c.content, &b);
    D2D1_RECT_F field = D2D1::RectF(r.left - 2, r.top + 2, r.right + 2, r.bottom - 2);
    dc->FillRoundedRectangle(D2D1::RoundedRect(field, 5, 5), b.Get());
    b->SetColor(c.accent);
    dc->DrawRoundedRectangle(D2D1::RoundedRect(field, 5, 5), b.Get(), 2.0f);
    IDWriteTextFormat* fmt = ui::Format(13);
    const std::wstring& text = rename_.Text();
    const float textW = ui::MeasureWidth(text, fmt);
    float x0 = centered ? std::max(field.left + 4, (field.left + field.right) / 2 - textW / 2) : r.left + 4;
    if (rename_.HasSelection()) {
        const float a = x0 + ui::MeasureWidth(text.substr(0, rename_.SelectionStart()), fmt);
        const float e = x0 + ui::MeasureWidth(text.substr(0, rename_.SelectionEnd()), fmt);
        b->SetColor(D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, 0.35f));
        dc->FillRectangle(D2D1::RectF(a, field.top + 4, e, field.bottom - 4), b.Get());
    }
    b->SetColor(c.text);
    ui::DrawLabel(dc, b.Get(), text, D2D1::RectF(x0, field.top, field.right - 4, field.bottom), fmt);
    if (caretOn_ && !rename_.HasSelection()) {
        const float cx = x0 + ui::MeasureWidth(text.substr(0, rename_.Caret()), fmt);
        dc->FillRectangle(D2D1::RectF(cx, field.top + 5, cx + 1.2f, field.bottom - 5), b.Get());
    }
}

void FilesWindow::DrawSidebar(ID2D1DeviceContext* dc) {
    const Colors c = ColorsFor(theme_);
    const float h = static_cast<float>(host_.Height()) / Scale();
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c.sidebar, &b);
    dc->FillRectangle(D2D1::RectF(0, 0, kSidebarW, h), b.Get());
    b->SetColor(c.line);
    dc->FillRectangle(D2D1::RectF(kSidebarW - 1, 0, kSidebarW, h), b.Get());

    // Traffic lights (Velora's own colours; grey while the window is inactive).
    const bool dark = theme_ == tokens::Theme::Dark;
    const D2D1_COLOR_F lights[3] = {D2D1::ColorF(0.90f, 0.33f, 0.29f), D2D1::ColorF(0.91f, 0.70f, 0.24f), D2D1::ColorF(0.30f, 0.75f, 0.36f)};
    const bool hoverGroup = hover_.zone == Zone::Close || hover_.zone == Zone::Minimize || hover_.zone == Zone::Zoom;
    for (int i = 0; i < 3; ++i) {
        const float cx = kTrafficLeft + kTrafficD / 2 + i * kTrafficPitch;
        b->SetColor(active_ ? lights[i] : (dark ? D2D1::ColorF(1, 1, 1, 0.20f) : D2D1::ColorF(0, 0, 0, 0.16f)));
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, kTrafficCenterY), kTrafficD / 2, kTrafficD / 2), b.Get());
        b->SetColor(D2D1::ColorF(0, 0, 0, 0.18f));
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, kTrafficCenterY), kTrafficD / 2 - 0.25f, kTrafficD / 2 - 0.25f), b.Get(), 0.5f);
        if (hoverGroup && active_) {  // glyphs appear while the pointer is over the group
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.55f));
            const float k = 3.0f;
            if (i == 0) {
                dc->DrawLine(D2D1::Point2F(cx - k, kTrafficCenterY - k), D2D1::Point2F(cx + k, kTrafficCenterY + k), b.Get(), 1.2f);
                dc->DrawLine(D2D1::Point2F(cx - k, kTrafficCenterY + k), D2D1::Point2F(cx + k, kTrafficCenterY - k), b.Get(), 1.2f);
            } else if (i == 1) {
                dc->DrawLine(D2D1::Point2F(cx - k, kTrafficCenterY), D2D1::Point2F(cx + k, kTrafficCenterY), b.Get(), 1.2f);
            } else {
                dc->DrawLine(D2D1::Point2F(cx - k, kTrafficCenterY), D2D1::Point2F(cx + k, kTrafficCenterY), b.Get(), 1.2f);
                dc->DrawLine(D2D1::Point2F(cx, kTrafficCenterY - k), D2D1::Point2F(cx, kTrafficCenterY + k), b.Get(), 1.2f);
            }
        }
    }

    dc->PushAxisAlignedClip(D2D1::RectF(0, kToolbarH, kSidebarW - 1, h), D2D1_ANTIALIAS_MODE_ALIASED);
    for (size_t i = 0; i < side_.size(); ++i) {
        const SideItem& s = side_[i];
        if (s.header) {
            b->SetColor(c.dim);
            ui::DrawLabel(dc, b.Get(), s.label, D2D1::RectF(20, s.y, kSidebarW - 12, s.y + 28), ui::Format(11, DWRITE_FONT_WEIGHT_SEMI_BOLD));
            continue;
        }
        const bool current = _wcsicmp(s.path.c_str(), dir_.c_str()) == 0 || _wcsicmp((s.path + L"\\").c_str(), dir_.c_str()) == 0;
        const bool hot = (hover_.zone == Zone::Sidebar && hover_.index == static_cast<int>(i)) || dropHotSide_ == static_cast<int>(i);
        const D2D1_ROUNDED_RECT row = D2D1::RoundedRect(D2D1::RectF(8, s.y + 1, kSidebarW - 9, s.y + kSidebarRow - 1), 7, 7);
        if (current || hot) {
            b->SetColor(dropHotSide_ == static_cast<int>(i) ? D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, 0.30f) : (current ? c.selInactive : c.hover));
            dc->FillRoundedRectangle(row, b.Get());
        }
        b->SetColor(current ? c.accent : c.dim);
        DrawGlyph(dc, b.Get(), s.glyph, 28, s.y + kSidebarRow / 2);
        b->SetColor(c.text);
        ui::DrawLabel(dc, b.Get(), s.label, D2D1::RectF(46, s.y, kSidebarW - 14, s.y + kSidebarRow), ui::Format(13, current ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL));
    }
    dc->PopAxisAlignedClip();
}

void FilesWindow::DrawToolbar(ID2D1DeviceContext* dc) {
    const Colors c = ColorsFor(theme_);
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c.line, &b);
    const float w = kSidebarW + ViewWidth();
    const float cy = (kToolbarH - kControlH) / 2;
    auto pill = [&](float x, float width, int zoneA, int zoneB) {
        const D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, cy, x + width, cy + kControlH), kControlH / 2, kControlH / 2);
        b->SetColor(theme_ == tokens::Theme::Dark ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.05f));
        dc->FillRoundedRectangle(r, b.Get());
        (void)zoneA;
        (void)zoneB;
    };
    const float bx = kSidebarW + 16;
    pill(bx, 2 * kControlH, 0, 0);
    auto glyphBtn = [&](float x, int glyph, bool enabled, bool hot, bool selected) {
        if (selected || hot) {
            b->SetColor(selected ? (theme_ == tokens::Theme::Dark ? D2D1::ColorF(1, 1, 1, 0.16f) : D2D1::ColorF(0, 0, 0, 0.10f)) : c.hover);
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 2, cy + 2, x + kControlH - 2, cy + kControlH - 2), (kControlH - 4) / 2, (kControlH - 4) / 2), b.Get());
        }
        b->SetColor(enabled ? c.text : D2D1::ColorF(c.dim.r, c.dim.g, c.dim.b, 0.4f));
        DrawGlyph(dc, b.Get(), glyph, x + kControlH / 2, cy + kControlH / 2, 1.6f);
    };
    glyphBtn(bx, kChevronLeft, history_.CanBack(), hover_.zone == Zone::Back && history_.CanBack(), false);
    glyphBtn(bx + kControlH, kChevronRight, history_.CanForward(), hover_.zone == Zone::Forward && history_.CanForward(), false);

    const float vx = w - 16 - 2 * kControlH;
    pill(vx, 2 * kControlH, 0, 0);
    glyphBtn(vx, kViewList, true, hover_.zone == Zone::ViewList, !prefs_.iconView);
    glyphBtn(vx + kControlH, kViewIcons, true, hover_.zone == Zone::ViewIcons, prefs_.iconView);

    b->SetColor(c.text);
    std::wstring title = LeafOf(dir_);
    if (title.empty()) title = dir_;
    ui::DrawLabel(dc, b.Get(), title, D2D1::RectF(bx + 2 * kControlH + 16, 0, vx - 16, kToolbarH), ui::Format(15, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    b->SetColor(c.line);
    dc->FillRectangle(D2D1::RectF(kSidebarW, kToolbarH - 1, w, kToolbarH), b.Get());
}

void FilesWindow::DrawStatus(ID2D1DeviceContext* dc) {
    const Colors c = ColorsFor(theme_);
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c.line, &b);
    const float w = kSidebarW + ViewWidth(), top = ViewBottom(), bottom = static_cast<float>(host_.Height()) / Scale();
    b->SetColor(c.content);
    dc->FillRectangle(D2D1::RectF(kSidebarW, top, w, bottom), b.Get());
    b->SetColor(c.line);
    dc->FillRectangle(D2D1::RectF(kSidebarW, top, w, top + 1), b.Get());

    // Breadcrumbs (click to jump up), the item count on the right.
    float x = kSidebarW + 12;
    auto crumbs = Breadcrumbs(dir_);
    IDWriteTextFormat* fmt = ui::Format(12);
    for (size_t i = 0; i < crumbs.size(); ++i) {
        const float tw = ui::MeasureWidth(crumbs[i].label, fmt) + 4;
        const bool hot = hover_.zone == Zone::Crumb && hover_.index == static_cast<int>(i);
        b->SetColor(hot ? c.text : c.dim);
        ui::DrawLabel(dc, b.Get(), crumbs[i].label, D2D1::RectF(x, top, x + tw, bottom), fmt);
        x += tw + 4;
        if (i + 1 < crumbs.size()) {
            DrawGlyph(dc, b.Get(), kChevronRight, x + 1.5f, (top + bottom) / 2, 1.0f);
            x += 8;
        } else {
            x += 8;
        }
        x += 0;
        if (x > w - 200) break;
    }
    std::wstring count;
    const size_t total = entries_.size(), chosen = sel_.Items().size();
    if (loading_) count = L"Loading…";
    else if (chosen == 0) count = std::to_wstring(total) + (total == 1 ? L" item" : L" items");
    else {
        std::uint64_t bytes = 0;
        for (int i : sel_.Items()) if (i < static_cast<int>(entries_.size()) && !entries_[i].isDir) bytes += entries_[i].size;
        count = std::to_wstring(chosen) + L" of " + std::to_wstring(total) + L" selected" + (bytes ? L", " + FormatSize(bytes) : L"");
    }
    b->SetColor(c.dim);
    ui::DrawLabel(dc, b.Get(), count, D2D1::RectF(w - 260, top, w - 14, bottom), fmt, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void FilesWindow::DrawContent(ID2D1DeviceContext* dc) {
    const Colors c = ColorsFor(theme_);
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c.content, &b);
    const float w = kSidebarW + ViewWidth(), top = ViewTop(), bottom = ViewBottom();
    dc->FillRectangle(D2D1::RectF(kSidebarW, kToolbarH, w, bottom), b.Get());
    const std::uint64_t now = NowFileTime();

    if (!prefs_.iconView) {  // column headers
        const Columns col = ComputeColumns(ViewWidth() - 16);
        float x = kSidebarW + 8;
        auto head = [&](const wchar_t* label, float width, SortKey key, bool right) {
            if (width <= 0) return;
            const bool sorted = prefs_.sort == key;
            b->SetColor(sorted ? c.text : c.dim);
            IDWriteTextFormat* f = ui::Format(11.5f, sorted ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
            ui::DrawLabel(dc, b.Get(), label, D2D1::RectF(x + (right ? 0 : (key == SortKey::Name ? 42.0f : 6.0f)), kToolbarH, x + width - (right ? 12 : 4), kToolbarH + kHeaderH), f,
                          right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
            if (sorted) {  // small chevron: up = ascending
                const float lw = ui::MeasureWidth(label, f);
                const float tx = right ? x + width - 12 - lw - 12 : x + (key == SortKey::Name ? 42.0f : 6.0f) + lw + 8;
                const float cy = kToolbarH + kHeaderH / 2, d = prefs_.ascending ? -1.0f : 1.0f;
                dc->DrawLine(D2D1::Point2F(tx - 3, cy - d * 1.5f), D2D1::Point2F(tx, cy + d * 1.5f), b.Get(), 1.2f);
                dc->DrawLine(D2D1::Point2F(tx, cy + d * 1.5f), D2D1::Point2F(tx + 3, cy - d * 1.5f), b.Get(), 1.2f);
            }
            x += width;
        };
        head(L"Name", col.name, SortKey::Name, false);
        head(L"Date Modified", col.modified, SortKey::Modified, false);
        head(L"Size", col.size, SortKey::Size, true);
        head(L"Kind", col.kind, SortKey::Kind, false);
        b->SetColor(c.line);
        dc->FillRectangle(D2D1::RectF(kSidebarW, kToolbarH + kHeaderH - 1, w, kToolbarH + kHeaderH), b.Get());
    }

    dc->PushAxisAlignedClip(D2D1::RectF(kSidebarW, top, w, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
    const int n = static_cast<int>(entries_.size());
    const Columns col = ComputeColumns(ViewWidth() - 16);
    IDWriteTextFormat* fmt = ui::Format(13);
    int first = 0, last = n;  // only the rows that can be on screen
    if (n > 0) {
        const float pitch = prefs_.iconView ? ComputeIconGrid(ViewWidth()).cellH : kRowH;
        const int perRow = prefs_.iconView ? GridColumns() : 1;
        first = std::clamp(static_cast<int>(scroll_ / pitch) * perRow - perRow, 0, n);
        last = std::clamp((static_cast<int>((scroll_ + ViewHeight()) / pitch) + 2) * perRow, 0, n);
    }
    for (int i = first; i < last; ++i) {
        const D2D1_RECT_F r = ItemRect(i);
        if (r.bottom < top || r.top > bottom) continue;
        const Entry& e = entries_[i];
        const bool selected = sel_.Has(i), hot = hover_.zone == Zone::Item && hover_.index == i, dropHot = dropHotItem_ == i;
        const bool dim = cut_.count(Lower(e.path)) != 0;
        const D2D1_COLOR_F fill = c.selActive;
        if (prefs_.iconView) {
            const D2D1_RECT_F cell = D2D1::RectF(r.left + 4, r.top + 2, r.right - 4, r.bottom - 2);
            const D2D1_RECT_F ic = D2D1::RectF((r.left + r.right) / 2 - kIconSize / 2, r.top + 8, (r.left + r.right) / 2 + kIconSize / 2, r.top + 8 + kIconSize);
            if (selected || hot || dropHot) {
                b->SetColor(dropHot ? D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, 0.30f) : (selected ? (active_ ? D2D1::ColorF(fill.r, fill.g, fill.b, 0.20f) : c.selInactive) : c.hover));
                dc->FillRoundedRectangle(D2D1::RoundedRect(cell, 9, 9), b.Get());
            }
            DrawItemIcon(dc, e, ic);
            if (renaming_ == i) {
                DrawRenameField(dc, NameRect(i), true);
            } else {
                const D2D1_RECT_F nr = NameRect(i);
                if (selected && active_) {
                    const float tw = std::min(ui::MeasureWidth(e.name, fmt) + 10, nr.right - nr.left);
                    b->SetColor(fill);
                    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF((nr.left + nr.right) / 2 - tw / 2, nr.top + 2, (nr.left + nr.right) / 2 + tw / 2, nr.top + 21), 5, 5), b.Get());
                    b->SetColor(c.onAccent);
                } else {
                    b->SetColor(dim ? D2D1::ColorF(c.text.r, c.text.g, c.text.b, 0.45f) : c.text);
                }
                ui::DrawLabel(dc, b.Get(), e.name, D2D1::RectF(nr.left, nr.top, nr.right, nr.top + 23), fmt, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            continue;
        }
        // List row
        const bool onAccent = selected && active_;
        if (selected || hot || dropHot) {
            b->SetColor(dropHot ? D2D1::ColorF(c.accent.r, c.accent.g, c.accent.b, 0.30f) : (selected ? (active_ ? fill : c.selInactive) : c.hover));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left, r.top + 1, r.right, r.bottom - 1), 6, 6), b.Get());
        }
        DrawItemIcon(dc, e, D2D1::RectF(r.left + 12, r.top + 4, r.left + 32, r.top + 24));
        const D2D1_COLOR_F textColor = onAccent ? c.onAccent : (dim ? D2D1::ColorF(c.text.r, c.text.g, c.text.b, 0.45f) : c.text);
        const D2D1_COLOR_F dimColor = onAccent ? D2D1::ColorF(1, 1, 1, 0.85f) : c.dim;
        float x = r.left;
        if (renaming_ == i) {
            DrawRenameField(dc, NameRect(i), false);
        } else {
            b->SetColor(textColor);
            ui::DrawLabel(dc, b.Get(), e.name, D2D1::RectF(r.left + 42, r.top, r.left + col.name - 8, r.bottom), fmt);
        }
        x += col.name;
        b->SetColor(dimColor);
        if (col.modified > 0) {
            ui::DrawLabel(dc, b.Get(), FormatModified(e.modified, now), D2D1::RectF(x + 6, r.top, x + col.modified - 4, r.bottom), ui::Format(12.5f));
            x += col.modified;
        }
        if (col.size > 0) {
            ui::DrawLabel(dc, b.Get(), e.isDir ? L"--" : FormatSize(e.size), D2D1::RectF(x, r.top, x + col.size - 12, r.bottom), ui::Format(12.5f),
                          DWRITE_TEXT_ALIGNMENT_TRAILING);
            x += col.size;
        }
        if (col.kind > 0) ui::DrawLabel(dc, b.Get(), KindOf(e), D2D1::RectF(x + 6, r.top, x + col.kind - 4, r.bottom), ui::Format(12.5f));
    }
    dc->PopAxisAlignedClip();

    // States.
    const float midX = (kSidebarW + w) / 2, midY = (top + bottom) / 2;
    std::wstring message;
    if (loading_ && entries_.empty()) message = L"Loading…";
    else if (listError_ != 0) message = listError_ == ERROR_ACCESS_DENIED ? L"You don't have permission to open this folder." : L"This folder can't be read.";
    else if (entries_.empty()) message = L"This folder is empty";
    if (!message.empty()) {
        b->SetColor(c.dim);
        ui::DrawLabel(dc, b.Get(), message, D2D1::RectF(midX - 240, midY - 20, midX + 240, midY + 20), ui::Format(14), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void FilesWindow::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const Colors c = ColorsFor(theme_);
    ID2D1DeviceContext* dc = host_.BeginDraw(c.content);
    if (!dc) return;
    ClampScroll();
    DrawContent(dc);
    DrawToolbar(dc);
    DrawStatus(dc);
    DrawSidebar(dc);
    if (host_.EndDraw()) {
        ++framesDrawn_;
    } else {
        log::Warn("files: present failed; recreating the render target");
        bitmaps_.clear();
        host_.Initialize(hwnd_);
        Invalidate();
    }
}

// ---- messages ----
LRESULT FilesWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const float s = Scale();
    switch (msg) {
        case WM_NCCREATE: break;
        case WM_CREATE: OnCreate(); return 0;
        case WM_NCCALCSIZE:
            if (wp) {  // the whole window is client area; a maximised window is pulled back inside the monitor
                if (IsZoomed(hwnd_)) {
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    const UINT d = GetDpiForWindow(hwnd_);
                    const int frame = GetSystemMetricsForDpi(SM_CXSIZEFRAME, d) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, d);
                    p->rgrc[0].left += frame;
                    p->rgrc[0].right -= frame;
                    p->rgrc[0].top += frame;
                    p->rgrc[0].bottom -= frame;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT wr{};
            GetWindowRect(hwnd_, &wr);
            const int b = static_cast<int>(kResizeBorder * s);
            if (!IsZoomed(hwnd_)) {
                const bool l = pt.x < wr.left + b, r = pt.x >= wr.right - b, t = pt.y < wr.top + b, bo = pt.y >= wr.bottom - b;
                if (t && l) return HTTOPLEFT;
                if (t && r) return HTTOPRIGHT;
                if (bo && l) return HTBOTTOMLEFT;
                if (bo && r) return HTBOTTOMRIGHT;
                if (l) return HTLEFT;
                if (r) return HTRIGHT;
                if (t) return HTTOP;
                if (bo) return HTBOTTOM;
            }
            const Hit h = HitTest((pt.x - wr.left) / s, (pt.y - wr.top) / s);
            return h.zone == Zone::Caption ? HTCAPTION : HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = POINT{static_cast<LONG>(560 * s), static_cast<LONG>(360 * s)};
            return 0;
        }
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
                ClampScroll();
                Invalidate();
            }
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            bitmaps_.clear();
            Invalidate();
            return 0;
        }
        case WM_SETTINGCHANGE:
            theme_ = ui::SystemTheme();
            Invalidate();
            return 0;
        case WM_DEVICECHANGE:
        case WM_DISPLAYCHANGE:
            ReloadSidebar();
            Invalidate();
            return 0;
        case WM_ACTIVATE:
            active_ = LOWORD(wp) != WA_INACTIVE;
            if (!active_ && !popup_.IsOpen()) CancelRename();
            Invalidate();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY: OnDestroy(); return 0;
        case WM_TIMER:
            if (wp == kTimerCaret) {
                caretOn_ = !caretOn_;
                Invalidate();
            } else if (wp == kTimerChange) {
                KillTimer(hwnd_, kTimerChange);
                if (renaming_ < 0 && !loading_) Reload();
            }
            return 0;
        case kMsgListed: OnListed(reinterpret_cast<ListMessage*>(lp)); return 0;
        case kMsgIcons: Invalidate(); return 0;
        case WM_COPYDATA: {
            auto* cd = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (cd && cd->dwData == kCopyDataOpen && cd->lpData) {
                std::wstring text(static_cast<const wchar_t*>(cd->lpData), cd->cbData / sizeof(wchar_t));
                while (!text.empty() && text.back() == L'\0') text.pop_back();
                const size_t nl = text.find(L'\n');
                Open(text.substr(0, nl), nl == std::wstring::npos ? std::wstring{} : text.substr(nl + 1));
                return TRUE;
            }
            return FALSE;
        }
        case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s, wp); return 0;
        case WM_MOUSELEAVE:
            hover_ = {};
            Invalidate();
            return 0;
        case WM_LBUTTONDOWN: OnMouseDown(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s, wp, false); return 0;
        case WM_LBUTTONUP: OnMouseUp(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s, wp); return 0;
        case WM_LBUTTONDBLCLK: return 0;
        case WM_RBUTTONDOWN:
            if (GetTickCount64() - popupClosedAt_ > 200) OnMouseDown(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s, wp, true);
            return 0;
        case WM_MOUSEWHEEL:
            scroll_ -= GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * (prefs_.iconView ? 80.0f : 3 * kRowH);
            ClampScroll();
            Invalidate();
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (OnKeyDown(wp)) return 0;
            break;
        case WM_CHAR: OnChar(static_cast<wchar_t>(wp)); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::files
