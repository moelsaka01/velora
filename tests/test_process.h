// Helpers for tests that start other programs (fixtures) and look at their windows. Header-only.
#pragma once

#include "core/win.h"

#include <fstream>
#include <sstream>
#include <string>

namespace testproc {

struct Child {
    PROCESS_INFORMATION pi{};
    HWND hwnd = nullptr;
};

struct FindCtx {
    DWORD pid;
    std::wstring part;
    HWND found = nullptr;
};

inline BOOL CALLBACK FindProc(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<FindCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
    wchar_t title[256] = {};
    GetWindowTextW(h, title, 256);
    if (std::wstring(title).find(c->part) != std::wstring::npos) {
        c->found = h;
        return FALSE;
    }
    return TRUE;
}

inline bool Spawn(const std::wstring& cmdline, const std::wstring& titlePart, int timeoutMs, Child* out) {
    std::wstring cmd = cmdline;
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNOACTIVATE;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &out->pi)) return false;
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    FindCtx ctx{out->pi.dwProcessId, titlePart};
    while (GetTickCount64() < end) {
        EnumWindows(FindProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.found) {
            out->hwnd = ctx.found;
            Sleep(500);  // let the toolkit finish building its menus
            return true;
        }
        if (WaitForSingleObject(out->pi.hProcess, 100) == WAIT_OBJECT_0) return false;
    }
    return false;
}

inline void Close(Child* c) {
    if (!c->pi.hProcess) return;
    if (c->hwnd && IsWindow(c->hwnd)) PostMessageW(c->hwnd, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(c->pi.hProcess, 4000) != WAIT_OBJECT_0) TerminateProcess(c->pi.hProcess, 0);  // our own fixture
    CloseHandle(c->pi.hProcess);
    CloseHandle(c->pi.hThread);
    c->pi = {};
}

inline std::string ReadFileText(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline bool WaitMarker(const std::wstring& path, const std::string& text, int timeoutMs = 8000) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < end) {
        if (ReadFileText(path).find(text) != std::string::npos) return true;
        Sleep(50);
    }
    return false;
}


}  // namespace testproc
