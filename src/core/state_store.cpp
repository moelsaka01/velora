#include "core/state_store.h"

#include "core/json.h"
#include "core/text.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fstream>
#include <sstream>

namespace velora {

namespace {

json::Value RectToJson(const Rect& r) {
    json::Object o;
    o.emplace_back("left", r.left);
    o.emplace_back("top", r.top);
    o.emplace_back("right", r.right);
    o.emplace_back("bottom", r.bottom);
    return o;
}

Rect RectFromJson(const json::Value* v) {
    Rect r;
    if (!v) return r;
    auto get = [&](const char* k) {
        const json::Value* f = v->Find(k);
        return f ? static_cast<std::int32_t>(f->AsInt()) : 0;
    };
    r.left = get("left");
    r.top = get("top");
    r.right = get("right");
    r.bottom = get("bottom");
    return r;
}

}  // namespace

std::string SerializeState(const SwitchState& s) {
    json::Array monitors;
    for (const auto& m : s.monitors) {
        json::Object o;
        o.emplace_back("deviceName", WideToUtf8(m.deviceName));
        o.emplace_back("workArea", RectToJson(m.workArea));
        o.emplace_back("taskbarVisible", m.taskbarVisible);
        monitors.emplace_back(std::move(o));
    }
    json::Object root;
    root.emplace_back("schemaVersion", s.schemaVersion);
    root.emplace_back("taskbarAutoHide", s.taskbarAutoHide);
    root.emplace_back("monitors", std::move(monitors));
    root.emplace_back("wallpaperPath", WideToUtf8(s.wallpaperPath));
    root.emplace_back("foregroundHwnd", static_cast<std::int64_t>(s.foregroundHwnd));
    root.emplace_back("shellPid", s.shellPid);
    root.emplace_back("watchdogPid", s.watchdogPid);
    root.emplace_back("shellStartTime", static_cast<std::int64_t>(s.shellStartTime));
    json::Array bars;
    for (std::uint64_t h : s.appbarHwnds) bars.emplace_back(static_cast<std::int64_t>(h));
    root.emplace_back("appbarHwnds", std::move(bars));
    return json::Value(std::move(root)).Dump();
}

std::optional<SwitchState> ParseState(std::string_view text) {
    auto doc = json::Parse(text);
    if (!doc || !doc->IsObject()) return std::nullopt;

    SwitchState s;
    auto num = [&](const char* k, std::int64_t def) {
        const json::Value* v = doc->Find(k);
        return v ? v->AsInt(def) : def;
    };
    s.schemaVersion = static_cast<int>(num("schemaVersion", kStateSchemaVersion));
    if (const auto* v = doc->Find("taskbarAutoHide")) s.taskbarAutoHide = v->AsBool();
    if (const auto* v = doc->Find("wallpaperPath")) s.wallpaperPath = Utf8ToWide(v->AsString());
    s.foregroundHwnd = static_cast<std::uint64_t>(num("foregroundHwnd", 0));
    s.shellPid = static_cast<std::uint32_t>(num("shellPid", 0));
    s.watchdogPid = static_cast<std::uint32_t>(num("watchdogPid", 0));
    s.shellStartTime = static_cast<std::uint64_t>(num("shellStartTime", 0));
    if (const auto* arr = doc->Find("appbarHwnds"); arr && arr->IsArray())
        for (const auto& h : arr->AsArray()) s.appbarHwnds.push_back(static_cast<std::uint64_t>(h.AsInt(0)));
    if (const auto* arr = doc->Find("monitors")) {
        for (const auto& m : arr->AsArray()) {
            MonitorState ms;
            if (const auto* v = m.Find("deviceName")) ms.deviceName = Utf8ToWide(v->AsString());
            ms.workArea = RectFromJson(m.Find("workArea"));
            if (const auto* v = m.Find("taskbarVisible")) ms.taskbarVisible = v->AsBool(true);
            s.monitors.push_back(std::move(ms));
        }
    }
    return s;
}

bool SaveState(const SwitchState& s, const std::filesystem::path& path) {
    if (path.empty()) return false;
    std::string data = SerializeState(s);
    std::filesystem::path tmp = path;
    tmp += L".tmp";

    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() &&
              FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::optional<SwitchState> LoadState(const std::filesystem::path& path) {
    if (path.empty()) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ParseState(ss.str());
}

bool CreateDirtyMarker(const std::filesystem::path& path) {
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = FlushFileBuffers(f) != FALSE;
    CloseHandle(f);
    return ok;
}

bool DirtyMarkerExists(const std::filesystem::path& path) {
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool RemoveDirtyMarker(const std::filesystem::path& path) {
    if (path.empty()) return true;
    // Two restorers can race (`velora-watchdog --restore` terminates the shell, whose own watchdog then starts a
    // restore too): the loser sees the marker in delete-pending state (access denied / sharing violation) while
    // the winner finishes the job. Treat "gone a moment later" as success.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (DeleteFileW(path.c_str())) return true;
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        Sleep(10);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            DWORD e2 = GetLastError();
            if (e2 == ERROR_FILE_NOT_FOUND || e2 == ERROR_PATH_NOT_FOUND) return true;
        }
    }
    return false;
}

}  // namespace velora
