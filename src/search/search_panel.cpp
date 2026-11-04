#include "search/search_panel.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "apps/files_launch.h"
#include "core/log.h"
#include "core/text.h"
#include "ui/backdrop.h"
#include "ui/ui_common.h"

namespace velora::search {

using Microsoft::WRL::ComPtr;
namespace sp = velora::tokens::searchpanel;

namespace {

constexpr wchar_t kClass[] = L"VeloraSearchPanel";
constexpr UINT_PTR kTimerFrame = 1, kTimerCaret = 2, kTimerQuery = 3;
constexpr UINT kMsgFiles = WM_APP + 60;
constexpr float kNoteHeight = 30.0f, kEmptyRow = 44.0f;
constexpr int kBackdropMaxPx = 640;

std::uint64_t NowMs() { return GetTickCount64(); }

ATOM RegisterOnce(HINSTANCE inst, WNDPROC proc) {
    static ATOM atom = [&] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        return RegisterClassExW(&wc);
    }();
    return atom;
}

std::wstring FolderOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

}  // namespace

PanelHooks DefaultHooks() {
    PanelHooks h;
    h.apps = [] { return apps::AppIndex::Get().Snapshot(); };
    h.launchApp = [](const apps::AppEntry& a) { apps::Launch(a); };
    h.openPath = [](const std::wstring& path, bool folder) {
        if (folder) {
            if (!apps::LaunchFilesApp(path, L"")) ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, FolderOf(path).c_str(), SW_SHOWNORMAL);
    };
    h.reveal = [](const std::wstring& path) {
        if (!apps::LaunchFilesApp(L"", path)) {  // no Files app next to the shell: Explorer can do it
            const std::wstring args = L"/select,\"" + path + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
    };
    return h;
}

SearchPanel::SearchPanel(HINSTANCE instance, PanelHooks hooks) : instance_(instance), hooks_(std::move(hooks)) {}

SearchPanel::~SearchPanel() { Hide(); }

void SearchPanel::Toggle() {
    if (hwnd_) Close();
    else Show();
}

void SearchPanel::Show() {
    if (hwnd_) return;
    if (!RegisterOnce(instance_, &SearchPanel::WndProc)) return;
    POINT pt{};
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Velora Search", WS_POPUP, mi.rcMonitor.left,
                            mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        log::Error("search: CreateWindowEx failed");
        return;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    if (!host_.Initialize(hwnd_)) {
        log::Warn("search: render host unavailable");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return;
    }
    render::ApplyBackdrop(hwnd_, render::Backdrop::None, ui::SystemTheme());
    render::ApplyRoundedCorners(hwnd_, false);

    edit_.Clear();
    apps_ = hooks_.apps ? hooks_.apps() : std::vector<apps::AppEntry>{};
    appResults_.clear();
    fileResults_.clear();
    results_.clear();
    rows_.clear();
    layout_ = {};
    selected_ = hover_ = -1;
    scroll_ = 0;
    filesState_ = IndexState::Ready;
    filesAnswered_ = false;
    filesQuery_.clear();
    caretOn_ = true;
    searcher_ = std::make_unique<FileSearcher>(hwnd_, kMsgFiles, hooks_.fileQuery);
    listener_ = ui::IconStore::Get().AddListener([this] { OnDataChanged(); });

    closing_ = closeDone_ = wallTried_ = false;
    wasActive_ = false;
    StartOpen();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    SetTimer(hwnd_, kTimerCaret, 530, nullptr);
    Tick();
}

void SearchPanel::OnSpring(float v, bool done) {
    open_ = std::clamp(v, 0.0f, 1.0f);
    if (done && closing_) closeDone_ = true;
}

void SearchPanel::StartOpen() {
    open_ = 0;
    spring_ = anim_.StartSpring(0, 1, tokens::timing::kPanelOpen, [this](float v, bool done) { OnSpring(v, done); }, NowMs());
}

void SearchPanel::Close() {
    if (!hwnd_ || closing_) return;
    closing_ = true;
    if (!anim_.Enabled()) {
        Hide();
        return;
    }
    if (!anim_.Retarget(spring_, 0))
        spring_ = anim_.StartSpring(open_, 0, tokens::timing::kPanelOpen, [this](float v, bool done) { OnSpring(v, done); }, NowMs());
    Tick();
}

void SearchPanel::Hide() {
    if (!hwnd_) return;
    ui::IconStore::Get().RemoveListener(listener_);
    anim_.Cancel(spring_);
    HWND h = hwnd_;
    hwnd_ = nullptr;  // before DestroyWindow: WM_ACTIVATE during teardown must not re-enter
    KillTimer(h, kTimerFrame);
    KillTimer(h, kTimerCaret);
    KillTimer(h, kTimerQuery);
    timerOn_ = false;
    searcher_.reset();  // a query in flight finishes in the background and is dropped
    bitmaps_.clear();
    wall_.Reset();
    host_.Shutdown();
    DestroyWindow(h);
    apps_.clear();
    appResults_.clear();
    fileResults_.clear();
    results_.clear();
    rows_.clear();
    layout_ = {};
    if (onHidden_) onHidden_();
}

void SearchPanel::SetQuery(const std::wstring& text) {
    if (!hwnd_) return;
    edit_.SetText(text);
    Refresh();
    Draw();
}

SearchPanel::Snapshot SearchPanel::GetSnapshot() const {
    Snapshot s;
    s.query = edit_.Text();
    s.results = static_cast<int>(results_.size());
    if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && !rows_[selected_].header) {
        s.selectedResult = rows_[selected_].result;
        s.selectedName = results_[s.selectedResult].name;
    }
    s.files = filesState_;
    s.filesAnswered = filesAnswered_;
    s.fileResults = static_cast<int>(fileResults_.size());
    if (HasNote()) s.note = NoteText();
    return s;
}

// ---- results ----
void SearchPanel::Refresh() {
    const std::wstring key = KeyOf(selected_);
    const std::wstring q = edit_.Text();
    appResults_.clear();
    if (!q.empty()) {
        const std::vector<size_t> hits = apps::Filter(apps_, q);
        for (size_t i : hits) {
            const apps::AppEntry& a = apps_[i];
            Result r;
            r.category = Category::App;
            r.name = a.name;
            r.detail = L"Application";
            r.path = a.exePath;
            r.appIndex = static_cast<int>(i);
            r.icon = a.icon;
            r.score = apps::MatchScore(q, a.name);
            appResults_.push_back(std::move(r));
            if (appResults_.size() >= 12) break;
        }
    }
    if (q.empty()) {
        fileResults_.clear();
        filesQuery_.clear();
        filesAnswered_ = false;
        KillTimer(hwnd_, kTimerQuery);
    } else if (q != filesQuery_) {
        SetTimer(hwnd_, kTimerQuery, 120, nullptr);  // debounce: the index is asked once typing pauses
    }
    RebuildRows(key);
}

void SearchPanel::RequestFiles() {
    KillTimer(hwnd_, kTimerQuery);
    const std::wstring q = edit_.Text();
    const std::wstring sql = BuildFileSql(q, 40);
    filesQuery_ = q;
    if (sql.empty()) {  // too short to search files
        fileResults_.clear();
        filesAnswered_ = true;
        RebuildRows(KeyOf(selected_));
        Draw();
        return;
    }
    filesAnswered_ = false;
    if (searcher_) searcher_->Request(++requestId_, sql);
}

void SearchPanel::OnFilesReady() {
    QueryResult r;
    if (!searcher_ || !searcher_->Take(&r)) return;
    if (r.id != requestId_) return;  // the query moved on
    filesState_ = r.state;
    filesAnswered_ = true;
    fileResults_.clear();
    const std::wstring q = filesQuery_;
    for (const FileHit& h : r.hits) {
        if (IsNoiseFilePath(h.path)) continue;
        Result res;
        res.category = ClassifyFile(h.name, h.type);
        res.name = h.name.empty() ? h.path.substr(h.path.find_last_of(L"\\/") + 1) : h.name;
        res.path = h.path;
        res.detail = FolderOf(h.path);
        res.icon = L"raw:" + h.path;
        res.score = std::max(1, apps::MatchScore(q, res.name));
        fileResults_.push_back(std::move(res));
    }
    RebuildRows(KeyOf(selected_));
    Draw();
}

std::wstring SearchPanel::KeyOf(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[row].header) return {};
    const Result& r = results_[rows_[row].result];
    return std::to_wstring(static_cast<int>(r.category)) + L"|" + (r.path.empty() ? r.name : r.path);
}

void SearchPanel::RebuildRows(const std::wstring& keepKey) {
    results_ = MergeResults(appResults_, fileResults_);
    rows_ = BuildRows(results_);
    layout_ = LayoutRows(rows_);
    selected_ = -1;
    if (!keepKey.empty()) {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (KeyOf(static_cast<int>(i)) == keepKey) {
                selected_ = static_cast<int>(i);
                break;
            }
    }
    if (selected_ < 0) selected_ = FirstSelectable(rows_);
    hover_ = -1;
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, layout_.total - RowsViewHeight(Layout())));
    EnsureSelectedVisible();
}

void SearchPanel::MoveSelection(int dir) {
    const int next = NextSelectable(rows_, selected_, dir);
    if (next < 0) return;
    selected_ = next;
    EnsureSelectedVisible();
}

void SearchPanel::EnsureSelectedVisible() {
    if (selected_ < 0 || !hwnd_) return;
    const float view = RowsViewHeight(Layout());
    scroll_ = ScrollToRow(scroll_, layout_, selected_, view);
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, layout_.total - view));
}

bool SearchPanel::Activate(bool reveal) {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size()) || rows_[selected_].header) return false;
    const Result r = results_[rows_[selected_].result];
    if (reveal && r.path.empty()) return false;  // nothing to reveal (a packaged app): keep the panel open
    apps::AppEntry app;
    if (r.category == Category::App && r.appIndex >= 0 && r.appIndex < static_cast<int>(apps_.size())) app = apps_[r.appIndex];
    Hide();  // the next window takes the focus
    if (reveal) {
        if (!r.path.empty() && hooks_.reveal) hooks_.reveal(r.path);
        return true;
    }
    if (r.category == Category::App) {
        if (hooks_.launchApp) hooks_.launchApp(app);
    } else if (hooks_.openPath) {
        hooks_.openPath(r.path, r.category == Category::Folder);
    }
    return true;
}

void SearchPanel::OnDataChanged() {
    if (!hwnd_) return;
    Draw();
}

// ---- geometry ----
bool SearchPanel::HasNote() const { return filesAnswered_ && filesState_ != IndexState::Ready && edit_.Text().size() >= 2; }

std::wstring SearchPanel::NoteText() const { return IndexStateMessage(filesState_); }

SearchPanel::Geometry SearchPanel::Layout() const {
    Geometry g{};
    const float s = ui::Scale(dpi_);
    g.viewW = static_cast<float>(host_.Width()) / s;
    g.viewH = static_cast<float>(host_.Height()) / s;
    g.pillW = std::min(sp::kWidth, std::max(280.0f, g.viewW - 32));
    g.pillH = sp::kPillHeight;
    g.pillLeft = (g.viewW - g.pillW) / 2;
    g.pillTop = std::clamp(g.viewH * 0.16f, 48.0f, 220.0f);
    g.panelTop = g.pillTop + g.pillH + sp::kGap;
    const bool query = !edit_.Text().empty();
    const float note = HasNote() ? kNoteHeight : 0.0f;
    if (!query) g.panelH = 0;
    else if (rows_.empty()) g.panelH = 2 * sp::kPanelPad + kEmptyRow + note;
    else g.panelH = std::min(sp::kResultsMaxHeight, layout_.total + 2 * sp::kPanelPad + note);
    return g;
}

float SearchPanel::RowsTop(const Geometry& g) const { return g.panelTop + sp::kPanelPad; }

float SearchPanel::RowsViewHeight(const Geometry& g) const {
    return std::max(0.0f, g.panelH - 2 * sp::kPanelPad - (HasNote() ? kNoteHeight : 0.0f));
}

int SearchPanel::HitRow(float x, float y) const {
    const Geometry g = Layout();
    if (rows_.empty() || x < g.pillLeft || x >= g.pillLeft + g.pillW) return -1;
    const float top = RowsTop(g), view = RowsViewHeight(g);
    if (y < top || y >= top + view) return -1;
    const float ry = y - top + scroll_;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i].header && ry >= layout_.top[i] && ry < layout_.top[i] + layout_.height[i]) return static_cast<int>(i);
    return -1;
}

void SearchPanel::Tick() {
    if (!hwnd_) return;
    const bool more = anim_.Tick(NowMs());
    if (closeDone_) {
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

// The wallpaper behind the pill and the tallest results panel, blurred (stronger veil when there is no image).
void SearchPanel::BuildBackdrop(ID2D1DeviceContext* dc) {
    wallTried_ = true;
    wall_.Reset();
    if (!hwnd_ || !dc || !ui::TransparencyEnabled()) return;
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const float s = ui::Scale(dpi_);
    const Geometry g = Layout();
    const float bottom = g.panelTop + sp::kResultsMaxHeight;
    const RECT region{win.left + static_cast<LONG>(std::lround(g.pillLeft * s)), win.top + static_cast<LONG>(std::lround(g.pillTop * s)),
                      win.left + static_cast<LONG>(std::lround((g.pillLeft + g.pillW) * s)), win.top + static_cast<LONG>(std::lround(bottom * s))};
    const int longest = static_cast<int>(std::max(region.right - region.left, region.bottom - region.top));
    const int shrink = std::max(1, (longest + kBackdropMaxPx - 1) / kBackdropMaxPx);
    ui::Pixels px;
    if (!ui::BlurredWallpaper(mi.rcMonitor, region, shrink, sp::kBlurSigma, dpi_, &px)) return;
    wall_ = ui::MakeBitmap(dc, px);
}

void SearchPanel::Draw() {
    if (!hwnd_ || !host_.Ready()) return;
    const float s = ui::Scale(dpi_);
    ID2D1DeviceContext* dc = host_.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
    if (!dc) return;
    if (!wallTried_) BuildBackdrop(dc);
    const float t = open_;
    const Geometry g = Layout();
    const float scale = 0.96f + 0.04f * t;
    dc->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(g.viewW / 2, g.pillTop)));
    const D2D1_COLOR_F white = D2D1::ColorF(1, 1, 1, 0.96f * t), dim = D2D1::ColorF(1, 1, 1, 0.58f * t);
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &brush);

    const D2D1_RECT_F pillRect = D2D1::RectF(g.pillLeft, g.pillTop, g.pillLeft + g.pillW, g.pillTop + g.pillH);
    const D2D1_RECT_F panelRect = D2D1::RectF(g.pillLeft, g.panelTop, g.pillLeft + g.pillW, g.panelTop + g.panelH);
    const float pillR = g.pillH / 2, panelR = std::min(sp::kResultsRadius, g.panelH / 2);

    auto shadow = [&](const D2D1_RECT_F& r, float radius) {
        constexpr int kLayers = 7;
        const float a = 1.0f - std::pow(1.0f - 0.28f * t, 1.0f / kLayers);
        for (int k = kLayers; k >= 1; --k) {
            const float grow = 16.0f * k / kLayers;
            brush->SetColor(D2D1::ColorF(0, 0, 0, a));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - grow, r.top - grow + 7, r.right + grow, r.bottom + grow + 7), radius + grow, radius + grow),
                                     brush.Get());
        }
    };
    ComPtr<ID2D1BitmapBrush> bb;
    if (wall_ && SUCCEEDED(dc->CreateBitmapBrush(wall_.Get(), &bb))) {
        const D2D1_SIZE_U bp = wall_->GetPixelSize();
        const float unionH = sp::kPillHeight + sp::kGap + sp::kResultsMaxHeight;
        bb->SetTransform(D2D1::Matrix3x2F::Scale(g.pillW / static_cast<float>(bp.width), unionH / static_cast<float>(bp.height)) *
                         D2D1::Matrix3x2F::Translation(g.pillLeft, g.pillTop));
        bb->SetOpacity(t);
    } else {
        bb.Reset();
    }
    auto glass = [&](const D2D1_RECT_F& r, float radius, std::uint32_t veilBlurred) {
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
        if (bb) dc->FillRoundedRectangle(rr, bb.Get());
        brush->SetColor(ui::Rgba({bb ? veilBlurred : sp::kVeilUnblurred}, t));
        dc->FillRoundedRectangle(rr, brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.14f * t));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), radius - 0.5f, radius - 0.5f), brush.Get(), 1.0f);
    };
    shadow(pillRect, pillR);
    glass(pillRect, pillR, sp::kPillVeil);

    // Magnifier + query.
    const float mx = g.pillLeft + 34, my = g.pillTop + g.pillH / 2;
    brush->SetColor(dim);
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(mx - 2, my - 2), 8, 8), brush.Get(), 2.0f);
    dc->DrawLine(D2D1::Point2F(mx + 4, my + 4), D2D1::Point2F(mx + 10, my + 10), brush.Get(), 2.2f);
    const float tx = g.pillLeft + 62, tr = g.pillLeft + g.pillW - 28;
    IDWriteTextFormat* fmt = ui::Format(24);
    const std::wstring& text = edit_.Text();
    if (text.empty()) {
        brush->SetColor(dim);
        ui::DrawLabel(dc, brush.Get(), L"Search", D2D1::RectF(tx, g.pillTop, tr, g.pillTop + g.pillH), fmt);
        if (!hotkeyHint_.empty()) {  // the combination that opens this panel, right-aligned in the pill
            IDWriteTextFormat* tiny = ui::Format(14);
            const float w = ui::MeasureWidth(hotkeyHint_, tiny);
            brush->SetColor(D2D1::ColorF(dim.r, dim.g, dim.b, dim.a * 0.75f));
            ui::DrawLabel(dc, brush.Get(), hotkeyHint_, D2D1::RectF(tr - w, g.pillTop, tr, g.pillTop + g.pillH), tiny);
        }
    } else {
        brush->SetColor(white);
        const float tw = ui::MeasureWidth(text.substr(0, edit_.Caret()), fmt);
        if (edit_.HasSelection()) {
            brush->SetColor(D2D1::ColorF(0.23f, 0.55f, 1.0f, 0.45f * t));
            const float a = tx + ui::MeasureWidth(text.substr(0, edit_.SelectionStart()), fmt), e = tx + ui::MeasureWidth(text.substr(0, edit_.SelectionEnd()), fmt);
            dc->FillRectangle(D2D1::RectF(a, my - 16, e, my + 16), brush.Get());
            brush->SetColor(white);
        }
        ui::DrawLabel(dc, brush.Get(), text, D2D1::RectF(tx, g.pillTop, tr, g.pillTop + g.pillH), fmt);
        if (caretOn_ && !edit_.HasSelection()) dc->FillRectangle(D2D1::RectF(tx + tw + 1, my - 15, tx + tw + 2.5f, my + 15), brush.Get());
    }
    if (text.empty() && caretOn_) {
        brush->SetColor(white);
        dc->FillRectangle(D2D1::RectF(tx - 2, my - 15, tx - 0.5f, my + 15), brush.Get());
    }

    // Results.
    if (g.panelH > 0) {
        shadow(panelRect, panelR);
        glass(panelRect, panelR, sp::kResultsVeil);
        const float top = RowsTop(g), view = RowsViewHeight(g);
        const bool hasRows = !rows_.empty();
        if (!hasRows) {
            brush->SetColor(dim);
            ui::DrawLabel(dc, brush.Get(), L"No results", D2D1::RectF(g.pillLeft + 24, top, g.pillLeft + g.pillW - 24, top + kEmptyRow), ui::Format(15));
        }
        dc->PushAxisAlignedClip(D2D1::RectF(g.pillLeft, top, g.pillLeft + g.pillW, top + view), D2D1_ANTIALIAS_MODE_ALIASED);
        const int tier = ui::IconTierFor(32 * s);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const float y = top + layout_.top[i] - scroll_, h = layout_.height[i];
            if (y + h < top || y > top + view) continue;
            const Row& row = rows_[i];
            if (row.header) {
                brush->SetColor(dim);
                ui::DrawLabel(dc, brush.Get(), row.text, D2D1::RectF(g.pillLeft + 22, y + 4, g.pillLeft + g.pillW - 20, y + h), ui::Format(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
                continue;
            }
            const Result& r = results_[row.result];
            const bool sel = static_cast<int>(i) == selected_;
            if (sel || static_cast<int>(i) == hover_) {
                brush->SetColor(sel ? ui::Rgba(tokens::PaletteFor(tokens::Theme::Dark).accent, t) : D2D1::ColorF(1, 1, 1, 0.12f * t));
                dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(g.pillLeft + 8, y + 1, g.pillLeft + g.pillW - 8, y + h - 1), 10, 10), brush.Get());
            }
            const D2D1_RECT_F ir = D2D1::RectF(g.pillLeft + 18, y + (h - 32) / 2, g.pillLeft + 50, y + (h - 32) / 2 + 32);
            ComPtr<ID2D1Bitmap> bmp;
            const std::wstring cacheKey = r.icon + L"@" + std::to_wstring(tier);
            if (auto it = bitmaps_.find(cacheKey); it != bitmaps_.end()) {
                bmp = it->second;
            } else if (auto px = ui::IconStore::Get().Find(r.icon, tier)) {
                bmp = ui::MakeBitmap(dc, *px);
                bitmaps_[cacheKey] = bmp;
            } else {
                ui::IconStore::Get().Request(r.icon, tier);
            }
            if (bmp) ui::DrawIcon(dc, bmp.Get(), ir, t);
            else if (ui::IconStore::Get().Failed(r.icon, tier)) ui::DrawFallbackTile(dc, ir, r.name, t);
            else {
                brush->SetColor(D2D1::ColorF(1, 1, 1, 0.12f * t));
                dc->FillRoundedRectangle(D2D1::RoundedRect(ir, 7, 7), brush.Get());
            }
            const bool twoLine = r.category != Category::App && !r.detail.empty();
            brush->SetColor(white);
            ui::DrawLabel(dc, brush.Get(), r.name, D2D1::RectF(g.pillLeft + 62, y + (twoLine ? 5 : 0), g.pillLeft + g.pillW - 20, y + (twoLine ? 26 : h)), ui::Format(15));
            if (twoLine) {
                brush->SetColor(sel ? D2D1::ColorF(1, 1, 1, 0.85f * t) : dim);
                ui::DrawLabel(dc, brush.Get(), r.detail, D2D1::RectF(g.pillLeft + 62, y + 25, g.pillLeft + g.pillW - 20, y + h - 3), ui::Format(11.5f));
            }
        }
        dc->PopAxisAlignedClip();
        if (HasNote()) {
            brush->SetColor(dim);
            ui::DrawLabel(dc, brush.Get(), NoteText(), D2D1::RectF(g.pillLeft + 22, top + view, g.pillLeft + g.pillW - 20, top + view + kNoteHeight), ui::Format(12));
        }
    }
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    if (!host_.EndDraw()) {
        log::Warn("search: present failed; closing");
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

// ---- messages ----
LRESULT CALLBACK SearchPanel::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<SearchPanel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self || self->hwnd_ != h) return DefWindowProcW(h, msg, wp, lp);
    return self->OnMessage(msg, wp, lp);
}

LRESULT SearchPanel::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const float s = ui::Scale(dpi_);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Draw();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if (wp == kTimerFrame) Tick();
            else if (wp == kTimerCaret) {
                caretOn_ = !caretOn_;
                Draw();
            } else if (wp == kTimerQuery) RequestFiles();
            return 0;
        case kMsgFiles: OnFilesReady(); return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) wasActive_ = true;
            else if (wasActive_ && dismissOnDeactivate_) Hide();  // clicked elsewhere or switched away (not before the panel was ever active)
            return 0;
        case WM_CLOSE: Hide(); return 0;
        case WM_CHAR:
            if (wp == 27 || wp == 13 || wp == 9) return 0;  // Esc / Enter / Tab are keys, not text
            if (edit_.Insert(static_cast<wchar_t>(wp))) {
                caretOn_ = true;
                Refresh();
                Draw();
            }
            return 0;
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            bool edited = false;
            switch (wp) {
                case VK_ESCAPE: Close(); return 0;
                case VK_RETURN: Activate(ctrl); return 0;
                case VK_DOWN: MoveSelection(1); break;
                case VK_UP: MoveSelection(-1); break;
                case VK_NEXT: for (int i = 0; i < 5; ++i) MoveSelection(1); break;
                case VK_PRIOR: for (int i = 0; i < 5; ++i) MoveSelection(-1); break;
                case VK_LEFT: edit_.Left(shift); break;
                case VK_RIGHT: edit_.Right(shift); break;
                case VK_HOME: edit_.Home(shift); break;
                case VK_END: edit_.End(shift); break;
                case VK_BACK: edit_.Backspace(); edited = true; break;
                case VK_DELETE: edit_.Delete(); edited = true; break;
                case 'A': if (ctrl) edit_.SelectAll(); else return 0; break;
                case 'V':
                    if (!ctrl) return 0;
                    if (OpenClipboard(hwnd_)) {
                        if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
                            if (auto* p = static_cast<const wchar_t*>(GlobalLock(hd))) {
                                edit_.InsertText(p);
                                GlobalUnlock(hd);
                                edited = true;
                            }
                        }
                        CloseClipboard();
                    }
                    break;
                default: return 0;
            }
            caretOn_ = true;
            if (edited) Refresh();
            Draw();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const Geometry g = Layout();
            scroll_ = std::clamp(scroll_ - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 48.0f, 0.0f, std::max(0.0f, layout_.total - RowsViewHeight(g)));
            Draw();
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int hit = HitRow(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (hit != hover_) {
                hover_ = hit;
                Draw();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
            const int hit = HitRow(x, y);
            if (hit >= 0) {
                selected_ = hit;
                Activate(false);
                return 0;
            }
            const Geometry g = Layout();
            const bool inPill = x >= g.pillLeft && x < g.pillLeft + g.pillW && y >= g.pillTop && y < g.pillTop + g.pillH;
            const bool inPanel = g.panelH > 0 && x >= g.pillLeft && x < g.pillLeft + g.pillW && y >= g.panelTop && y < g.panelTop + g.panelH;
            if (!inPill && !inPanel) Close();  // outside dismisses
            return 0;
        }
        case WM_RBUTTONUP: Close(); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace velora::search
