#include "core/log.h"

#include "core/runtime.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <system_error>

namespace velora::log {

namespace {

std::mutex g_mutex;
std::filesystem::path g_path;
constexpr std::uintmax_t kMaxBytes = 1024 * 1024;

}  // namespace

void Init(std::string_view processName) {
    std::lock_guard lock(g_mutex);
    if (!g_path.empty()) return;
    auto dir = LogDir();
    if (dir.empty()) return;
    g_path = dir / (std::string(processName) + ".log");
}

std::string Sanitize(std::string_view message) {
    std::string out(message);
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
    return out;
}

void Write(Level level, std::string_view message) {
    std::lock_guard lock(g_mutex);
    if (g_path.empty()) return;

    std::error_code ec;
    if (std::filesystem::file_size(g_path, ec) > kMaxBytes && !ec) {
        auto old = g_path;
        old += L".1";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(g_path, old, ec);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* tag = level == Level::Info ? "INFO" : level == Level::Warn ? "WARN" : "ERROR";
    char head[96];
    std::snprintf(head, sizeof head, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(), tag);
    std::string line = head;
    line.append(Sanitize(message));
    line += '\n';

    HANDLE f = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(f);
}

}  // namespace velora::log
