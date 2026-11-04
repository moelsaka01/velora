#include "apps/files_launch.h"

#include <filesystem>

#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"
#include "core/win.h"

namespace velora::apps {

namespace {
// CommandLineToArgvW quoting: backslashes are literal except in front of a quote, which includes the closing one.
std::wstring Quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += c;
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}
}  // namespace

std::wstring FilesCommandLine(const std::wstring& exe, const std::wstring& path, const std::wstring& select) {
    std::wstring cmd = Quote(exe);
    if (!select.empty()) cmd += L" --select " + Quote(select);
    else if (!path.empty()) cmd += L" " + Quote(path);
    return cmd;
}

bool LaunchFilesApp(const std::wstring& path, const std::wstring& select, std::wstring* error) {
    const std::filesystem::path exe = ExeDir() / L"velora-files.exe";
    std::error_code ec;
    if (!std::filesystem::exists(exe, ec)) {
        if (error) *error = L"velora-files.exe was not found next to the shell";
        log::Warn("files launch: " + WideToUtf8(exe.wstring()) + " is missing");
        return false;
    }
    std::wstring cmd = FilesCommandLine(exe.wstring(), path, select);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    const std::wstring dir = ExeDir().wstring();
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        if (error) *error = L"velora-files.exe could not be started (error " + std::to_wstring(GetLastError()) + L")";
        log::Warn("files launch: CreateProcess failed, error " + std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace velora::apps
