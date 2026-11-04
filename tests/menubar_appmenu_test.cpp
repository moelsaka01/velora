// The menu bar showing another app's menus, end to end in this process: a MenuBar attached to a plain host window, the classic-menu
// fixture (menu_fixture.exe, its own process) as the "active app", then real window messages: the titles appear after the app name,
// a click opens the drawn popup under the title, keys walk it, Enter runs the command in the other process (marker file), a submenu
// drills in with a way back, Right moves to the next title, and a window without menus shows the name only.
#include "core/win.h"

#include <cstdio>
#include <functional>
#include <string>

#include <fstream>
#include <vector>

#include "core/event_bus.h"
#include "core/log.h"
#include "core/runtime.h"
#include "menubar/menubar.h"
#include "test_process.h"
#include "windows/window_tracker.h"

using namespace velora;

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

std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)> g_hook;

LRESULT CALLBACK HostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT r = 0;
    if (g_hook && g_hook(h, msg, wp, lp, r)) return r;
    return DefWindowProcW(h, msg, wp, lp);
}

bool Pump(const std::function<bool()>& done, int timeoutMs = 10000) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (done()) return true;
        if (GetTickCount64() > end) return false;
        Sleep(10);
    }
}

HWND PopupWindow() { return FindWindowW(L"VeloraMenuPopup", nullptr); }

void Key(WPARAM vk) {
    if (HWND p = PopupWindow()) SendMessageW(p, WM_KEYDOWN, vk, 0);
}

}  // namespace

// On failure CI shows what the menu bar logged (the log is a file otherwise nobody reads).
void DumpLogTail() {
    std::ifstream in(velora::LogDir() / L"menubar_appmenu_test.log");
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    std::printf("---- log tail ----\n");
    for (size_t i = lines.size() > 60 ? lines.size() - 60 : 0; i < lines.size(); ++i) std::printf("%s\n", lines[i].c_str());
}

int main(int, char** argv) {
    velora::log::Init("menubar_appmenu_test");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const int wlen = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0);
    std::wstring fixture(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, fixture.data(), wlen);
    fixture.resize(wcslen(fixture.c_str()));
    wchar_t full[MAX_PATH] = {};
    if (GetFullPathNameW(fixture.c_str(), MAX_PATH, full, nullptr)) fixture = full;
    wchar_t tmp[MAX_PATH] = {}, longTmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    GetLongPathNameW(tmp, longTmp, MAX_PATH);
    const std::wstring marker = std::wstring(longTmp) + L"velora-barmenu-" + std::to_wstring(GetCurrentProcessId()) + L".marker";

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VeloraBarTestHost";
    RegisterClassExW(&wc);
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"bar", WS_POPUP, 40, 40, 1400, 30, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(host != nullptr);
    if (!host) return 1;

    int rc;
    {
        EventBus bus;
        win::WindowTracker tracker(bus);
        menubar::MenuBar bar(bus, tracker, {});
        g_hook = bar.Options().hook;
        ShowWindow(host, SW_SHOWNOACTIVATE);
        bar.Attach(host);
        CHECK(bar.Attached());
        const float s = GetDpiForWindow(host) / 96.0f;

        testproc::Child c;
        CHECK(testproc::Spawn(L"\"" + fixture + L"\" --marker \"" + marker + L"\"", L"Velora Menu Fixture", 15000, &c));
        if (c.hwnd) {
            // The titles appear after the app name once the worker has read them.
            bar.SetMenuTargetOverride(c.hwnd);
            CHECK(Pump([&] { return bar.AppMenuSummary() == L"File Edit Help Run"; }));
            CHECK(bar.AppMenuTier() == menubar::MenuTier::Win32);
            D2D1_RECT_F file{}, edit{}, run{};
            CHECK(bar.TitleRectDip(0, &file) && bar.TitleRectDip(1, &edit) && bar.TitleRectDip(3, &run));
            CHECK(file.right <= edit.left + 0.01f && edit.left > file.left);  // laid out left to right, no overlap

            // settle: after a popup was dismissed by a click the bar ignores a click on that title for 250 ms (that click is the one that
            // closed it). After a command ran there is nothing to ignore, so those clicks pass settle=false and must open at once.
            auto click = [&](const D2D1_RECT_F& r, bool settle = true) {
                if (settle) Sleep(300);
                const LPARAM pt = MAKELPARAM(static_cast<int>((r.left + r.right) / 2 * s), static_cast<int>(15 * s));
                SendMessageW(host, WM_MOUSEMOVE, 0, pt);
                SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, pt);
            };

            // Click File: the popup opens under it.
            click(file);
            CHECK(Pump([&] { return bar.AppMenuPopupOpen() && PopupWindow() != nullptr; }));
            CHECK(bar.OpenTitleIndex() == 0);
            RECT pr{};
            GetWindowRect(PopupWindow(), &pr);
            RECT hr{};
            GetWindowRect(host, &hr);
            CHECK(pr.top >= hr.bottom - 2 && pr.left >= hr.left);  // under the bar, not over it
            Key(VK_DOWN);                                          // New
            Key(VK_RETURN);
            CHECK(testproc::WaitMarker(marker, "cmd 101"));        // WM_COMMAND reached the other process
            CHECK(Pump([&] { return !bar.AppMenuPopupOpen(); }));

            // Submenu: File > Recent drills in, has a way back, runs "two.txt".
            click(file, false);  // right after a command: must reopen without waiting
            CHECK(Pump([&] { return bar.AppMenuPopupOpen() && PopupWindow() != nullptr; }));
            Key(VK_DOWN);  // New
            Key(VK_DOWN);  // Open...
            Key(VK_DOWN);  // Recent >
            Key(VK_RETURN);
            CHECK(Pump([&] { return bar.AppMenuPopupOpen() && PopupWindow() != nullptr; }));
            Key(VK_DOWN);  // < Recent (the way back)
            Key(VK_RETURN);
            CHECK(Pump([&] { return bar.AppMenuPopupOpen() && PopupWindow() != nullptr; }));  // back at the File menu
            Key(VK_DOWN);
            Key(VK_DOWN);
            Key(VK_DOWN);
            Key(VK_RETURN);  // Recent > again
            CHECK(Pump([&] { return bar.AppMenuPopupOpen() && PopupWindow() != nullptr; }));
            Key(VK_DOWN);  // back
            Key(VK_DOWN);  // one.txt
            Key(VK_DOWN);  // two.txt
            Key(VK_RETURN);
            CHECK(testproc::WaitMarker(marker, "cmd 112"));

            // Right moves to the next title while a menu is open.
            click(file, false);
            CHECK(Pump([&] { return bar.AppMenuPopupOpen() && PopupWindow() != nullptr; }));
            Key(VK_RIGHT);
            CHECK(Pump([&] { return bar.OpenTitleIndex() == 1 && PopupWindow() != nullptr; }));
            Key(VK_ESCAPE);
            CHECK(Pump([&] { return !bar.AppMenuPopupOpen(); }));

            // A command in the bar itself runs without a popup.
            click(run);
            CHECK(testproc::WaitMarker(marker, "cmd 400"));
            CHECK(!bar.AppMenuPopupOpen());

            // A window without a menu: name only.
            bar.SetMenuTargetOverride(host);  // the bar's own host window has no menu
            CHECK(Pump([&] { return bar.AppMenuSummary().empty(); }));
            D2D1_RECT_F none{};
            CHECK(!bar.TitleRectDip(0, &none));

            // Back to the fixture, then it exits: the titles go away with the window.
            bar.SetMenuTargetOverride(c.hwnd);
            CHECK(Pump([&] { return bar.AppMenuSummary() == L"File Edit Help Run"; }));
            PostMessageW(c.hwnd, WM_COMMAND, MAKEWPARAM(103, 0), 0);  // Exit
            WaitForSingleObject(c.pi.hProcess, 5000);
            bar.SetMenuTargetOverride(nullptr);
            Pump([] { return false; }, 200);
        }
        testproc::Close(&c);
        bar.Detach();
        g_hook = nullptr;
        rc = g_failed ? 1 : 0;
    }
    DestroyWindow(host);
    DeleteFileW(marker.c_str());
    if (g_failed) DumpLogTail();
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return rc;
}
