#include "apps/app_index.h"

#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include "core/json.h"
#include "core/log.h"
#include "core/runtime.h"
#include "core/text.h"

namespace velora::apps {

using Microsoft::WRL::ComPtr;

namespace {

// PKEY_AppUserModel_ID and PKEY_Link_TargetParsingPath, defined here so no extra .lib is needed.
constexpr PROPERTYKEY kPkeyAumid = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
// PKEY_Link_Arguments
constexpr PROPERTYKEY kPkeyLinkArgs = {{0x436F2667, 0x14E2, 0x4FEB, {0xB3, 0x0A, 0x14, 0x6C, 0x53, 0xB5, 0xB6, 0x74}}, 100};
constexpr PROPERTYKEY kPkeyLinkTarget = {{0xB9B4B3FC, 0x2B51, 0x4A42, {0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25}}, 2};

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring TakeString(PWSTR p) {
    std::wstring s = p ? p : L"";
    CoTaskMemFree(p);
    return s;
}

std::wstring PropString(IShellItem2* item, const PROPERTYKEY& key) {
    PWSTR p = nullptr;
    if (FAILED(item->GetString(key, &p))) return {};
    return TakeString(p);
}

std::vector<AppEntry> ScanAppsFolder() {
    std::vector<AppEntry> out;
    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) return out;
    ComPtr<IEnumShellItems> en;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) return out;
    for (;;) {
        ComPtr<IShellItem> item;
        ULONG got = 0;
        if (en->Next(1, &item, &got) != S_OK || got == 0) break;
        PWSTR display = nullptr, parse = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display))) continue;
        std::wstring name = TakeString(display);
        if (FAILED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &parse))) continue;
        std::wstring id = TakeString(parse);
        if (name.empty() || id.empty()) continue;
        AppEntry e;
        e.name = name;
        e.launch = L"shell:AppsFolder\\" + id;
        e.icon = e.launch;
        ComPtr<IShellItem2> item2;
        if (SUCCEEDED(item.As(&item2))) {
            e.aumid = PropString(item2.Get(), kPkeyAumid);
            std::wstring target = PropString(item2.Get(), kPkeyLinkTarget);
            if (target.size() > 4 && Lower(target.substr(target.size() - 4)) == L".exe")
                e.exePath = RealProgram(target, PropString(item2.Get(), kPkeyLinkArgs));
        }
        // Classic shortcuts to documents/readmes show up here too; MSIX apps (with an AUMID) are never noise.
        if (e.aumid.empty() && IsNoiseShortcut(e.name)) continue;
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace

std::optional<AppEntry> ParseUrlShortcut(const std::filesystem::path& p) {
    std::wstring name = p.stem().wstring();
    if (name.empty() || IsNoiseShortcut(name)) return std::nullopt;
    wchar_t url[2048] = {}, icon[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", url, static_cast<DWORD>(std::size(url)), p.c_str());
    if (!url[0]) return std::nullopt;
    const std::wstring u = Lower(url);
    for (const wchar_t* web : {L"http:", L"https:", L"ftp:", L"file:", L"mailto:", L"javascript:"})
        if (u.rfind(web, 0) == 0) return std::nullopt;
    if (u.find(L':') == std::wstring::npos) return std::nullopt;
    GetPrivateProfileStringW(L"InternetShortcut", L"IconFile", L"", icon, static_cast<DWORD>(std::size(icon)), p.c_str());
    AppEntry e;
    e.name = name;
    e.launch = p.wstring();
    std::error_code ec;
    e.icon = icon[0] && std::filesystem::exists(icon, ec) ? std::wstring(icon) : e.launch;
    return e;
}

namespace {

void ScanLinkDir(const std::filesystem::path& dir, IShellLinkW* link, IPersistFile* file, std::vector<AppEntry>* out) {
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        const std::wstring ext = Lower(p.extension().wstring());
        if (ext == L".url") {  // games: Steam, Epic and others leave protocol shortcuts in the Start menu
            if (auto e = ParseUrlShortcut(p)) out->push_back(std::move(*e));
            continue;
        }
        if (ext != L".lnk") continue;
        std::wstring name = p.stem().wstring();
        if (IsNoiseShortcut(name)) continue;
        if (FAILED(file->Load(p.c_str(), STGM_READ))) continue;
        wchar_t target[MAX_PATH * 2] = {};
        if (FAILED(link->GetPath(target, static_cast<int>(std::size(target)), nullptr, SLGP_UNCPRIORITY)) || !target[0]) continue;
        std::wstring t = target;
        if (Lower(std::filesystem::path(t).extension().wstring()) != L".exe") continue;
        wchar_t args[MAX_PATH * 2] = {};
        if (SUCCEEDED(link->GetArguments(args, static_cast<int>(std::size(args))))) t = RealProgram(t, args);
        AppEntry e;
        e.name = name;
        e.exePath = t;
        e.launch = p.wstring();
        e.icon = p.wstring();
        out->push_back(std::move(e));
    }
}

std::vector<AppEntry> ScanStartMenu() {
    std::vector<AppEntry> out;
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return out;
    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file))) return out;
    for (const KNOWNFOLDERID* id : {&FOLDERID_Programs, &FOLDERID_CommonPrograms}) {
        PWSTR p = nullptr;
        if (FAILED(SHGetKnownFolderPath(*id, 0, nullptr, &p))) continue;
        std::filesystem::path dir = TakeString(p);
        ScanLinkDir(dir, link.Get(), file.Get(), &out);
    }
    return out;
}

std::filesystem::path CachePath() {
    auto dir = DataDir();
    return dir.empty() ? std::filesystem::path{} : dir / L"apps_index.json";
}

}  // namespace

int MatchScore(const std::wstring& query, const std::wstring& name) {
    if (query.empty()) return 1;
    std::wstring q = Lower(query), n = Lower(name);
    if (n.rfind(q, 0) == 0) return 100;
    for (size_t i = 1; i < n.size(); ++i)
        if (!std::iswalnum(n[i - 1]) && n.compare(i, q.size(), q) == 0) return 80;
    size_t pos = n.find(q);
    if (pos != std::wstring::npos) return 50;
    size_t at = 0;
    for (wchar_t c : q) {
        if (c == L' ') continue;
        at = n.find(c, at);
        if (at == std::wstring::npos) return 0;
        ++at;
    }
    return 20;
}

bool IsNoiseShortcut(const std::wstring& name) {
    std::wstring n = Lower(name);
    for (const wchar_t* bad : {L"uninstall", L"readme", L"release notes", L"documentation", L"license", L"eula", L"website"}) {
        if (n.find(bad) != std::wstring::npos) return true;
    }
    return n.size() >= 4 && n.compare(n.size() - 4, 4, L"help") == 0;
}

std::vector<AppEntry> MergeEntries(std::vector<std::vector<AppEntry>> sources) {
    std::map<std::wstring, AppEntry> byName;
    for (auto& list : sources) {
        for (auto& e : list) {
            std::wstring key = Lower(e.name);
            auto it = byName.find(key);
            if (it == byName.end()) {
                byName.emplace(key, std::move(e));
            } else if (it->second.exePath.empty() && !e.exePath.empty()) {
                it->second.exePath = e.exePath;
            }
        }
    }
    std::vector<AppEntry> out;
    out.reserve(byName.size());
    for (auto& [k, v] : byName) out.push_back(std::move(v));  // std::map order = lower-case name order
    return out;
}

std::string EntriesToJson(const std::vector<AppEntry>& entries) {
    json::Array arr;
    for (const auto& e : entries) {
        json::Value o;
        o.Set("name", WideToUtf8(e.name));
        o.Set("aumid", WideToUtf8(e.aumid));
        o.Set("exe", WideToUtf8(e.exePath));
        o.Set("launch", WideToUtf8(e.launch));
        o.Set("icon", WideToUtf8(e.icon));
        arr.push_back(std::move(o));
    }
    json::Value root;
    root.Set("version", 1);
    root.Set("apps", std::move(arr));
    return root.Dump();
}

std::vector<AppEntry> EntriesFromJson(const std::string& text) {
    std::vector<AppEntry> out;
    auto doc = json::Parse(text);
    if (!doc || !doc->IsObject()) return out;
    const auto* v = doc->Find("version");
    const auto* apps = doc->Find("apps");
    if (!v || v->AsInt() != 1 || !apps || !apps->IsArray()) return out;
    auto str = [](const json::Value& o, const char* k) {
        const auto* f = o.Find(k);
        return f && f->IsString() ? Utf8ToWide(f->AsString()) : std::wstring{};
    };
    for (const auto& o : apps->AsArray()) {
        if (!o.IsObject()) continue;
        AppEntry e;
        e.name = str(o, "name");
        e.aumid = str(o, "aumid");
        e.exePath = str(o, "exe");
        e.launch = str(o, "launch");
        e.icon = str(o, "icon");
        if (!e.name.empty() && !e.launch.empty()) out.push_back(std::move(e));
    }
    return out;
}

std::vector<size_t> Filter(const std::vector<AppEntry>& entries, const std::wstring& query) {
    std::vector<std::pair<int, size_t>> scored;
    for (size_t i = 0; i < entries.size(); ++i)
        if (int s = MatchScore(query, entries[i].name)) scored.emplace_back(s, i);
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<size_t> out;
    out.reserve(scored.size());
    for (auto& [s, i] : scored) out.push_back(i);
    return out;
}

std::wstring RealProgram(const std::wstring& exe, const std::wstring& args) {
    // Click-to-Run Office starts its tools through the App-V launcher: AppVLP.exe "C:\...\TOOL.EXE".
    if (Lower(std::filesystem::path(exe).filename().wstring()) != L"appvlp.exe" || args.size() < 3 || args[0] != L'"') return exe;
    size_t close = args.find(L'"', 1);
    if (close == std::wstring::npos) return exe;
    std::wstring program = args.substr(1, close - 1);
    return Lower(std::filesystem::path(program).extension().wstring()) == L".exe" ? program : exe;
}

bool IsStaleEntry(const AppEntry& entry, const std::function<bool(const std::wstring&)>& exists) {
    if (entry.exePath.empty() || entry.exePath.find(L'%') != std::wstring::npos) return false;
    return !exists(entry.exePath);
}

std::vector<AppEntry> ScanInstalledApps() {
    auto apps = MergeEntries({ScanAppsFolder(), ScanStartMenu()});
    std::error_code ec;
    std::erase_if(apps, [&](const AppEntry& e) {
        bool stale = IsStaleEntry(e, [&](const std::wstring& p) { return std::filesystem::exists(p, ec); });
        if (stale) log::Info("apps: dropped '" + WideToUtf8(e.name) + "' (its program " + WideToUtf8(e.exePath) + " is gone)");
        return stale;
    });
    return apps;
}

bool Launch(const AppEntry& app) {
    if (app.launch.empty()) return false;
    HINSTANCE r = ShellExecuteW(nullptr, L"open", app.launch.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    bool ok = reinterpret_cast<INT_PTR>(r) > 32;
    if (!ok) log::Warn("apps: launch failed for '" + WideToUtf8(app.name) + "', code " + std::to_string(reinterpret_cast<INT_PTR>(r)));
    return ok;
}

AppIndex& AppIndex::Get() {
    static AppIndex* index = new AppIndex();
    return *index;
}

void AppIndex::LoadCache() {
    auto path = CachePath();
    if (path.empty()) return;
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    auto cached = EntriesFromJson(ss.str());
    if (!cached.empty()) Set(std::move(cached), false);
}

void AppIndex::Set(std::vector<AppEntry> fresh, bool save) {
    {
        std::lock_guard lock(mutex_);
        if (fresh == entries_) return;
        entries_ = fresh;
        ++generation_;
    }
    if (save) {
        auto path = CachePath();
        if (!path.empty()) {
            auto tmp = path;
            tmp += L".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                f << EntriesToJson(fresh);
            }
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (ec) {
                std::filesystem::remove(path, ec);
                std::filesystem::rename(tmp, path, ec);
            }
        }
    }
    if (notify_) PostMessageW(notify_, message_, 0, 0);
}

std::vector<AppEntry> AppIndex::Snapshot() const {
    std::lock_guard lock(mutex_);
    return entries_;
}

unsigned AppIndex::Generation() const {
    std::lock_guard lock(mutex_);
    return generation_;
}

void AppIndex::StartBackground(HWND notify, UINT message) {
    {
        std::lock_guard lock(mutex_);
        if (started_) return;
        started_ = true;
        notify_ = notify;
        message_ = message;
    }
    // Detached: the shell must exit promptly even in the middle of a scan. The index object is leaked, so
    // the thread never touches freed memory.
    std::thread([this] { Run(); }).detach();
}

void AppIndex::Run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<HANDLE> watches;
    for (const KNOWNFOLDERID* id : {&FOLDERID_Programs, &FOLDERID_CommonPrograms}) {
        PWSTR p = nullptr;
        if (FAILED(SHGetKnownFolderPath(*id, 0, nullptr, &p))) continue;
        std::wstring dir = TakeString(p);
        HANDLE h = FindFirstChangeNotificationW(dir.c_str(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
        if (h != INVALID_HANDLE_VALUE) watches.push_back(h);
    }
    constexpr DWORD kPeriodicMs = 30 * 60 * 1000;
    for (;;) {
        Set(ScanInstalledApps(), true);
        if (watches.empty()) {
            Sleep(kPeriodicMs);
            continue;
        }
        DWORD r = WaitForMultipleObjects(static_cast<DWORD>(watches.size()), watches.data(), FALSE, kPeriodicMs);
        if (r < WAIT_OBJECT_0 + watches.size()) {
            Sleep(3000);  // installers write many files: let them finish, then re-arm and rescan once
            for (HANDLE h : watches) {
                FindNextChangeNotification(h);
                while (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) FindNextChangeNotification(h);
            }
        }
    }
}

}  // namespace velora::apps
