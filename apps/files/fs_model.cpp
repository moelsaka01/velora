#include "files/fs_model.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "core/json.h"
#include "core/text.h"
#include "files/file_ops.h"

#pragma comment(lib, "shlwapi.lib")

namespace velora::files {

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring LongPath(const std::wstring& p) {
    if (p.size() < 248 || p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    return L"\\\\?\\" + p;
}

std::wstring ExtOf(const std::wstring& name) {
    const size_t dot = name.rfind(L'.');
    return dot == std::wstring::npos || dot == 0 ? std::wstring{} : Lower(name.substr(dot));
}

std::uint64_t Ticks(const FILETIME& ft) { return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }

}  // namespace

Listing ListDirectory(const std::wstring& dir, bool showHidden) {
    Listing out;
    std::wstring pattern = files::JoinPath(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(LongPath(pattern).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        out.error = GetLastError();
        return out;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        Entry e;
        e.name = fd.cFileName;
        e.path = files::JoinPath(dir, e.name);
        e.attrs = fd.dwFileAttributes;
        e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.hidden = (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
        e.size = e.isDir ? 0 : (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.modified = Ticks(fd.ftLastWriteTime);
        if (e.hidden && !showHidden) continue;
        out.entries.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

void SortEntries(std::vector<Entry>* entries, SortKey key, bool ascending) {
    auto byName = [](const Entry& a, const Entry& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()); };
    std::stable_sort(entries->begin(), entries->end(), [&](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        int c = 0;
        switch (key) {
            case SortKey::Name: c = byName(a, b); break;
            case SortKey::Modified: c = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0; break;
            case SortKey::Size: c = a.isDir ? 0 : (a.size < b.size ? -1 : a.size > b.size ? 1 : 0); break;
            case SortKey::Kind: c = StrCmpLogicalW(ExtOf(a.name).c_str(), ExtOf(b.name).c_str()); break;
        }
        if (c != 0) return ascending ? c < 0 : c > 0;
        return byName(a, b) < 0;
    });
}

std::wstring FormatSize(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + (bytes == 1 ? L" byte" : L" bytes");
    static const wchar_t* units[] = {L"KB", L"MB", L"GB", L"TB"};
    double v = static_cast<double>(bytes);
    int u = -1;
    do {
        v /= 1024.0;
        ++u;
    } while (v >= 1024.0 && u < 3);
    wchar_t buf[32];
    swprintf_s(buf, v < 10 ? L"%.1f %s" : L"%.0f %s", v, units[u]);
    return buf;
}

std::wstring FormatModified(std::uint64_t modified, std::uint64_t now) {
    if (modified == 0) return L"--";
    auto local = [](std::uint64_t t, SYSTEMTIME* st) {
        FILETIME ft{static_cast<DWORD>(t), static_cast<DWORD>(t >> 32)}, lf;
        return FileTimeToLocalFileTime(&ft, &lf) && FileTimeToSystemTime(&lf, st);
    };
    SYSTEMTIME m{}, n{};
    if (!local(modified, &m) || !local(now, &n)) return L"--";
    auto day = [](const SYSTEMTIME& s) { return static_cast<std::int64_t>(s.wYear) * 400 + s.wMonth * 31 + s.wDay; };
    wchar_t time[32] = {};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &m, nullptr, time, static_cast<int>(std::size(time)));
    if (m.wYear == n.wYear && m.wMonth == n.wMonth && m.wDay == n.wDay) return L"Today, " + std::wstring(time);
    // Yesterday: step the clock back one day.
    std::uint64_t dayTicks = 864000000000ULL;
    SYSTEMTIME y{};
    if (now >= dayTicks && local(now - dayTicks, &y) && m.wYear == y.wYear && m.wMonth == y.wMonth && m.wDay == y.wDay)
        return L"Yesterday, " + std::wstring(time);
    (void)day;
    wchar_t date[64] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &m, L"d MMM yyyy", date, static_cast<int>(std::size(date)), nullptr);
    return date;
}

std::wstring KindOf(const Entry& e) {
    if (e.isDir) return L"Folder";
    static std::map<std::wstring, std::wstring> cache;
    const std::wstring ext = ExtOf(e.name);
    if (auto it = cache.find(ext); it != cache.end()) return it->second;
    SHFILEINFOW fi{};
    std::wstring kind;
    if (SHGetFileInfoW(e.name.c_str(), FILE_ATTRIBUTE_NORMAL, &fi, sizeof fi, SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && fi.szTypeName[0])
        kind = fi.szTypeName;
    else
        kind = ext.empty() ? L"Document" : Lower(ext.substr(1)) + L" file";
    if (cache.size() > 512) cache.clear();
    return cache[ext] = kind;
}

std::vector<Crumb> Breadcrumbs(const std::wstring& path) {
    std::vector<Crumb> out;
    std::wstring p = path;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    size_t start = 0;
    if (p.rfind(L"\\\\", 0) == 0) {  // \\server\share
        const size_t a = p.find(L'\\', 2);
        const size_t b = a == std::wstring::npos ? a : p.find(L'\\', a + 1);
        const size_t end = b == std::wstring::npos ? p.size() : b;
        out.push_back({p.substr(0, end), p.substr(0, end)});
        start = end;
    } else if (p.size() >= 2 && p[1] == L':') {
        out.push_back({p.substr(0, 2), p.substr(0, 2) + L"\\"});
        start = 2;
    }
    size_t i = start;
    while (i < p.size()) {
        while (i < p.size() && (p[i] == L'\\' || p[i] == L'/')) ++i;
        if (i >= p.size()) break;
        size_t j = p.find_first_of(L"\\/", i);
        if (j == std::wstring::npos) j = p.size();
        out.push_back({p.substr(i, j - i), p.substr(0, j)});
        i = j;
    }
    return out;
}

std::vector<Place> FavoritePlaces() {
    struct Def {
        const GUID* id;
        const wchar_t* label;
        int glyph;
    };
    // Glyph numbers are files::Glyph in files_window (home, desktop, document, download, picture, music, video).
    const Def defs[] = {
        {&FOLDERID_Profile, L"Home", 0},         {&FOLDERID_Desktop, L"Desktop", 1}, {&FOLDERID_Documents, L"Documents", 2},
        {&FOLDERID_Downloads, L"Downloads", 3}, {&FOLDERID_Pictures, L"Pictures", 4}, {&FOLDERID_Music, L"Music", 5},
        {&FOLDERID_Videos, L"Videos", 6},
    };
    std::vector<Place> out;
    for (const Def& d : defs) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*d.id, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
            if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) out.push_back({d.label, p, d.glyph});
        }
        CoTaskMemFree(p);
    }
    return out;
}

std::vector<DriveInfo> Drives() {
    std::vector<DriveInfo> out;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        DriveInfo d;
        d.path = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        d.type = GetDriveTypeW(d.path.c_str());
        if (d.type == DRIVE_NO_ROOT_DIR || d.type == DRIVE_UNKNOWN) continue;
        wchar_t volume[MAX_PATH] = {};
        if (d.type == DRIVE_FIXED || d.type == DRIVE_REMOVABLE) {
            ULARGE_INTEGER freeToCaller{}, total{}, free{};
            if (GetDiskFreeSpaceExW(d.path.c_str(), &freeToCaller, &total, &free)) {
                d.total = total.QuadPart;
                d.free = free.QuadPart;
            }
            GetVolumeInformationW(d.path.c_str(), volume, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
        } else if (d.type == DRIVE_REMOTE) {
            GetVolumeInformationW(d.path.c_str(), volume, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
        }
        std::wstring letter = d.path.substr(0, 2);
        d.label = (volume[0] ? std::wstring(volume) : (d.type == DRIVE_REMOTE ? L"Network Drive" : d.type == DRIVE_CDROM ? L"Disc Drive" : d.type == DRIVE_REMOVABLE ? L"Removable Disk" : L"Local Disk")) + L" (" + letter + L")";
        out.push_back(std::move(d));
    }
    return out;
}

void History::Visit(const std::wstring& path) {
    if (!list_.empty() && _wcsicmp(list_[pos_].c_str(), path.c_str()) == 0) return;
    if (!list_.empty()) list_.resize(pos_ + 1);
    list_.push_back(path);
    pos_ = list_.size() - 1;
}

std::wstring History::Back() {
    if (!CanBack()) return {};
    return list_[--pos_];
}

std::wstring History::Forward() {
    if (!CanForward()) return {};
    return list_[++pos_];
}

IconKey IconKeyFor(const Entry& e) {
    if (e.isDir) return {L"d:" + Lower(e.path), e.path};
    const std::wstring ext = ExtOf(e.name);
    static const wchar_t* const own[] = {L".exe", L".lnk", L".ico", L".cur", L".scr", L".url", L".msi", L".appref-ms", L".library-ms"};
    for (const wchar_t* o : own)
        if (ext == o) return {L"f:" + Lower(e.path), e.path};
    return {L"e:" + (ext.empty() ? std::wstring(L".") : ext), e.path};
}

// ---- preferences ----
std::string PrefsToJson(const Prefs& p) {
    json::Value v;
    v.Set("view", p.iconView ? "icons" : "list");
    static const char* const names[] = {"name", "modified", "size", "kind"};
    v.Set("sort", names[static_cast<int>(p.sort)]);
    v.Set("ascending", p.ascending);
    v.Set("showHidden", p.showHidden);
    v.Set("width", p.width);
    v.Set("height", p.height);
    return v.Dump();
}

Prefs PrefsFromJson(const std::string& text) {
    Prefs p;
    auto v = json::Parse(text);
    if (!v || !v->IsObject()) return p;
    if (auto* x = v->Find("view"); x && x->IsString()) p.iconView = x->AsString() == "icons";
    if (auto* x = v->Find("sort"); x && x->IsString()) {
        const std::string& s = x->AsString();
        p.sort = s == "modified" ? SortKey::Modified : s == "size" ? SortKey::Size : s == "kind" ? SortKey::Kind : SortKey::Name;
    }
    if (auto* x = v->Find("ascending")) p.ascending = x->AsBool(true);
    if (auto* x = v->Find("showHidden")) p.showHidden = x->AsBool(false);
    if (auto* x = v->Find("width")) p.width = std::clamp(static_cast<int>(x->AsInt(960)), 480, 4000);
    if (auto* x = v->Find("height")) p.height = std::clamp(static_cast<int>(x->AsInt(620)), 320, 3000);
    return p;
}

namespace {
std::string ReadAll(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

Prefs LoadPrefs(const std::wstring& settingsPath) {
    if (settingsPath.empty()) return {};
    auto root = json::Parse(ReadAll(settingsPath));
    if (!root || !root->IsObject()) return {};
    const json::Value* f = root->Find("files");
    return f ? PrefsFromJson(f->Dump()) : Prefs{};
}

bool SavePrefs(const std::wstring& settingsPath, const Prefs& p) {
    if (settingsPath.empty()) return false;
    json::Value root;
    if (auto old = json::Parse(ReadAll(settingsPath)); old && old->IsObject()) root = *old;  // other modules keep their sections
    auto mine = json::Parse(PrefsToJson(p));
    if (!mine) return false;
    root.Set("files", *mine);
    std::filesystem::path path(settingsPath), tmp = path;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << root.Dump();
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

}  // namespace velora::files
