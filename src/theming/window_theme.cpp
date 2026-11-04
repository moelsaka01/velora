#include "theming/window_theme.h"

#include <dwmapi.h>

#include <optional>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"

namespace velora::theming {

namespace {

using Attr = WindowThemer::Attr;
using Entry = WindowThemer::Entry;
constexpr int kAttrCount = WindowThemer::kAttrCount;

struct AttrInfo {
    DWORD id;
    const char* name;
};
// DWMWA_WINDOW_CORNER_PREFERENCE, DWMWA_USE_IMMERSIVE_DARK_MODE, DWMWA_BORDER_COLOR, DWMWA_CAPTION_COLOR,
// DWMWA_TEXT_COLOR, DWMWA_SYSTEMBACKDROP_TYPE (numeric: older SDKs lack some names).
constexpr AttrInfo kInfo[kAttrCount] = {{33, "corner"}, {20, "dark"}, {34, "border"}, {35, "caption"}, {36, "text"}, {38, "backdrop"}};

constexpr DWORD kCornerRound = 2;  // DWMWCP_ROUND
constexpr DWORD kColourDefault = 0xFFFFFFFF;  // DWMWA_COLOR_DEFAULT
bool IsColourAttr(int a) { return a == WindowThemer::kBorder || a == WindowThemer::kCaption || a == WindowThemer::kText; }

DWORD BuildNumber() {
    using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static DWORD build = [] {
        DWORD b = 0;
        if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
            if (auto fn = reinterpret_cast<Fn>(GetProcAddress(nt, "RtlGetVersion"))) {
                OSVERSIONINFOW vi{sizeof vi};
                if (fn(&vi) == 0) b = vi.dwBuildNumber;
            }
        }
        return b;
    }();
    return build;
}

std::uint64_t StartTimeOf(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME c{}, e{}, k{}, u{};
    std::uint64_t t = 0;
    if (GetProcessTimes(h, &c, &e, &k, &u)) t = (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(h);
    return t;
}

std::wstring ClassOf(HWND h) {
    wchar_t buf[256] = {};
    int n = GetClassNameW(h, buf, static_cast<int>(std::size(buf)));
    return std::wstring(buf, n > 0 ? n : 0);
}

bool Get(HWND h, int a, DWORD* v) { return SUCCEEDED(DwmGetWindowAttribute(h, kInfo[a].id, v, sizeof *v)); }
bool Set(HWND h, int a, DWORD v) { return SUCCEEDED(DwmSetWindowAttribute(h, kInfo[a].id, &v, sizeof v)); }

bool Matches(HWND h, const Entry& e) {
    if (!IsWindow(h)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != e.pid || ClassOf(h) != e.className) return false;
    if (e.startTime != 0) {
        const std::uint64_t t = StartTimeOf(pid);
        if (t != 0 && t != e.startTime) return false;  // the window handle was reused by another process
    }
    return true;
}

// Puts back every attribute that still has the value Velora set. Returns true when the window was still there.
bool RestoreEntry(HWND h, const Entry& e) {
    if (!Matches(h, e)) return false;
    for (int a = 0; a < kAttrCount; ++a) {
        const Attr& at = e.attr[a];
        if (!at.recorded) continue;
        DWORD cur = 0;
        if (at.blind) Set(h, a, at.original);
        else if (Get(h, a, &cur) && cur == at.applied) Set(h, a, at.original);
    }
    return true;
}

json::Value EntryToJson(HWND h, const Entry& e) {
    json::Value o;
    o.Set("hwnd", static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(h)));
    o.Set("pid", static_cast<std::int64_t>(e.pid));
    o.Set("start", static_cast<std::int64_t>(e.startTime));
    o.Set("class", WideToUtf8(e.className));
    o.Set("exe", WideToUtf8(e.exe));
    json::Value attrs;
    for (int a = 0; a < kAttrCount; ++a) {
        const Attr& at = e.attr[a];
        if (!at.recorded) continue;
        json::Array pair;
        pair.emplace_back(static_cast<std::int64_t>(at.original));
        pair.emplace_back(static_cast<std::int64_t>(at.applied));
        pair.emplace_back(at.blind);
        attrs.Set(kInfo[a].name, json::Value(std::move(pair)));
    }
    o.Set("attrs", std::move(attrs));
    return o;
}

bool WriteAtomic(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr) && w == text.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::string ReadText(const std::filesystem::path& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (8 << 20)) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr)) got = 0;
        text.resize(got);
    }
    CloseHandle(h);
    return text;
}

}  // namespace

std::filesystem::path BackupPath() {
    auto dir = DataDir();
    return dir.empty() ? std::filesystem::path{} : dir / L"theming_backup.json";
}

WindowFacts GatherFacts(HWND hwnd, bool uncontrollable, bool fullscreen) {
    WindowFacts f;
    f.className = ClassOf(hwnd);
    f.style = static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    f.exStyle = static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    f.visible = IsWindowVisible(hwnd) != FALSE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    f.cloaked = cloaked != 0;
    f.fullscreen = fullscreen;
    f.uncontrollable = uncontrollable;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    f.ownProcess = pid == GetCurrentProcessId();
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2];
        DWORD n = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(p, 0, path, &n)) {
            f.exePath = Lower(std::wstring_view(path, n));
            size_t slash = f.exePath.find_last_of(L"\\/");
            f.exeName = slash == std::wstring::npos ? f.exePath : f.exePath.substr(slash + 1);
        }
        CloseHandle(p);
    }
    return f;
}

int RestoreFromBackup() {
    const auto path = BackupPath();
    if (path.empty()) return 0;
    const std::string text = ReadText(path);
    if (text.empty()) {
        DeleteFileW(path.c_str());
        return 0;
    }
    int restored = 0;
    if (auto doc = json::Parse(text); doc && doc->IsObject()) {
        if (const auto* ws = doc->Find("windows"); ws && ws->IsArray()) {
            for (const auto& w : ws->AsArray()) {
                if (!w.IsObject()) continue;
                const auto* hv = w.Find("hwnd");
                const auto* pv = w.Find("pid");
                const auto* cv = w.Find("class");
                const auto* av = w.Find("attrs");
                if (!hv || !pv || !cv || !av || !av->IsObject()) continue;
                Entry e;
                e.pid = static_cast<std::uint32_t>(pv->AsInt());
                if (const auto* sv = w.Find("start")) e.startTime = static_cast<std::uint64_t>(sv->AsInt());
                e.className = Utf8ToWide(cv->AsString());
                for (int a = 0; a < kAttrCount; ++a) {
                    const auto* pair = av->Find(kInfo[a].name);
                    if (!pair || !pair->IsArray() || pair->AsArray().size() < 2) continue;
                    e.attr[a].recorded = true;
                    e.attr[a].original = static_cast<std::uint32_t>(pair->AsArray()[0].AsInt());
                    e.attr[a].applied = static_cast<std::uint32_t>(pair->AsArray()[1].AsInt());
                    if (pair->AsArray().size() > 2) e.attr[a].blind = pair->AsArray()[2].AsBool();
                }
                HWND h = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(hv->AsInt()));
                if (RestoreEntry(h, e)) ++restored;
            }
        }
    } else {
        log::Warn("theming: backup unreadable; nothing to restore");
    }
    DeleteFileW(path.c_str());
    log::Info("theming: restored " + std::to_string(restored) + " window(s) from the backup");
    return restored;
}

void WindowThemer::Save() const {
    const auto path = BackupPath();
    if (path.empty()) return;
    if (entries_.empty()) {
        DeleteFileW(path.c_str());
        return;
    }
    json::Array list;
    for (const auto& [h, e] : entries_) list.push_back(EntryToJson(h, e));
    json::Value root;
    root.Set("version", 1);
    root.Set("windows", json::Value(std::move(list)));
    if (!WriteAtomic(path, root.Dump())) log::Warn("theming: could not write the backup");
}

void WindowThemer::Sync(HWND hwnd, Entry& e) {
    const Colours col = NeutralColours(dark_);
    const bool backdropOk = BuildNumber() >= 22621;
    std::optional<DWORD> want[kAttrCount];
    if (config_.corners) want[kCorner] = kCornerRound;
    if (config_.caption) {
        want[kDark] = dark_ ? 1u : 0u;
        want[kBorder] = static_cast<DWORD>(col.border);
        if (config_.backdrop == Backdrop::None) {  // with a backdrop the caption stays default so the material shows
            want[kCaption] = static_cast<DWORD>(col.caption);
            want[kText] = static_cast<DWORD>(col.text);
        }
    }
    if (config_.backdrop != Backdrop::None && backdropOk)
        want[kBackdrop] = config_.backdrop == Backdrop::Mica ? 2u : config_.backdrop == Backdrop::Acrylic ? 3u : 4u;

    for (int a = 0; a < kAttrCount; ++a) {
        Attr& at = e.attr[a];
        DWORD cur = 0;
        const bool readable = Get(hwnd, a, &cur);
        if (at.recorded && at.blind) {
            if (!want[a]) {
                Set(hwnd, a, at.original);
                at.recorded = false;
            } else if (*want[a] != at.applied && Set(hwnd, a, *want[a])) {
                at.applied = *want[a];
            }
            continue;
        }
        if (at.recorded) {
            if (!readable || cur != at.applied) {  // the app (or the window) took over: not ours any more
                at.recorded = false;
                continue;
            }
            if (!want[a]) {
                Set(hwnd, a, at.original);
                at.recorded = false;
            } else if (*want[a] != at.applied) {
                if (Set(hwnd, a, *want[a])) at.applied = *want[a];
            }
            continue;
        }
        if (!want[a]) continue;
        if (!readable && IsColourAttr(a)) {
            if (!Set(hwnd, a, *want[a])) {
                ++stats_.attributesRefused;
                continue;
            }
            at.recorded = true;
            at.blind = true;
            at.original = kColourDefault;
            at.applied = *want[a];
            ++stats_.attributesChanged;
            continue;
        }
        if (!readable) {  // never change what cannot be put back
            ++stats_.attributesUnreadable;
            continue;
        }
        if (cur == *want[a]) continue;
        if (!Set(hwnd, a, *want[a])) {
            ++stats_.attributesRefused;
            continue;
        }
        DWORD after = 0;
        if (!Get(hwnd, a, &after) || after != *want[a]) {  // accepted but not visible: undo and remember it did not work
            Set(hwnd, a, cur);
            ++stats_.attributesIgnored;
            continue;
        }
        at.recorded = true;
        at.original = cur;
        at.applied = *want[a];
        ++stats_.attributesChanged;
    }
}

Skip WindowThemer::Apply(HWND hwnd, bool uncontrollable, bool fullscreen) {
    const WindowFacts f = GatherFacts(hwnd, uncontrollable, fullscreen);
    const Skip s = Evaluate(f, config_);
    auto it = entries_.find(hwnd);
    if (s != Skip::None) {
        if (it != entries_.end() && s != Skip::Fullscreen && s != Skip::Hidden && s != Skip::Cloaked) {
            RestoreEntry(hwnd, it->second);  // newly excluded / disabled
            entries_.erase(it);
            Save();
        }
        return s;
    }
    const bool isNew = it == entries_.end();
    if (isNew) {
        Entry e;
        GetWindowThreadProcessId(hwnd, reinterpret_cast<DWORD*>(&e.pid));
        e.startTime = StartTimeOf(e.pid);
        e.className = f.className;
        e.exe = f.exeName;
        it = entries_.emplace(hwnd, std::move(e)).first;
    }
    const Stats before = stats_;
    Sync(hwnd, it->second);
    bool any = false;
    for (const auto& at : it->second.attr) any = any || at.recorded;
    if (!any) {
        entries_.erase(it);
        if (!isNew) Save();
        return Skip::None;  // nothing needed changing (or nothing was possible); not recorded
    }
    Save();
    if (isNew) {
        ++stats_.themed;
        const Entry& e = entries_.at(hwnd);
        std::string line = "theming: " + WideToUtf8(f.exeName) + " [" + WideToUtf8(f.className) + "]";
        for (int a = 0; a < kAttrCount; ++a) line += std::string(" ") + kInfo[a].name + "=" + (e.attr[a].recorded ? "set" : "-");
        line += " ignored=" + std::to_string(stats_.attributesIgnored - before.attributesIgnored) +
                " refused=" + std::to_string(stats_.attributesRefused - before.attributesRefused) +
                " unreadable=" + std::to_string(stats_.attributesUnreadable - before.attributesUnreadable);
        log::Info(line);
    }
    return Skip::None;
}

void WindowThemer::Configure(const Config& config, bool dark) {
    config_ = config;
    dark_ = dark;
    std::vector<HWND> gone;
    for (auto& [h, e] : entries_) {
        if (!IsWindow(h)) {
            gone.push_back(h);
            continue;
        }
        const WindowFacts f = GatherFacts(h, false, false);
        if (Evaluate(f, config_) != Skip::None) {
            RestoreEntry(h, e);
            gone.push_back(h);
            continue;
        }
        Sync(h, e);
        bool any = false;
        for (const auto& at : e.attr) any = any || at.recorded;
        if (!any) gone.push_back(h);
    }
    for (HWND h : gone) entries_.erase(h);
    Save();
}

void WindowThemer::Forget(HWND hwnd) {
    if (entries_.erase(hwnd)) Save();
}

int WindowThemer::RestoreAll() {
    int n = 0;
    for (auto& [h, e] : entries_)
        if (RestoreEntry(h, e)) ++n;
    entries_.clear();
    Save();  // deletes the backup
    if (n) log::Info("theming: restored " + std::to_string(n) + " window(s)");
    return n;
}

}  // namespace velora::theming
