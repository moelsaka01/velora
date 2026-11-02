// The Search panel (CLAUDE.md sec. 5 search G1): a Spotlight-style pill (520 pt wide) with a results panel under it, opened by a global
// hotkey. Apps come from the shell's app index, files from the Windows Search index (a worker thread); the whole flow works from the
// keyboard: type, Up/Down, Enter opens, Ctrl+Enter reveals in Files, Esc closes. The window (transparent, one monitor, so a click
// outside dismisses) and its GPU resources exist only while the panel is open. Owner: C.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "apps/app_index.h"
#include "render/animation.h"
#include "render/render_host.h"
#include "search/search_core.h"
#include "search/win_search.h"
#include "ui/text_edit.h"

namespace velora::search {

// What the panel does with a result; the defaults are the real thing, tests replace them.
struct PanelHooks {
    std::function<std::vector<apps::AppEntry>()> apps;                 // snapshot of the app index
    std::function<void(const apps::AppEntry&)> launchApp;
    std::function<void(const std::wstring& path, bool isFolder)> openPath;  // a file in its default app, a folder in Files
    std::function<void(const std::wstring& path)> reveal;              // show in Files with the item highlighted
    FileSearcher::Runner fileQuery;                                    // empty = the real Windows Search index
};
PanelHooks DefaultHooks();

class SearchPanel {
public:
    explicit SearchPanel(HINSTANCE instance, PanelHooks hooks = DefaultHooks());
    ~SearchPanel();
    SearchPanel(const SearchPanel&) = delete;
    SearchPanel& operator=(const SearchPanel&) = delete;

    void Toggle();
    void Show();
    void Hide();  // immediately
    void Close(); // with the closing spring (immediately when animations are off)
    bool Visible() const { return hwnd_ != nullptr; }
    HWND Hwnd() const { return hwnd_; }
    void SetQuery(const std::wstring& text);  // as if typed (IPC / tests)
    void OnDataChanged();                      // the app index or icons changed
    void SetAnimationsEnabled(bool on) { anim_.SetEnabled(on); }
    void SetOnHidden(std::function<void()> fn) { onHidden_ = std::move(fn); }
    // The panel closes when it loses the focus (default). Tests and scripted runs keep it open.
    void SetDismissOnDeactivate(bool on) { dismissOnDeactivate_ = on; }
    // Opens (or reveals) the selected result and closes the panel. False when nothing is selected.
    bool Activate(bool reveal);

    struct Snapshot {
        std::wstring query;
        int results = 0;
        int selectedResult = -1;  // index into the results, -1 for none
        std::wstring selectedName;
        IndexState files = IndexState::Ready;
        bool filesAnswered = false;  // the index has answered at least once for the current query
        int fileResults = 0;
        std::wstring note;  // the line shown under the results when files cannot be searched
    };
    Snapshot GetSnapshot() const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void Refresh();           // rebuild the results from the apps and the last file answer
    void RequestFiles();      // debounced
    void OnFilesReady();
    void RebuildRows(const std::wstring& keepKey);
    std::wstring KeyOf(int row) const;
    void MoveSelection(int dir);
    void EnsureSelectedVisible();
    void Draw();
    void Tick();
    void StartOpen();
    void OnSpring(float v, bool done);
    void BuildBackdrop(ID2D1DeviceContext* dc);
    struct Geometry {
        float pillLeft, pillTop, pillW, pillH, panelTop, panelH, viewW, viewH;
    };
    Geometry Layout() const;
    float RowsTop(const Geometry& g) const;
    float RowsViewHeight(const Geometry& g) const;
    int HitRow(float x, float y) const;
    bool HasNote() const;
    std::wstring NoteText() const;

    HINSTANCE instance_;
    PanelHooks hooks_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    render::AnimationEngine anim_;
    float open_ = 0;
    render::AnimationEngine::Id spring_ = 0;
    bool closing_ = false, closeDone_ = false, wallTried_ = false, wasActive_ = false, timerOn_ = false, dismissOnDeactivate_ = true;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;
    std::function<void()> onHidden_;

    ui::TextEdit edit_;
    bool caretOn_ = true;
    std::vector<apps::AppEntry> apps_;
    std::vector<Result> appResults_, fileResults_, results_;
    std::vector<Row> rows_;
    RowLayout layout_;
    int selected_ = -1, hover_ = -1;
    float scroll_ = 0;
    std::unique_ptr<FileSearcher> searcher_;
    unsigned requestId_ = 0;
    IndexState filesState_ = IndexState::Ready;
    bool filesAnswered_ = false;
    std::wstring filesQuery_;  // the query the file results belong to
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    int listener_ = 0;
};

}  // namespace velora::search
