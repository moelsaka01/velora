// velora-settings: the Settings app (CLAUDE.md sec. 5 settings G1). On demand: one window, exits when it is closed.
//   velora-settings.exe [--page <name>]    open on a page whose title contains <name> (e.g. "theming", "hotkeys")
// A second launch brings the open window forward.
#include "core/win.h"

#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <string>

#include "core/log.h"
#include "core/runtime.h"
#include "settings/settings_window.h"

using namespace velora;

namespace {

int PageIndex(const std::wstring& wanted) {
    if (wanted.empty()) return 0;
    auto lower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    };
    const std::wstring w = lower(wanted);
    const auto pages = settings::BuildPages();
    for (size_t i = 0; i < pages.size(); ++i)
        if (lower(pages[i].title).find(w) != std::wstring::npos) return static_cast<int>(i);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    log::Init("velora-settings");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring pageName;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::wstring(argv[i]) == L"--page") pageName = argv[i + 1];
        LocalFree(argv);
    }

    std::wstring mutexName = L"Local\\Velora.Settings.SingleInstance";
    if (CurrentProfile().IsTest()) mutexName += L"." + CurrentProfile().id;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = nullptr;
        for (int i = 0; i < 30 && !w; ++i) {
            w = FindWindowW(L"VeloraSettingsWindow", nullptr);
            if (!w) Sleep(100);
        }
        if (w) {
            if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        CloseHandle(mutex);
        return w ? 0 : 1;
    }

    auto dir = DataDir();
    if (dir.empty()) {
        log::Error("settings: no data directory");
        return 2;
    }
    int rc = 0;
    {
        settings::SettingsWindow window(instance);
        if (!window.Create(dir / L"settings.json", PageIndex(pageName), showCmd)) {
            rc = 2;
        } else {
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
    }
    if (mutex) CloseHandle(mutex);
    return rc;
}
