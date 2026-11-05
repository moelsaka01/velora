#include "dock/dock.h"

#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
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
#include "ui/backdrop.h"
#include "ui/ui_common.h"
#include "wallpaper/wallpaper.h"

namespace velora::dock {

using Microsoft::WRL::ComPtr;

namespace {

constexpr float kPad = tokens::size::kDockPadding;
constexpr float kIcon = tokens::size::kDockIcon;
constexpr float kShelf = tokens::dock::kShelfHeight;
constexpr UINT_PTR kTimerFrame = 1;
constexpr UINT_PTR kTimerRebuild = 2;
constexpr UINT_PTR kTimerWallpaper = 3;  // one shot after attach: Velora applies its wallpaper right after entering

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

// settings.json -> {"dock": {"magnification": false}}; on by default.
bool MagnificationSetting() {
    auto path = SettingsPath();
    if (path.empty()) return true;
    auto doc = json::Parse(ReadFile(path));
    const json::Value* dock = doc && doc->IsObject() ? doc->Find("dock") : nullptr;
    const json::Value* m = dock && dock->IsObject() ? dock->Find("magnification") : nullptr;
    return m ? m->AsBool(true) : true;
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
    json::Value merged;  // keep the other keys of the dock section (dock.magnification is written by Settings)
    if (const json::Value* old = root.Find("dock"); old && old->IsObject()) merged = *old;
    if (dock->IsObject())
        for (const auto& [k, v] : dock->AsObject()) merged.Set(k, v);
    root.Set("dock", merged.IsNull() ? *dock : merged);
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

namespace {
std::vector<Dock*>& AllDocks() {
    static std::vector<Dock*> docks;
    return docks;
}
}  // namespace

Dock::Dock(EventBus& bus, win::WindowTracker& tracker, Actions actions)
    : bus_(bus), tracker_(tracker), actions_(std::move(actions)) {
    AllDocks().push_back(this);
    pinned_ = LoadPinned();
    magnify_ = MagnificationSetting();
    tokens_.push_back(bus_.Subscribe(EventType::SettingsChanged, [this](const Event&) {
        const bool now = MagnificationSetting();
        if (now == magnify_) return;
        magnify_ = now;
        if (!magnify_) UpdateMagnification(false, 0);
        ScheduleRebuild();
    }));
    for (EventType t : {EventType::WindowCreated, EventType::WindowDestroyed, EventType::WindowActivated, EventType::WindowMinimized,
                        EventType::WindowRestored, EventType::WindowTitleChanged, EventType::WindowCloaked, EventType::WindowUncloaked})
        tokens_.push_back(bus_.Subscribe(t, [this](const Event&) { ScheduleRebuild(); }));
    Rebuild();
}

Dock::~Dock() {
    std::erase(AllDocks(), this);
    for (auto t : tokens_) bus_.Unsubscribe(t);
    Detach();
}

float Dock::ReservedDip() const { return kShelf + tokens::size::kDockBottomInset; }

sw::AppBarOptions Dock::Options() {
    sw::AppBarOptions o;
    o.composition = true;
    o.hook = [this](HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& r) { return OnMessage(h, m, w, l, r); };
    o.placement = [this](const RECT& reserved, UINT dpi) { return Placement(reserved, dpi); };
    return o;
}

// The window is the shelf plus the margin it stretches into and the headroom magnified icons grow into; its bottom
// edge is the bottom of the reserved band.
RECT Dock::Placement(const RECT& reserved, UINT dpi) const {
    float s = ui::Scale(dpi);
    int w = std::min(static_cast<int>(std::lround(WindowWidthDip() * s)), static_cast<int>(reserved.right - reserved.left));
    int h = static_cast<int>(std::lround(WindowHeightDip() * s));
    int left = reserved.left + (reserved.right - reserved.left - w) / 2;
    return RECT{left, reserved.bottom - h, left + w, reserved.bottom};
}

void Dock::ApplyPlacement() {
    if (!bar_ || !hwnd_) return;
    RECT w = Placement(bar_->Rect(), dpi_);
    SetWindowPos(hwnd_, nullptr, w.left, w.top, w.right - w.left, w.bottom - w.top, SWP_NOACTIVATE | SWP_NOZORDER);
}

// What part of the window takes the pointer (and shows pixels). At rest only the shelf and its shadow, so the headroom
// above and the margins beside it never block the windows underneath; while the Dock is hovered or magnified, the
// whole window.
void Dock::UpdateRegion(bool force) {
    if (!hwnd_) return;
    bool full = hoverActive_;
    for (float s : scale_)
        if (s > 1.004f) full = true;
    if (!force && regionSet_ && full == regionFull_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    float s = ui::Scale(dpi_);
    HRGN rgn = nullptr;
    if (full) {
        rgn = CreateRectRgn(0, 0, rc.right, rc.bottom);
    } else {
        const float pad = tokens::dock::kShadowPad, left = (WindowWidthDip() - baseGeo_.pillWidth) / 2;
        int r = static_cast<int>(std::lround((tokens::radius::kDockShelf + pad) * 2 * s));
        rgn = CreateRoundRectRgn(static_cast<int>(std::lround((left - pad) * s)), static_cast<int>(std::lround((headroom_ - pad) * s)),
                                 static_cast<int>(std::lround((left + baseGeo_.pillWidth + pad) * s)) + 1, rc.bottom + 1, r, r);
    }
    if (rgn) SetWindowRgn(hwnd_, rgn, FALSE);  // the system owns the region now
    regionFull_ = full;
    regionSet_ = true;
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
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, ui::SystemTheme());  // the material is drawn, see dock.h
    render::ApplyRoundedCorners(hwnd_, false);
    iconListener_ = ui::IconStore::Get().AddListener([this] { if (hwnd_) Draw(); });
    appear_ = 0;
    anim_.Start({0, 1, tokens::timing::kSlow, 0, tokens::timing::kDecelerate}, [this](float v, bool) { appear_ = v; }, NowMs());
    wallDirty_ = true;
    Rebuild();
    ApplyPlacement();
    UpdateRegion(true);
    SetTimer(hwnd_, kTimerWallpaper, 1500, nullptr);
    Tick();
    log::Info("dock: attached, " + std::to_string(host_.Width()) + "x" + std::to_string(host_.Height()) + " px, " + std::to_string(items_.size()) + " items");
}

void Dock::Detach() {
    if (!hwnd_) return;
    ui::IconStore::Get().RemoveListener(iconListener_);
    KillTimer(hwnd_, kTimerFrame);
    KillTimer(hwnd_, kTimerRebuild);
    KillTimer(hwnd_, kTimerWallpaper);
    frameTimer_ = rebuildPending_ = false;
    bitmaps_.clear();
    wall_.Reset();
    host_.Shutdown();
    hwnd_ = nullptr;
    bar_ = nullptr;
    regionSet_ = false;
}

void Dock::ReloadPinned() {
    pinned_ = LoadPinned();
    ScheduleRebuild();
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

// Crops the strip of wallpaper behind the window (the full monitor width, so moving or resizing the window only
// shifts the bitmap), blurs it and keeps it as a bitmap: the shelf paints it as its backdrop. Null (and a stronger
// veil) when the wallpaper is a colour, a slideshow or unreadable.
void Dock::RebuildBackdrop(ID2D1DeviceContext* dc) {
    wall_.Reset();
    wallDirty_ = false;
    if (!hwnd_ || !host_.Ready() || !dc) return;
    const std::uint64_t t0 = NowMs();
    RECT win{};
    GetWindowRect(hwnd_, &win);
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;
    std::wstring path;
    int position = 4;  // DWPOS_FILL
    if (!wallpaper::CurrentFor(mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom, &path, &position)) {
        log::Info("dock: no wallpaper image behind the Dock; the shelf uses a stronger veil instead of a blurred backdrop");
        return;
    }
    ui::Pixels px;
    const RECT strip{mi.rcMonitor.left, win.top, mi.rcMonitor.right, win.bottom};
    wallMonitorLeft_ = mi.rcMonitor.left;
    wallHeightPx_ = win.bottom - win.top;
    if (!ui::LoadWallpaperRegion(path, position == 2 /* DWPOS_STRETCH */, mi.rcMonitor, strip, strip.right - strip.left, strip.bottom - strip.top, &px)) {
        log::Warn("dock: could not read the wallpaper " + WideToUtf8(path) + "; stronger veil instead of a blurred backdrop");
        return;
    }
    BoxBlur(px.bgra, px.width, px.height, std::max(1, static_cast<int>(std::lround(tokens::dock::kBlurSigma * ui::Scale(dpi_)))));
    wall_ = ui::MakeBitmap(dc, px);
    log::Info("dock: backdrop " + std::to_string(px.width) + "x" + std::to_string(px.height) + " px from the wallpaper, " +
              std::to_string(NowMs() - t0) + " ms");
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

    std::error_code ec;
    auto visible = VisiblePinned(pinned_, [&](const std::wstring& p) { return std::filesystem::exists(p, ec); });
    if (visible.size() != hiddenLogged_) {
        hiddenLogged_ = visible.size();
        log::Info("dock: " + std::to_string(pinned_.size() - visible.size()) + " pinned app(s) hidden (missing file or module not at G1)");
    }
    items_ = BuildItems(visible, windows, [this](const DockWindow& w) { return DescribeUnpinned(w); });
    pinnedCount_ = visible.size();
    const float winW = WindowWidthDip(), winH = WindowHeightDip();
    baseGeo_ = ComputeGeometry(items_.size(), pinnedCount_);
    // Window margins: room for the widest the shelf gets while magnified, and for icons that overshoot the peak.
    const float peak = anim_.Enabled() && magnify_ ? tokens::dock::kMagnifyPeak : 1.0f;
    sideMargin_ = std::ceil(MaxGrowth(items_.size(), pinnedCount_, peak) / 2) + tokens::dock::kShadowPad;
    headroom_ = std::max(tokens::dock::kShadowPad, kIcon * peak * 1.08f - IconBaseline() + 4.0f);
    if (scale_.size() != items_.size()) {  // the item set changed: start from rest
        for (auto id : scaleAnim_)
            if (id) anim_.Cancel(id);
        scale_.assign(items_.size(), 1.0f);
        scaleTarget_.assign(items_.size(), 1.0f);
        scaleAnim_.assign(items_.size(), 0);
    }
    geo_ = ComputeGeometry(items_.size(), pinnedCount_, &scale_);
    hoverIndex_ = -1;
    if (hwnd_) {
        if (winW != WindowWidthDip() || winH != WindowHeightDip()) ApplyPlacement();
        UpdateRegion(true);
        Draw();
    }
}

int Dock::HitItem(float x, float y) const {
    const float sy = headroom_, base = sy + IconBaseline();
    if (y > sy + kShelf) return -1;
    const float sx = ShelfLeft(), half = tokens::dock::kSpacing / 2;
    for (size_t i = 0; i < geo_.itemX.size(); ++i)
        if (x >= sx + geo_.itemX[i] - half && x < sx + geo_.itemX[i] + geo_.itemSize[i] + half && y >= base - geo_.itemSize[i] - 4.0f)
            return static_cast<int>(i);
    return -1;
}

std::set<std::wstring> Dock::IconSources() const {
    std::set<std::wstring> out;
    for (const auto& it : items_)
        if (!it.app.icon.empty()) out.insert(it.app.icon);
    return out;
}

ID2D1Bitmap* Dock::BitmapFor(ID2D1DeviceContext* dc, const std::wstring& icon) {
    if (icon.empty()) return nullptr;
    if (auto it = bitmaps_.find(icon); it != bitmaps_.end()) return it->second.Get();
    // Decoded for the largest size the icon is ever drawn at (fully magnified), so magnifying never upscales.
    const int tier = ui::IconTierFor(tokens::size::kDockIconMax * ui::Scale(dpi_));
    if (auto px = ui::IconStore::Get().Find(icon, tier)) {
        auto bmp = ui::MakeBitmap(dc, *px);
        bitmaps_[icon] = bmp;
        return bmp.Get();
    }
    ui::IconStore::Get().Request(icon, tier);
    return nullptr;
}

ID2D1Bitmap* Dock::VeloraBitmap(ID2D1DeviceContext* dc, int tierPx) {
    std::wstring key = L"velora-app:" + std::to_wstring(tierPx);
    if (auto it = bitmaps_.find(key); it != bitmaps_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap> bmp;
    ui::Pixels pixels;
    if (ui::LoadVeloraIcon(tierPx, &pixels)) bmp = ui::MakeBitmap(dc, pixels);
    bitmaps_[key] = bmp;  // a null entry also stops retrying every frame
    return bmp.Get();
}

void Dock::Tick() {
    if (!hwnd_) return;
    bool more = anim_.Tick(NowMs());
    geo_ = ComputeGeometry(items_.size(), pinnedCount_, &scale_);
    UpdateRegion(false);
    Draw();
    if (more && !frameTimer_) {
        SetTimer(hwnd_, kTimerFrame, 16, nullptr);
        frameTimer_ = true;
    } else if (!more && frameTimer_) {
        KillTimer(hwnd_, kTimerFrame);
        frameTimer_ = false;
    }
}

// One spring per icon, retargeted as the pointer moves (an interrupted spring keeps its velocity).
void Dock::SetScaleTargets(const std::vector<float>& targets) {
    if (targets.size() != scale_.size()) return;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (std::fabs(targets[i] - scaleTarget_[i]) < 0.002f) continue;
        scaleTarget_[i] = targets[i];
        if (scaleAnim_[i] && anim_.Retarget(scaleAnim_[i], targets[i])) continue;
        scaleAnim_[i] = anim_.StartSpring(scale_[i], targets[i], tokens::timing::kDockMagnify,
                                          [this, i](float v, bool) { if (i < scale_.size()) scale_[i] = v; }, NowMs());
    }
}

void Dock::UpdateMagnification(bool active, float pointerX) {
    hoverActive_ = active && anim_.Enabled() && magnify_;
    std::vector<float> targets(items_.size(), 1.0f);
    if (hoverActive_)
        targets = MagnifyScales(items_.size(), pinnedCount_, pointerX - WindowWidthDip() / 2, tokens::dock::kMagnifyPeak);
    SetScaleTargets(targets);
    // Frames are drawn by the timer, not per mouse event: a present waits for the display and would stall input.
    if (anim_.ActiveCount() > 0 && !frameTimer_) {
        SetTimer(hwnd_, kTimerFrame, 16, nullptr);
        frameTimer_ = true;
    }
}

void Dock::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const tokens::Theme theme = ui::SystemTheme();
    const tokens::Palette& p = tokens::PaletteFor(theme);
    const tokens::Glass& glass = tokens::GlassFor(theme);
    const bool translucent = ui::TransparencyEnabled() && host_.EffectsEnabled();
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    RECT win{};
    GetWindowRect(hwnd_, &win);
    if (translucent && (wallDirty_ || (wall_ && win.bottom - win.top != wallHeightPx_))) RebuildBackdrop(dc);
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);

    const float s = ui::Scale(dpi_);
    const float sx = ShelfLeft(), sy = headroom_, radius = tokens::radius::kDockShelf;
    dc->SetTransform(D2D1::Matrix3x2F::Translation(0, (1.0f - appear_) * kShelf * 0.6f));  // rises into place
    const D2D1_RECT_F shelfRect = D2D1::RectF(sx, sy, sx + geo_.pillWidth, sy + kShelf);

    // Soft shadow: nested rounded rectangles that fade with distance (about a 12 DIP blur, 4 DIP down).
    {
        constexpr int kLayers = 8;
        const float strength = static_cast<float>((glass.shadow.argb >> 24) & 0xFF) / 255.0f * appear_;
        const float a = 1.0f - std::pow(1.0f - std::min(strength, 0.95f), 1.0f / kLayers);
        for (int k = kLayers; k >= 1; --k) {
            const float grow = tokens::dock::kShadowPad * k / kLayers;
            D2D1_RECT_F r = D2D1::RectF(shelfRect.left - grow, shelfRect.top - grow + 4, shelfRect.right + grow, shelfRect.bottom + grow + 4);
            brush->SetColor(D2D1::ColorF(static_cast<float>((glass.shadow.argb >> 16) & 0xFF) / 255.0f, static_cast<float>((glass.shadow.argb >> 8) & 0xFF) / 255.0f,
                                         static_cast<float>(glass.shadow.argb & 0xFF) / 255.0f, a));
            dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius + grow, radius + grow), brush.Get());
        }
    }

    const D2D1_ROUNDED_RECT shelf = D2D1::RoundedRect(shelfRect, radius, radius);
    if (translucent) {
        bool blurred = false;
        if (wall_) {
            ComPtr<ID2D1BitmapBrush> bb;
            if (SUCCEEDED(dc->CreateBitmapBrush(wall_.Get(), &bb))) {
                float bx = 96, by = 96;
                wall_->GetDpi(&bx, &by);  // map the bitmap's pixels onto device pixels whatever DPI it was created with
                bb->SetTransform(D2D1::Matrix3x2F::Scale(bx / static_cast<float>(dpi_), by / static_cast<float>(dpi_)) *
                                 D2D1::Matrix3x2F::Translation(-static_cast<float>(win.left - wallMonitorLeft_) / s, 0));
                bb->SetOpacity(appear_);
                dc->FillRoundedRectangle(shelf, bb.Get());
                blurred = true;
            }
        }
        const float veil = blurred ? (theme == tokens::Theme::Dark ? tokens::dock::kVeilAlphaDark : tokens::dock::kVeilAlphaLight)
                                   : tokens::dock::kVeilAlphaUnblurred;
        brush->SetColor(ui::Rgba(glass.veil, veil * appear_));
        dc->FillRoundedRectangle(shelf, brush.Get());
    } else {
        brush->SetColor(ui::Rgba(p.surfaceStrong, appear_));  // transparency effects off / gaming mode: opaque
        dc->FillRoundedRectangle(shelf, brush.Get());
    }
    // Rim: a 1 DIP inner line, brighter at the top.
    {
        D2D1_GRADIENT_STOP stops[2] = {{0.0f, ui::Rgba(glass.rimTop, appear_)}, {1.0f, ui::Rgba(glass.rimBottom, appear_)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1LinearGradientBrush> rim;
        if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 2, &collection)) &&
            SUCCEEDED(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, sy), D2D1::Point2F(0, sy + kShelf)),
                                                    collection.Get(), &rim)))
            dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx + 0.5f, sy + 0.5f, sx + geo_.pillWidth - 0.5f, sy + kShelf - 0.5f), radius - 0.5f, radius - 0.5f),
                                     rim.Get(), 1.0f);
    }

    if (geo_.separatorX >= 0) {  // hairline between the pinned apps and the rest
        brush->SetColor(ui::Rgba(p.text, 0.18f * appear_));
        dc->FillRectangle(D2D1::RectF(sx + geo_.separatorX - 0.5f, sy + kPad, sx + geo_.separatorX + 0.5f, sy + IconBaseline()), brush.Get());
    }

    const int veloraTier = ui::IconTierFor(tokens::size::kDockIconMax * s);
    for (size_t i = 0; i < items_.size() && i < geo_.itemX.size(); ++i) {
        const DockItem& it = items_[i];
        const float size = geo_.itemSize[i], x = sx + geo_.itemX[i], bottom = sy + IconBaseline();
        const D2D1_RECT_F ir = D2D1::RectF(x, bottom - size, x + size, bottom);
        if (it.app.internalApps) {
            // The Apps item shows the Velora app icon; the drawn dot grid (original glyph) is the fallback when the
            // resource is unavailable.
            if (ID2D1Bitmap* logo = VeloraBitmap(dc, veloraTier)) {
                ui::DrawIcon(dc, logo, ir, appear_);
            } else {
                brush->SetColor(ui::Rgba(p.accent, appear_));
                dc->FillRoundedRectangle(D2D1::RoundedRect(ir, size * tokens::dock::kIconRadiusFraction, size * tokens::dock::kIconRadiusFraction), brush.Get());
                brush->SetColor(D2D1::ColorF(1, 1, 1, appear_));
                float cx = (ir.left + ir.right) / 2, cy = (ir.top + ir.bottom) / 2, step = size * 0.22f;
                for (int r = -1; r <= 1; ++r)
                    for (int c = -1; c <= 1; ++c) dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + c * step, cy + r * step), size * 0.07f, size * 0.07f), brush.Get());
            }
        } else if (ID2D1Bitmap* bmp = BitmapFor(dc, it.app.icon)) {
            ui::DrawIcon(dc, bmp, ir, appear_);
        } else if (ui::IconStore::Get().Failed(it.app.icon, ui::IconTierFor(tokens::size::kDockIconMax * s))) {
            ui::DrawFallbackTile(dc, ir, it.app.name, appear_);  // no icon exists anywhere (not even a generic one)
        } else {
            brush->SetColor(ui::Rgba(p.separator, appear_));  // placeholder until the icon arrives
            dc->FillRoundedRectangle(D2D1::RoundedRect(ir, size * tokens::dock::kIconRadiusFraction, size * tokens::dock::kIconRadiusFraction), brush.Get());
        }
        if (it.Running()) {
            brush->SetColor(ui::Rgba(p.text, (it.Active() ? 0.9f : 0.55f) * appear_));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + size / 2, sy + DotCentre()), tokens::dock::kDot / 2, tokens::dock::kDot / 2), brush.Get());
        }
        if (it.AnyUncontrollable()) {  // elevated window: Velora cannot minimize or close it
            brush->SetColor(D2D1::ColorF(0.95f, 0.6f, 0.1f, appear_));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ir.right - 4, ir.top + 4), 4, 4), brush.Get());
        }
    }
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    if (!host_.EndDraw()) {
        log::Warn("dock: present failed; recreating the render target");
        host_.Initialize(hwnd_);
        bitmaps_.clear();
        wall_.Reset();
        wallDirty_ = true;
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
    for (Dock* other : AllDocks())
        if (other != this) other->ReloadPinned();  // one Dock per monitor share one list
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
                              r.left + static_cast<int>((ShelfLeft() + geo_.itemX[index] + geo_.itemSize[index] / 2) * s),
                              r.top + static_cast<int>((headroom_ + IconBaseline() - geo_.itemSize[index] - 6.0f) * s), 0, hwnd_, nullptr);
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
                UpdateRegion(true);
                Draw();
            }
            return false;
        case WM_DPICHANGED:
            dpi_ = HIWORD(wp);
            bitmaps_.clear();
            wallDirty_ = true;
            return false;
        case WM_SETTINGCHANGE:  // theme, transparency or wallpaper changed
            wallDirty_ = true;
            Draw();
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
            if (wp == kTimerWallpaper) {
                KillTimer(h, kTimerWallpaper);
                wallDirty_ = true;
                Draw();
                result = 0;
                return true;
            }
            return false;
        case WM_MOUSEMOVE: {
            const float mx = GET_X_LPARAM(lp) / s, my = GET_Y_LPARAM(lp) / s;
            if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                tracking_ = TrackMouseEvent(&t) != FALSE;
            }
            hoverIndex_ = HitItem(mx, my);
            // The Dock magnifies while the pointer is on the shelf or just above it (where the icons grow into).
            UpdateMagnification(my >= headroom_ - 24.0f, mx);
            return false;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            hoverIndex_ = -1;
            UpdateMagnification(false, 0);
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
