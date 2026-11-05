// velora-files: the Files app (CLAUDE.md sec. 5 files G1). On demand: one window, exits when it is closed.
//   velora-files.exe [folder]              open a folder (the user's home by default)
//   velora-files.exe --select <path>       open the folder that contains <path> and highlight it (Search "reveal")
// A second launch hands its folder to the running window and exits.
#include "core/win.h"

#include <shellapi.h>

#include <string>

#include "core/log.h"
#include "core/runtime.h"
#include "files/files_window.h"

using namespace velora;

namespace {

bool IsDirectoryPath(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Hands the request to a window that is already open. True when one took it.
bool ForwardToRunning(const std::wstring& dir, const std::wstring& select) {
    HWND w = nullptr;
    for (int i = 0; i < 50 && !w; ++i) {  // the first instance may still be creating its window
        w = FindWindowW(L"VeloraFilesWindow", nullptr);
        if (!w) Sleep(100);
    }
    if (!w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    AllowSetForegroundWindow(pid);  // this process was just started by the user's action; let the old window come forward
    std::wstring text = dir + L"\n" + select;
    COPYDATASTRUCT cd{files::kCopyDataOpen, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), text.data()};
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(w, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd), SMTO_ABORTIFHUNG, 3000, &result) != 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);  // later DLL loads never come from the current directory

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    log::Init("velora-files");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    OleInitialize(nullptr);  // drag and drop and the clipboard

    std::wstring dir, select;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring a = argv[i];
            if (a == L"--select" && i + 1 < argc) select = argv[++i];
            else if (a.rfind(L"--", 0) != 0) dir = a;
        }
        LocalFree(argv);
    }
    if (!select.empty()) {
        // Reveal: the folder of the item, or the folder itself when the item is one (then it is opened, nothing highlighted).
        if (IsDirectoryPath(select) && dir.empty()) {
            dir = select;
            select.clear();
        } else if (dir.empty()) {
            const size_t slash = select.find_last_of(L"\\/");
            dir = slash == std::wstring::npos ? std::wstring{} : select.substr(0, slash == 2 && select[1] == L':' ? 3 : slash);
        }
    }

    std::wstring mutexName = L"Local\\Velora.Files.SingleInstance";
    if (CurrentProfile().IsTest()) mutexName += L"." + CurrentProfile().id;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        const bool forwarded = ForwardToRunning(dir, select);
        log::Info(forwarded ? "files: handed the folder to the running window" : "files: another instance holds the mutex but has no window");
        CloseHandle(mutex);
        OleUninitialize();
        return forwarded ? 0 : 1;
    }

    int rc = 0;
    {
        files::FilesWindow window(instance);
        if (!window.Create(dir, select, showCmd)) {
            rc = 2;
        } else {
            for (;;) {
                HANDLE change = window.ChangeHandle();
                const DWORD n = change != INVALID_HANDLE_VALUE ? 1 : 0;
                DWORD r = MsgWaitForMultipleObjectsEx(n, n ? &change : nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (n && r == WAIT_OBJECT_0) window.OnDirectoryChanged();
                bool quit = false;
                MSG m;
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                    if (m.message == WM_QUIT) {
                        quit = true;
                        break;
                    }
                    TranslateMessage(&m);
                    DispatchMessageW(&m);
                }
                if (quit) break;
            }
        }
    }
    if (mutex) CloseHandle(mutex);
    OleUninitialize();
    return rc;
}
