// The Files window end to end, in this process: create it on a temp tree, let it list, then drive it through its actions and its
// message handlers (keys, mouse clicks, double click) - rename, new folder, copy / paste, Recycle Bin, navigation, views, sorting,
// painting. Everything happens inside the temp tree. No dialogs are expected; none of these paths opens one.
#include "core/win.h"

#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "files/file_ops.h"
#include "files/files_window.h"

using namespace velora::files;
namespace fs = std::filesystem;

namespace {
int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

bool g_quit = false;

// Pumps messages until `done()` or the timeout.
bool Pump(const std::function<bool()>& done, int timeoutMs = 10000) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) g_quit = true;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (done()) return true;
        if (GetTickCount64() > end || g_quit) return false;
        Sleep(10);
    }
}

void Write(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    f << content;
}

int IndexOf(const FilesWindow& w, const wchar_t* name) {
    const auto& e = w.Entries();
    for (size_t i = 0; i < e.size(); ++i)
        if (_wcsicmp(e[i].name.c_str(), name) == 0) return static_cast<int>(i);
    return -1;
}

LPARAM Point(HWND h, float xDip, float yDip) {
    const float s = GetDpiForWindow(h) / 96.0f;
    return MAKELPARAM(static_cast<int>(xDip * s), static_cast<int>(yDip * s));
}

}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    OleInitialize(nullptr);

    wchar_t tmp[MAX_PATH] = {}, longTmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    GetLongPathNameW(tmp, longTmp, MAX_PATH);
    const fs::path root = fs::path(longTmp) / (L"velora-files-window-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root / L"sub");
    Write(root / L"a.txt", "alpha");
    Write(root / L"b.txt", "bravo bravo");
    Write(root / L"c.bin", std::string(5000, 'c'));
    Write(root / L"sub" / L"inner.txt", "inner");
    const std::wstring hashA = Sha256Hex((root / L"a.txt").wstring()), hashC = Sha256Hex((root / L"c.bin").wstring());

    {
        FilesWindow w(GetModuleHandleW(nullptr));
        CHECK(w.Create(root.wstring(), L"", SW_SHOWNOACTIVATE));
        HWND h = w.Hwnd();
        CHECK(h != nullptr);
        if (!h) return 1;

        // Listing and first paint.
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.items == 4; }));
        CHECK(w.GetSnapshot().dir == root.wstring());
        CHECK(IndexOf(w, L"sub") == 0);  // folders first
        InvalidateRect(h, nullptr, FALSE);
        CHECK(Pump([&] { return w.GetSnapshot().drawn; }));

        // Keys reach the window: Down selects the first item, Down again the next.
        SendMessageW(h, WM_KEYDOWN, VK_DOWN, 0);
        CHECK(w.GetSnapshot().selected == 1);
        SendMessageW(h, WM_KEYDOWN, VK_DOWN, 0);
        CHECK(w.GetSnapshot().selected == 1);

        // Mouse: a click on the first row selects it; two quick clicks on a folder open it, Backspace goes up.
        const float rowY = 52 + 28 + 14;  // toolbar + header + half a row
        SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, Point(h, 216 + 120, rowY));
        SendMessageW(h, WM_LBUTTONUP, 0, Point(h, 216 + 120, rowY));
        CHECK(w.GetSnapshot().selected == 1);
        SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, Point(h, 216 + 120, rowY));
        SendMessageW(h, WM_LBUTTONUP, 0, Point(h, 216 + 120, rowY));
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.dir == (root / L"sub").wstring(); }));
        CHECK(w.GetSnapshot().items == 1 && w.GetSnapshot().canBack);
        SendMessageW(h, WM_KEYDOWN, VK_BACK, 0);
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.dir == root.wstring() && s.items == 4; }));
        CHECK(w.GetSnapshot().selected == 1);  // coming back selects the folder we came from

        // Back / forward.
        w.GoBack();
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.dir == (root / L"sub").wstring(); }));
        w.GoForward();
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.dir == root.wstring(); }));

        // Rename with the field: a.txt -> renamed.txt, contents untouched.
        int a = IndexOf(w, L"a.txt");
        CHECK(a >= 0);
        w.SelectIndex(a);
        w.BeginRename(a);
        CHECK(w.GetSnapshot().renaming == a);
        w.TypeRenameText(L"renamed.txt");
        w.CommitRename();
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.renaming < 0 && IndexOf(w, L"renamed.txt") >= 0; }));
        CHECK(!fs::exists(root / L"a.txt") && Sha256Hex((root / L"renamed.txt").wstring()) == hashA);
        // Escape cancels a rename.
        int r = IndexOf(w, L"renamed.txt");
        w.SelectIndex(r);
        w.BeginRename(r);
        w.TypeRenameText(L"zzz");
        SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(w.GetSnapshot().renaming < 0 && fs::exists(root / L"renamed.txt") && !fs::exists(root / L"zzz"));

        // New folder: created, selected, rename field open; naming it keeps it.
        w.NewFolder();
        CHECK(Pump([&] { return w.GetSnapshot().renaming >= 0; }));
        CHECK(fs::is_directory(root / L"untitled folder"));
        w.TypeRenameText(L"Projects");
        w.CommitRename();
        CHECK(Pump([&] { return IndexOf(w, L"Projects") >= 0 && w.GetSnapshot().renaming < 0; }));
        CHECK(fs::is_directory(root / L"Projects") && !fs::exists(root / L"untitled folder"));

        // Copy and paste next to the original (numbered copy). The clipboard can be unavailable in a locked-down session.
        const int c = IndexOf(w, L"c.bin");
        w.SelectIndex(c);
        w.CopyToClipboard(false);
        IDataObject* clip = nullptr;
        const bool clipboard = SUCCEEDED(OleGetClipboard(&clip)) && clip;
        if (clip) clip->Release();
        if (clipboard) {
            w.Paste();
            CHECK(Pump([&] { return IndexOf(w, L"c (2).bin") >= 0; }));
            CHECK(Sha256Hex((root / L"c (2).bin").wstring()) == hashC && Sha256Hex((root / L"c.bin").wstring()) == hashC);
        } else {
            std::printf("INFO clipboard unavailable in this session; paste not exercised\n");
        }

        // Recycle Bin: the item leaves the folder (and goes to the bin, not away).
        const int b = IndexOf(w, L"b.txt");
        w.SelectIndex(b);
        w.MoveToBin();
        CHECK(Pump([&] { return IndexOf(w, L"b.txt") < 0 && !w.GetSnapshot().loading; }));
        CHECK(!fs::exists(root / L"b.txt"));

        // Views and sorting repaint without trouble.
        w.SetView(true);
        CHECK(w.GetSnapshot().iconView);
        InvalidateRect(h, nullptr, FALSE);
        Pump([] { return false; }, 150);
        w.SetSort(SortKey::Size);
        w.SetSort(SortKey::Size);  // flips to descending
        w.SetSort(SortKey::Kind);
        w.SetView(false);
        CHECK(!w.GetSnapshot().iconView);
        w.SelectAllItems();
        CHECK(w.GetSnapshot().selected == w.GetSnapshot().items);
        InvalidateRect(h, nullptr, FALSE);
        Pump([] { return false; }, 150);
        CHECK(w.GetSnapshot().drawn);

        // A second launch hands over its folder (the WM_COPYDATA path).
        std::wstring payload = (root / L"sub").wstring() + L"\n" + (root / L"sub" / L"inner.txt").wstring();
        COPYDATASTRUCT cd{kCopyDataOpen, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)), payload.data()};
        SendMessageW(h, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd));
        CHECK(Pump([&] { auto s = w.GetSnapshot(); return !s.loading && s.dir == (root / L"sub").wstring() && s.selected == 1; }));

        // Close.
        PostMessageW(h, WM_CLOSE, 0, 0);
        CHECK(Pump([&] { return g_quit; }));
    }

    OleUninitialize();
    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
