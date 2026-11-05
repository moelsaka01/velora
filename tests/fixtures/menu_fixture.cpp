// Test fixture for the global app menus: a classic Win32 window with an HMENU menu bar, in a process of its own so the reader is
// exercised across processes. It records every WM_COMMAND it receives in the --marker file ("cmd <id>") and changes one menu item
// each time its File menu is about to open (WM_INITMENUPOPUP), which proves the reader replays that message.
//
//   File:   New  Ctrl+N (101) | Open... (102) | --- | Recent > (one.txt 111, two.txt 112) | --- | Opened N times (120) | Exit (103)
//   Edit:   Cut (201, disabled) | Copy (202) | Paste (203, checked)
//   Help:   About (301)
//   Run     (400, a command in the bar itself)
#include "core/win.h"

#include <cstdio>
#include <string>

namespace {

std::wstring g_marker;
int g_opened = 0;
HMENU g_fileMenu = nullptr;

void Mark(const std::wstring& line) {
    if (g_marker.empty()) return;
    HANDLE f = CreateFileW(g_marker.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string utf8;
    for (wchar_t c : line) utf8 += static_cast<char>(c);  // ASCII only
    utf8 += "\n";
    DWORD n = 0;
    WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &n, nullptr);
    CloseHandle(f);
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITMENUPOPUP:
            if (reinterpret_cast<HMENU>(wp) == g_fileMenu) {
                ++g_opened;
                const std::wstring text = L"Opened " + std::to_wstring(g_opened) + L" times";
                ModifyMenuW(g_fileMenu, 120, MF_BYCOMMAND | MF_STRING, 120, text.c_str());
            }
            return 0;
        case WM_COMMAND:
            Mark(L"cmd " + std::to_wstring(LOWORD(wp)));
            if (LOWORD(wp) == 103) DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    std::wstring line = cmd;
    const size_t at = line.find(L"--marker ");
    if (at != std::wstring::npos) {
        g_marker = line.substr(at + 9);
        while (!g_marker.empty() && (g_marker.front() == L'"' || g_marker.front() == L' ')) g_marker.erase(0, 1);
        while (!g_marker.empty() && (g_marker.back() == L'"' || g_marker.back() == L' ')) g_marker.pop_back();
    }
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VeloraMenuFixture";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    HMENU bar = CreateMenu();
    g_fileMenu = CreatePopupMenu();
    HMENU recent = CreatePopupMenu();
    AppendMenuW(recent, MF_STRING, 111, L"one.txt");
    AppendMenuW(recent, MF_STRING, 112, L"two.txt");
    AppendMenuW(g_fileMenu, MF_STRING, 101, L"&New\tCtrl+N");
    AppendMenuW(g_fileMenu, MF_STRING, 102, L"&Open...");
    AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(recent), L"&Recent");
    AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_fileMenu, MF_STRING, 120, L"Opened 0 times");
    AppendMenuW(g_fileMenu, MF_STRING, 103, L"E&xit");
    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING | MF_GRAYED, 201, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, 202, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING | MF_CHECKED, 203, L"&Paste\tCtrl+V");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, 301, L"&About");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(g_fileMenu), L"&File");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    AppendMenuW(bar, MF_STRING, 400, L"Run");

    const std::wstring title = L"Velora Menu Fixture " + std::to_wstring(GetCurrentProcessId());
    HWND w = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 480, 300, nullptr, bar, inst, nullptr);
    if (!w) return 1;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
