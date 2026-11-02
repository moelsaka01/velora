// The window of Velora Files (CLAUDE.md sec. 5 files G1): a Finder-style window drawn with Direct2D - sidebar (Favorites / Drives),
// toolbar, traffic-light controls, list and icon views, selection, rename, clipboard, drag and drop in and out, context menu.
// One window per process; a second launch hands its folder to this window (WM_COPYDATA). Owner: C.
#pragma once

#include <d2d1_3.h>
#include <wrl/client.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/win.h"
#include "files/file_ops.h"
#include "files/files_layout.h"
#include "files/fs_model.h"
#include "menubar/menu_popup.h"
#include "render/render_host.h"
#include "ui/text_edit.h"

namespace velora::files {

// Decodes shell icons on a worker thread (one STA); the window turns the pixels into bitmaps on its own device.
class FileIcons;

// What a second launch sends to the running window: WM_COPYDATA with dwData == kCopyDataOpen, UTF-16 "<folder>\n<item to select>".
inline constexpr ULONG_PTR kCopyDataOpen = 0x56464C31;

class FilesWindow {
public:
    explicit FilesWindow(HINSTANCE instance);
    ~FilesWindow();
    FilesWindow(const FilesWindow&) = delete;
    FilesWindow& operator=(const FilesWindow&) = delete;

    // Creates and shows the window on `dir` (the user's home when empty); `select` is an item to highlight.
    bool Create(const std::wstring& dir, const std::wstring& select, int showCmd);
    HWND Hwnd() const { return hwnd_; }
    // Shows `dir` and raises the window (a second launch).
    void Open(const std::wstring& dir, const std::wstring& select);
    // Change notification for the folder on show (FindFirstChangeNotification); signalled -> OnDirectoryChanged().
    HANDLE ChangeHandle() const { return change_; }
    void OnDirectoryChanged();

    // The actions behind the keyboard shortcuts and the context menu (also what the automated tests drive).
    void OpenIndex(int index);  // a folder: navigate; a file: its default app
    void GoBack();
    void GoForward();
    void GoUp();
    void SetView(bool icons);
    void SetSort(SortKey key);  // the same key again flips the direction
    void CopyToClipboard(bool cut);
    void Paste();
    void MoveToBin();  // Recycle Bin only
    void BeginRename(int index);
    void CommitRename();
    void CancelRename();
    void NewFolder();
    void SelectAllItems();
    void SelectIndex(int index);  // select only this item (and scroll to it)
    void TypeRenameText(const std::wstring& text);  // replaces the text of the open rename field

    // What the window shows right now (tests and diagnostics).
    struct Snapshot {
        std::wstring dir;
        int items = 0, selected = 0, renaming = -1;
        bool loading = false, iconView = false, canBack = false, canForward = false, drawn = false;
        DWORD listError = 0;
    };
    Snapshot GetSnapshot() const;
    const std::vector<Entry>& Entries() const { return entries_; }

private:
    struct DropTarget;
    struct ListMessage;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void OnCreate();
    void OnDestroy();

    // data
    void Navigate(const std::wstring& dir, std::wstring select = {}, bool record = true);  // select by value: callers pass dir_ itself
    void StartListing(const std::wstring& select, std::set<std::wstring> keepSelected);
    void OnListed(ListMessage* msg);
    void ApplyPrefsToListing();
    void ReloadSidebar();
    void Reload();  // same folder, keep selection and scroll
    std::vector<std::wstring> SelectedPaths() const;
    int IndexOfPath(const std::wstring& path) const;
    void WatchFolder();

    // actions
    void CopyPathText();
    void ShowContextMenu(POINT screen, int hitIndex);
    void FinishOperation(const OpResult& r);
    OpOptions Opts() const;

    // geometry / hit testing (client DIPs)
    float Scale() const;
    float ViewWidth() const;
    float ViewHeight() const;
    float ViewTop() const;
    float ViewBottom() const;
    float ContentHeight() const;
    int GridColumns() const;
    D2D1_RECT_F ItemRect(int index) const;  // in client DIPs, scroll applied
    D2D1_RECT_F NameRect(int index) const;  // where the name text sits (rename field)
    int HitItem(float x, float y) const;
    enum class Zone { None, Close, Minimize, Zoom, Back, Forward, ViewList, ViewIcons, Sidebar, Header, Item, Blank, Crumb, Caption };
    struct Hit {
        Zone zone = Zone::None;
        int index = -1;  // item / sidebar entry / header column / crumb
    };
    Hit HitTest(float x, float y) const;
    void EnsureVisible(int index);
    void ClampScroll();

    // drawing
    void Draw();
    void DrawSidebar(ID2D1DeviceContext* dc);
    void DrawToolbar(ID2D1DeviceContext* dc);
    void DrawContent(ID2D1DeviceContext* dc);
    void DrawStatus(ID2D1DeviceContext* dc);
    void DrawItemIcon(ID2D1DeviceContext* dc, const Entry& e, const D2D1_RECT_F& r);
    void DrawRenameField(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, bool centered);
    void Invalidate();

    // input
    void OnMouseDown(float x, float y, WPARAM keys, bool right);
    void OnMouseUp(float x, float y, WPARAM keys);
    void OnMouseMove(float x, float y, WPARAM keys);
    bool OnKeyDown(WPARAM vk);
    void OnChar(wchar_t c);
    void StartDrag();
    void ExecuteDrop(IDataObject* data, const std::wstring& destDir, DWORD effect);
    std::wstring DropFolderAt(float x, float y, int* hotItem, int* hotSide) const;

    struct SideItem {
        std::wstring label, path;
        int glyph = 0;
        bool header = false;
        float y = 0;
    };

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    tokens::Theme theme_ = tokens::Theme::Light;
    bool active_ = true;
    unsigned framesDrawn_ = 0;

    std::wstring dir_;
    History history_;
    std::vector<Entry> entries_;
    Selection sel_;
    float scroll_ = 0;
    Prefs prefs_;
    std::wstring settingsPath_;
    unsigned generation_ = 0;
    bool loading_ = false;
    DWORD listError_ = 0;
    std::wstring pendingSelect_;
    bool pendingRename_ = false;
    std::vector<SideItem> side_;
    HANDLE change_ = INVALID_HANDLE_VALUE;

    // interaction state
    Hit hover_;
    int dropHotItem_ = -1, dropHotSide_ = -1;
    bool dragPending_ = false, mouseDown_ = false, deselectOnUp_ = false;
    POINT dragStart_{};
    int dragIndex_ = -1;
    bool dragging_ = false;
    ui::TextEdit rename_;
    int renaming_ = -1;
    bool caretOn_ = true;
    std::wstring typed_;
    ULONGLONG typedAt_ = 0;
    ULONGLONG lastClickAt_ = 0;
    int lastClickIndex_ = -1;
    std::set<std::wstring> cut_;
    menubar::MenuPopup popup_;
    ULONGLONG popupClosedAt_ = 0;
    std::vector<std::wstring> dropSources_;
    DWORD dropEffect_ = 0;

    std::unique_ptr<FileIcons> icons_;
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    std::unique_ptr<DropTarget> dropTarget_;
};

}  // namespace velora::files
