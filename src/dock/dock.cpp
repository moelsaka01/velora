#include "dock/dock.h"

#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "apps/app_index.h"
#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"
#include "ui/ui_common.h"

namespace velora::dock {

using Microsoft::WRL::ComPtr;

namespace {

constexpr float kPad = tokens::size::kDockPadding;
constexpr float kIcon = tokens::size::kDockIcon;
constexpr float kGap = 6.0f;
constexpr float kSepGap = 14.0f;
constexpr float kBelowIcon = 10.0f;  // room for the running dot
constexpr float kPillHeight = kPad + kIcon + kBelowIcon;
constexpr UINT_PTR kTimerFrame = 1;
constexpr UINT_PTR kTimerRebuild = 2;

constexpr PROPERTYKEY kPkeyAumid = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

std::uint64_t NowMs() { return GetTickCount64(); }
std::uint64_t ToInt(HWND h) { return static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(h)); }
HWND ToHwnd(std::uint64_t v) { return reinterpret_cast<HWND>(static_cast<UINT_PTR>(v)); }

std::filesystem::path SettingsPath() {
    auto dir = DataDir();
    return dir.empty() ? std::filesystem::path{} : dir / L"settings.json";
}

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::wstring WindowAumid(HWND h) {
    ComPtr<IPropertyStore> ps;
    if (FAILED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) return {};
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::wstring out;
    if (SUCCEEDED(ps->GetValue(kPkeyAumid, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) out = pv.pwszVal;
    PropVariantClear(&pv);
    return out;
}

DockApp FileApp(const std::wstring& name, const std::filesystem::path& exe) {
    DockApp a;
    a.name = name;
    a.launch = exe.wstring();
    a.icon = a.launch;
    a.exePath = a.launch;
    return a;
}

}  // namespace

DockGeometry ComputeGeometry(size_t itemCount, size_t pinnedCount) {
    DockGeometry g;
    pinnedCount = std::min(pinnedCount, itemCount);
    bool sep = pinnedCount > 0 && itemCount > pinnedCount;
    float x = kPad;
    for (size_t i = 0; i < itemCount; ++i) {
        if (sep && i == pinnedCount) {
            g.separatorX = x - kGap + kSepGap / 2;
            x += kSepGap;
        }
        g.itemX.push_back(x);
        x += kIcon + kGap;
    }
    g.pillWidth = itemCount ? x - kGap + kPad : 2 * kPad;
    g.pillHeight = kPillHeight;
    return g;
}

std::vector<DockApp> DefaultPinned() {
    std::vector<DockApp> list;
    DockApp apps;
    apps.name = L"Apps";
    apps.internalApps = true;
    list.push_back(apps);
    std::error_code ec;
    for (auto [name, exe] : {std::pair{L"Files", ExeDir() / L"velora-files.exe"}, std::pair{L"Settings", ExeDir() / L"velora-settings.exe"}}) {
        if (std::filesystem::exists(exe, ec)) list.push_back(FileApp(name, exe));
    }
    wchar_t win[MAX_PATH] = {};
    if (GetWindowsDirectoryW(win, MAX_PATH)) {
        auto explorer = std::filesystem::path(win) / L"explorer.exe";
        if (std::filesystem::exists(explorer, ec)) list.push_back(FileApp(L"File Explorer", explorer));
    }
    wchar_t pf[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH)) {
        auto edge = std::filesystem::path(pf) / L"Microsoft\\Edge\\Application\\msedge.exe";
        if (std::filesystem::exists(edge, ec)) list.push_back(FileApp(L"Microsoft Edge", edge));
    }
    return list;
}

std::vector<DockApp> LoadPinned() {
    auto path = SettingsPath();
    std::vector<DockApp> list;
    if (!path.empty() && PinnedFromJson(ReadFile(path), &list)) return list;
    return DefaultPinned();
}

bool SavePinned(const std::vector<DockApp>& pinned) {
    auto path = SettingsPath();
    if (path.empty()) return false;
    // Read-modify-write: other modules keep their sections of settings.json.
    json::Value root;
    if (auto old = json::Parse(ReadFile(path)); old && old->IsObject()) root = *old;
    auto dock = json::Parse(PinnedToJson(pinned));
    if (!dock) return false;
    root.Set("dock", *dock);
    auto tmp = path;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << root.Dump();
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

Dock::Dock(EventBus& bus, win::WindowTracker& tracker, Actions actions)
    : bus_(bus), tracker_(tracker), actions_(std::move(actions)) {
    pinned_ = LoadPinned();
    for (EventType t : {EventType::WindowCreated, EventType::WindowDestroyed, EventType::WindowActivated, EventType::WindowMinimized,
                        EventType::WindowRestored, EventType::WindowTitleChanged, EventType::WindowCloaked, EventType::WindowUncloaked})
        tokens_.push_back(bus_.Subscribe(t, [this](const Event&) { ScheduleRebuild(); }));
    Rebuild();
}

Dock::~Dock() {
    for (auto t : tokens_) bus_.Unsubscribe(t);
    Detach();
}

float Dock::ReservedDip() const { return kPillHeight + tokens::size::kDockBottomInset; }

sw::AppBarOptions Dock::Options() {
    sw::AppBarOptions o;
    o.composition = true;
    o.hook = [this](HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& r) { return OnMessage(h, m, w, l, r); };
    o.placement = [this](const RECT& reserved, UINT dpi) { return Placement(reserved, dpi); };
    return o;
}

RECT Dock::Placement(const RECT& reserved, UINT dpi) const {
    float s = ui::Scale(dpi);
    int w = std::min(static_cast<int>(std::lround(geo_.pillWidth * s)), static_cast<int>(reserved.right - reserved.left));
    int h = static_cast<int>(std::lround(geo_.pillHeight * s));
    int bottom = reserved.bottom - static_cast<int>(std::lround(tokens::size::kDockBottomInset * s));
    int left = reserved.left + (reserved.right - reserved.left - w) / 2;
    return RECT{left, bottom - h, left + w, bottom};
}

void Dock::ApplyPlacement() {
    if (!bar_ || !hwnd_) return;
    RECT w = Placement(bar_->Rect(), dpi_);
    SetWindowPos(hwnd_, nullptr, w.left, w.top, w.right - w.left, w.bottom - w.top, SWP_NOACTIVATE | SWP_NOZORDER);
}

// The acrylic backdrop fills the whole window; clip the window to the pill so the blur has rounded corners.
void Dock::ApplyRegion() {
    if (!hwnd_ || !backdrop_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int r = static_cast<int>(std::lround(tokens::radius::kLarge * ui::Scale(dpi_) * 2));
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, r, r), TRUE);  // the system owns the region
}

void Dock::Attach(sw::AppBar* bar) {
    if (hwnd_ || !bar || !bar->Hwnd()) return;
    bar_ = bar;
    hwnd_ = bar->Hwnd();
    dpi_ = GetDpiForWindow(hwnd_);
    if (!host_.Initialize(hwnd_)) {
        log::Warn("dock: render host unavailable; the Dock stays blank");
        hwnd_ = nullptr;
        bar_ = nullptr;
        return;
    }
    backdrop_ = ui::TransparencyEnabled() && render::ApplyBackdrop(hwnd_, render::Backdrop::Acrylic, ui::SystemTheme());
    if (!backdrop_) render::ApplyBackdrop(hwnd_, render::Backdrop::None, ui::SystemTheme());
    render::ApplyRoundedCorners(hwnd_, false);
    iconListener_ = ui::IconStore::Get().AddListener([this] { if (hwnd_) Draw(); });
    appear_ = 0;
    anim_.Start({0, 1, tokens::timing::kSlow, 0, tokens::timing::kDecelerate}, [this](float v, bool) { appear_ = v; }, NowMs());
    Rebuild();
    ApplyPlacement();
    ApplyRegion();
    Tick();
    log::Info("dock: attached, " + std::to_string(host_.Width()) + "x" + std::to_string(host_.Height()) + " px, " + std::to_string(items_.size()) + " items");
}

void Dock::Detach() {
    if (!hwnd_) return;
    ui::IconStore::Get().RemoveListener(iconListener_);
    KillTimer(hwnd_, kTimerFrame);
    KillTimer(hwnd_, kTimerRebuild);
    frameTimer_ = rebuildPending_ = false;
    bitmaps_.clear();
    host_.Shutdown();
    hwnd_ = nullptr;
    bar_ = nullptr;
}

void Dock::ScheduleRebuild() {
    if (!hwnd_ || rebuildPending_) return;
    rebuildPending_ = true;
    SetTimer(hwnd_, kTimerRebuild, 40, nullptr);  // coalesce bursts of window events
}

DockWindow Dock::Describe(HWND h, const win::TrackedWindow& w) {
    DockWindow d;
    d.hwnd = ToInt(h);
    d.title = w.title;
    d.minimized = w.minimized;
    d.controllable = w.controllable;
    d.foreground = GetForegroundWindow() == h;
    auto ex = exeCache_.find(w.pid);
    if (ex == exeCache_.end()) {
        if (exeCache_.size() > 512) exeCache_.clear();
        ex = exeCache_.emplace(w.pid, ui::ProcessImagePath(w.pid)).first;
    }
    d.exePath = ex->second;
    auto au = aumidCache_.find(d.hwnd);
    if (au == aumidCache_.end()) au = aumidCache_.emplace(d.hwnd, WindowAumid(h)).first;
    d.aumid = au->second;
    return d;
}

DockApp Dock::DescribeUnpinned(const DockWindow& w) {
    static std::vector<apps::AppEntry> snapshot;
    static unsigned generation = ~0u;
    if (generation != apps::AppIndex::Get().Generation()) {
        snapshot = apps::AppIndex::Get().Snapshot();
        generation = apps::AppIndex::Get().Generation();
    }
    DockApp a;
    a.aumid = w.aumid;
    a.exePath = w.exePath;
    // A window of a UWP app is owned by ApplicationFrameHost: only its AUMID identifies the app.
    const bool frameHost = ui::Lower(ui::StemOf(w.exePath)) == L"applicationframehost";
    auto take = [&](const apps::AppEntry& e) {
        a.name = e.name;
        a.launch = e.launch;
        a.icon = e.icon;
    };
    for (const auto& e : snapshot)  // AUMID first ...
        if (!w.aumid.empty() && !e.aumid.empty() && Ieq(w.aumid, e.aumid)) {
            take(e);
            return a;
        }
    for (const auto& e : snapshot)  // ... then the executable (a Win32 window may carry its own AUMID, e.g. Chrome)
        if (!frameHost && !w.exePath.empty() && Ieq(w.exePath, e.exePath)) {
            take(e);
            return a;
        }
    if (frameHost && !w.aumid.empty()) {
        a.name = w.title.empty() ? w.aumid : w.title;
        a.launch = L"shell:AppsFolder\\" + w.aumid;
        a.icon = a.launch;
    } else {
        // Not in the index: the executable itself still has a name and an icon.
        std::wstring desc = ui::FileDescription(w.exePath);
        a.name = desc.empty() ? ui::StemOf(w.exePath) : desc;
        a.launch = w.exePath;
        a.icon = w.exePath;
    }
    if (a.name.empty()) a.name = w.title;
    return a;
}

void Dock::Rebuild() {
    rebuildPending_ = false;
    std::vector<DockWindow> windows;
    std::vector<std::uint64_t> live;
    for (const auto& [h, w] : tracker_.Windows()) {
        windows.push_back(Describe(h, w));
        live.push_back(ToInt(h));
    }
    std::erase_if(aumidCache_, [&](const auto& kv) { return !std::binary_search(live.begin(), live.end(), kv.first); });

    items_ = BuildItems(pinned_, windows, [this](const DockWindow& w) { return DescribeUnpinned(w); });
    size_t before = geo_.itemX.size();
    float widthBefore = geo_.pillWidth;
    geo_ = ComputeGeometry(items_.size(), pinned_.size());
    hover_.assign(items_.size(), 0.0f);
    hoverIndex_ = -1;
    if (hwnd_) {
        if (before != geo_.itemX.size() || widthBefore != geo_.pillWidth) ApplyPlacement();
        Draw();
    }
}

int Dock::HitItem(float x, float y) const {
    if (y < 0 || y > geo_.pillHeight) return -1;
    for (size_t i = 0; i < geo_.itemX.size(); ++i)
        if (x >= geo_.itemX[i] - kGap / 2 && x < geo_.itemX[i] + kIcon + kGap / 2) return static_cast<int>(i);
    return -1;
}

ID2D1Bitmap* Dock::BitmapFor(ID2D1DeviceContext* dc, const std::wstring& icon) {
    if (icon.empty()) return nullptr;
    if (auto it = bitmaps_.find(icon); it != bitmaps_.end()) return it->second.Get();
    if (auto px = ui::IconStore::Get().Find(icon)) {
        auto bmp = ui::MakeBitmap(dc, *px);
        bitmaps_[icon] = bmp;
        return bmp.Get();
    }
    ui::IconStore::Get().Request(icon);
    return nullptr;
}

ID2D1Bitmap* Dock::VeloraBitmap(ID2D1DeviceContext* dc, const D2D1_RECT_F& dipRect) {
    int px = static_cast<int>(std::lround((dipRect.right - dipRect.left) * ui::Scale(dpi_)));
    std::wstring key = L"velora-app:" + std::to_wstring(px);
    if (auto it = bitmaps_.find(key); it != bitmaps_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap> bmp;
    ui::Pixels pixels;
    if (ui::LoadVeloraIcon(px, &pixels)) bmp = ui::MakeBitmap(dc, pixels);
    bitmaps_[key] = bmp;  // a null entry also stops retrying every frame
    return bmp.Get();
}

void Dock::Tick() {
    if (!hwnd_) return;
    bool more = anim_.Tick(NowMs());
    Draw();
    if (more && !frameTimer_) {
        SetTimer(hwnd_, kTimerFrame, 16, nullptr);
        frameTimer_ = true;
    } else if (!more && frameTimer_) {
        KillTimer(hwnd_, kTimerFrame);
        frameTimer_ = false;
    }
}

void Dock::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const tokens::Palette& p = tokens::PaletteFor(ui::SystemTheme());
    const bool translucent = ui::TransparencyEnabled() && host_.EffectsEnabled();
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(ui::Rgba(translucent ? p.surface : p.surfaceStrong, appear_ * (backdrop_ ? 0.55f : 1.0f)), &brush);
    float rise = (1.0f - appear_) * 24.0f;
    dc->SetTransform(D2D1::Matrix3x2F::Translation(0, rise));
    D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, geo_.pillWidth - 0.5f, geo_.pillHeight - 0.5f),
                                               tokens::radius::kLarge, tokens::radius::kLarge);
    dc->FillRoundedRectangle(pill, brush.Get());
    brush->SetColor(ui::Rgba(p.separator, appear_));
    dc->DrawRoundedRectangle(pill, brush.Get(), 1.0f);

    if (geo_.separatorX >= 0) dc->FillRectangle(D2D1::RectF(geo_.separatorX - 0.5f, 16, geo_.separatorX + 0.5f, geo_.pillHeight - 18), brush.Get());

    for (size_t i = 0; i < items_.size() && i < geo_.itemX.size(); ++i) {
        const DockItem& it = items_[i];
        float x = geo_.itemX[i], y = kPad;
        if (hover_[i] > 0.001f) {
            brush->SetColor(ui::Rgba(p.separator, hover_[i] * appear_));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 3, y - 3, x + kIcon + 3, y + kIcon + 3), 12, 12), brush.Get());
        }
        D2D1_RECT_F ir = D2D1::RectF(x + 2, y + 2, x + kIcon - 2, y + kIcon - 2);
        if (it.app.internalApps) {
            // The Apps item shows the Velora app icon, scaled to the device pixels of this display; the drawn
            // dot grid (original glyph) is the fallback when the resource is unavailable.
            if (ID2D1Bitmap* logo = VeloraBitmap(dc, ir)) {
                dc->DrawBitmap(logo, ir, appear_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                brush->SetColor(ui::Rgba(p.accent, appear_));
                dc->FillRoundedRectangle(D2D1::RoundedRect(ir, 12, 12), brush.Get());
                brush->SetColor(D2D1::ColorF(1, 1, 1, appear_));
                float cx = (ir.left + ir.right) / 2, cy = (ir.top + ir.bottom) / 2;
                for (int r = -1; r <= 1; ++r)
                    for (int c = -1; c <= 1; ++c) dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + c * 10, cy + r * 10), 3, 3), brush.Get());
            }
        } else if (ID2D1Bitmap* bmp = BitmapFor(dc, it.app.icon)) {
            dc->DrawBitmap(bmp, ir, appear_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            brush->SetColor(ui::Rgba(p.separator, appear_));
            dc->FillRoundedRectangle(D2D1::RoundedRect(ir, 12, 12), brush.Get());
            brush->SetColor(ui::Rgba(p.text, appear_));
            ui::DrawLabel(dc, brush.Get(), it.app.name.substr(0, 1), ir, ui::Format(20, DWRITE_FONT_WEIGHT_SEMI_BOLD), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (it.Running()) {
            brush->SetColor(ui::Rgba(it.Active() ? p.text : p.textSecondary, appear_));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + kIcon / 2, y + kIcon + 5), 2.5f, 2.5f), brush.Get());
        }
        if (it.AnyUncontrollable()) {  // elevated window: Velora cannot minimize or close it
            brush->SetColor(D2D1::ColorF(0.95f, 0.6f, 0.1f, appear_));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + kIcon - 4, y + 4), 4, 4), brush.Get());
        }
    }
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    if (!host_.EndDraw()) {
        log::Warn("dock: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        bitmaps_.clear();
    }
}

void Dock::OnClick(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    const DockItem& it = items_[index];
    ClickAction a = DecideClick(it);
    switch (a.kind) {
        case ClickKind::Launch:
            if (it.app.internalApps) {
                if (actions_.openApps) actions_.openApps();
            } else {
                apps::AppEntry e;
                e.name = it.app.name;
                e.launch = it.app.launch;
                apps::Launch(e);
            }
            break;
        case ClickKind::Activate: {
            HWND h = ToHwnd(a.hwnd);
            if (IsIconic(h)) PostMessageW(h, WM_SYSCOMMAND, SC_RESTORE, 0);
            if (!SetForegroundWindow(h)) log::Warn("dock: SetForegroundWindow was refused");
            break;
        }
        case ClickKind::Minimize:
            PostMessageW(ToHwnd(a.hwnd), WM_SYSCOMMAND, SC_MINIMIZE, 0);  // posted: a hung app cannot block the Dock
            break;
        case ClickKind::None:
            break;
    }
}

void Dock::Pin(const DockApp& app, bool pin) {
    if (pin) {
        if (std::none_of(pinned_.begin(), pinned_.end(), [&](const DockApp& a) { return a.name == app.name && a.launch == app.launch; }))
            pinned_.push_back(app);
    } else {
        std::erase_if(pinned_, [&](const DockApp& a) { return !a.internalApps && a.name == app.name && a.launch == app.launch; });
    }
    if (!SavePinned(pinned_)) log::Warn("dock: could not save the pinned list");
    Rebuild();
}

void Dock::ShowItemMenu(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    const DockItem it = items_[index];  // copy: the menu loop pumps messages and Rebuild may replace items_
    enum : UINT { kOpen = 1, kQuit, kPin, kWindowBase = 100 };
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kOpen, it.app.internalApps ? L"Open Apps" : (it.Running() ? L"New Window" : L"Open"));
    if (!it.windows.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < it.windows.size() && i < 12; ++i) {
            std::wstring t = it.windows[i].title.empty() ? L"(untitled)" : it.windows[i].title.substr(0, 60);
            AppendMenuW(menu, MF_STRING, kWindowBase + static_cast<UINT>(i), t.c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (it.AnyUncontrollable() ? MF_GRAYED : 0), kQuit, L"Quit");
    }
    if (!it.app.internalApps) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kPin, it.pinned ? L"Remove from Dock" : L"Keep in Dock");
    }
    RECT r{};
    GetWindowRect(hwnd_, &r);
    float s = ui::Scale(dpi_);
    SetForegroundWindow(hwnd_);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN | TPM_CENTERALIGN,
                              r.left + static_cast<int>((geo_.itemX[index] + kIcon / 2) * s), r.top, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == kOpen) {
        if (it.app.internalApps) {
            if (actions_.openApps) actions_.openApps();
        } else {
            apps::AppEntry e;
            e.name = it.app.name;
            e.launch = it.app.launch;
            apps::Launch(e);
        }
    } else if (cmd == kQuit) {
        for (const auto& w : it.windows)
            if (w.controllable) PostMessageW(ToHwnd(w.hwnd), WM_CLOSE, 0, 0);  // a polite close; the app may ask to save
    } else if (cmd == kPin) {
        Pin(it.app, !it.pinned);
    } else if (cmd >= kWindowBase && cmd - kWindowBase < it.windows.size()) {
        HWND h = ToHwnd(it.windows[cmd - kWindowBase].hwnd);
        if (IsIconic(h)) PostMessageW(h, WM_SYSCOMMAND, SC_RESTORE, 0);
        SetForegroundWindow(h);
    }
}

bool Dock::OnMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    if (!hwnd_ || h != hwnd_) return false;
    float s = ui::Scale(dpi_);
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
                ApplyRegion();
                Draw();
            }
            return false;
        case WM_DPICHANGED:
            dpi_ = HIWORD(wp);
            bitmaps_.clear();
            return false;
        case WM_TIMER:
            if (wp == kTimerFrame) {
                Tick();
                result = 0;
                return true;
            }
            if (wp == kTimerRebuild) {
                KillTimer(h, kTimerRebuild);
                Rebuild();
                result = 0;
                return true;
            }
            return false;
        case WM_MOUSEMOVE: {
            int idx = HitItem(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (idx != hoverIndex_) {
                auto fade = [this](int i, float to) {
                    if (i < 0 || i >= static_cast<int>(hover_.size())) return;
                    anim_.Start({hover_[i], to, tokens::timing::kFast, 0, tokens::timing::kStandard},
                                [this, i](float v, bool) { if (i < static_cast<int>(hover_.size())) hover_[i] = v; }, NowMs());
                };
                fade(hoverIndex_, 0.0f);
                fade(idx, 1.0f);
                hoverIndex_ = idx;
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                Tick();
            }
            return false;
        }
        case WM_MOUSELEAVE:
            if (hoverIndex_ >= 0 && hoverIndex_ < static_cast<int>(hover_.size())) {
                int i = hoverIndex_;
                anim_.Start({hover_[i], 0.0f, tokens::timing::kFast, 0, tokens::timing::kStandard},
                            [this, i](float v, bool) { if (i < static_cast<int>(hover_.size())) hover_[i] = v; }, NowMs());
            }
            hoverIndex_ = -1;
            Tick();
            return false;
        case WM_LBUTTONDOWN:
            pressIndex_ = HitItem(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            result = 0;
            return true;
        case WM_LBUTTONUP: {
            int idx = HitItem(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (idx >= 0 && idx == pressIndex_) OnClick(idx);
            pressIndex_ = -1;
            result = 0;
            return true;
        }
        case WM_RBUTTONUP: {
            ShowItemMenu(HitItem(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s));
            result = 0;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace velora::dock
