// The Settings window driven through its own actions (no real mouse): toggles, a pick list, the editable list, page changes,
// dependent rows, button actions, a real paint, and the written settings.json. Opens a window: runs in CI.
#include <windowsx.h>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>

#include "core/runtime.h"
#include "core/win.h"
#include "settings/settings_window.h"
#include "theming/theme_policy.h"

using namespace velora;
using namespace velora::settings;
using Hit = SettingsWindow::Hit;
using HitKind = SettingsWindow::HitKind;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

static void Pump(int ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static int PageOf(const std::wstring& title) {
    const auto pages = BuildPages();
    for (size_t i = 0; i < pages.size(); ++i)
        if (pages[i].title == title) return static_cast<int>(i);
    return -1;
}

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const auto dir = std::filesystem::path(tmp) / (L"velora-settings-win-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto file = dir / L"settings.json";
    FILE* f = _wfopen(file.c_str(), L"wb");
    if (f) {
        std::fputs(R"({"dock":{"pinned":["x"]}})", f);
        std::fclose(f);
    }

    {
        SettingsWindow w(GetModuleHandleW(nullptr));
        int notified = 0;
        std::string lastAction;
        int startupSet = -1;
        w.SetNotifyHook([&] { ++notified; });
        w.SetActionHook([&](const std::string& a) { lastAction = a; });
        w.SetStartupHook([&](bool on) { startupSet = on ? 1 : 0; });
        CHECK(w.Create(file, PageOf(L"Window theming"), SW_SHOWNOACTIVATE));
        Pump(300);  // first paint
        CHECK(w.Hwnd() != nullptr && w.CurrentPage() == PageOf(L"Window theming"));

        auto find = [&](HitKind kind, const std::string& key) {
            const auto pages = BuildPages();
            for (const Hit& h : w.Hits())
                if (h.kind == kind && (key.empty() || (h.item >= 0 && pages[w.CurrentPage()].items[h.item].key == key))) return h;
            return Hit{};
        };
        using HK = SettingsWindow::HitKind;

        // A switch writes the file at once and tells the shell.
        Hit t = find(HK::Toggle, "theming.corners");
        CHECK(t.kind == HK::Toggle);
        CHECK(w.GetStore().GetBool("theming.corners", true));
        w.Activate(t);
        CHECK(!w.GetStore().GetBool("theming.corners", true) && notified == 1);
        {
            Store disk(file);
            CHECK(disk.Load() && !disk.GetBool("theming.corners", true));
            CHECK(disk.GetList("dock.pinned").size() == 1);  // other modules' keys are kept
            CHECK(!theming::ConfigFromSettings(disk.Root()).corners);  // and the theming module reads it
        }
        // Dependent rows are inert while their master switch is off.
        w.Activate(find(HK::Toggle, "theming.enabled"));
        CHECK(!w.GetStore().GetBool("theming.enabled", true));
        const int before = notified;
        w.Activate(find(HK::Toggle, "theming.caption"));
        CHECK(w.GetStore().GetBool("theming.caption", true) && notified == before);
        w.Activate(find(HK::Toggle, "theming.enabled"));
        CHECK(w.GetStore().GetBool("theming.enabled", false));

        // The exclusion list: type, Enter-style add, duplicate ignored, bad name refused, remove.
        w.TypeInField(L"  Notepad++ ");
        w.Activate(find(HK::ListAdd, ""));
        CHECK(w.GetStore().GetList("theming.exclude") == (std::vector<std::string>{"notepad++.exe"}));
        CHECK(w.ListFieldText().empty());
        w.TypeInField(L"notepad++.exe");
        w.Activate(find(HK::ListAdd, ""));
        CHECK(w.GetStore().GetList("theming.exclude").size() == 1);
        w.TypeInField(L"bad|name");
        w.Activate(find(HK::ListAdd, ""));
        CHECK(w.GetStore().GetList("theming.exclude").size() == 1);
        w.Activate(find(HK::ListRemove, ""));
        CHECK(w.GetStore().GetList("theming.exclude").empty());

        // Pick list: the popup opens, Down + Return choose the first entry after the current one.
        Hit c = find(HK::ChoiceButton, "theming.backdrop");
        CHECK(c.kind == HK::ChoiceButton);
        w.Activate(c);
        Pump(200);
        HWND popup = FindWindowW(L"VeloraMenuPopup", nullptr);
        CHECK(popup != nullptr);
        if (popup) {
            SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);  // Mica (first row)
            SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
            SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
            Pump(200);
            CHECK(w.GetStore().GetString("theming.backdrop", "mica") == "acrylic");
            CHECK(FindWindowW(L"VeloraMenuPopup", nullptr) == nullptr);
        }

        // Other pages: hotkey choice, button actions, startup switch.
        w.ShowPage(PageOf(L"General"));
        w.Activate(find(HK::ActionButton, ""));
        CHECK(lastAction == "restore-windows");
        w.Activate(find(HK::Toggle, "startup.launchAtLogin"));
        CHECK(startupSet == 1 && w.GetStore().GetBool("startup.launchAtLogin", false));
        w.Activate(find(HK::Toggle, "startup.launchAtLogin"));
        CHECK(startupSet == 0);
        w.ShowPage(PageOf(L"Updates"));
        w.Activate(find(HK::ActionButton, ""));
        CHECK(lastAction == "check-updates");

        // Sidebar navigation and keyboard focus move without crashing; the window paints each page.
        for (int p = 0; p < static_cast<int>(BuildPages().size()); ++p) {
            for (const Hit& h : w.Hits())
                if (h.kind == HK::Page && h.sub == p) {
                    w.Activate(h);
                    break;
                }
            CHECK(w.CurrentPage() == p);
            SendMessageW(w.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
            SendMessageW(w.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
            InvalidateRect(w.Hwnd(), nullptr, FALSE);
            UpdateWindow(w.Hwnd());
            Pump(30);
        }
        // Resize to a small window: the rows scroll, nothing throws.
        SetWindowPos(w.Hwnd(), nullptr, 0, 0, 640, 440, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(w.Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA * 3), 0);
        Pump(100);
        CHECK(IsWindow(w.Hwnd()));
        SendMessageW(w.Hwnd(), WM_CLOSE, 0, 0);
        Pump(100);
    }

    // A broken file: the window still opens with defaults and says so; the first change replaces the file.
    {
        FILE* g = _wfopen(file.c_str(), L"wb");
        if (g) {
            std::fputs("{ nope", g);
            std::fclose(g);
        }
        SettingsWindow w(GetModuleHandleW(nullptr));
        w.SetNotifyHook([] {});
        CHECK(w.Create(file, 0, SW_SHOWNOACTIVATE));
        Pump(150);
        CHECK(w.GetStore().GetBool("theming.enabled", true));
        SendMessageW(w.Hwnd(), WM_CLOSE, 0, 0);
        Pump(50);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
