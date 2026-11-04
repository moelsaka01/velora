#include "settings/settings_model.h"

#include <algorithm>
#include <cwctype>

#include "core/text.h"
#include "core/win.h"

namespace velora::settings {

namespace {

Item Toggle(const char* key, const wchar_t* label, const wchar_t* detail, bool def, const char* dependsOn = "") {
    Item i;
    i.kind = ItemKind::Toggle;
    i.key = key;
    i.label = label;
    i.detail = detail;
    i.defBool = def;
    i.dependsOn = dependsOn;
    return i;
}

Item Pick(const char* key, const wchar_t* label, const wchar_t* detail, const char* def, std::vector<Choice> choices, const char* dependsOn = "") {
    Item i;
    i.kind = ItemKind::Choice;
    i.key = key;
    i.label = label;
    i.detail = detail;
    i.defString = def;
    i.choices = std::move(choices);
    i.dependsOn = dependsOn;
    return i;
}

Item Button(const char* action, const wchar_t* label, const wchar_t* detail) {
    Item i;
    i.kind = ItemKind::Button;
    i.action = action;
    i.label = label;
    i.detail = detail;
    return i;
}

Item Note(const wchar_t* text) {
    Item i;
    i.kind = ItemKind::Note;
    i.detail = text;
    return i;
}

Item List(const char* key, const wchar_t* label, const wchar_t* detail, const char* dependsOn = "") {
    Item i;
    i.kind = ItemKind::List;
    i.key = key;
    i.label = label;
    i.detail = detail;
    i.dependsOn = dependsOn;
    return i;
}

}  // namespace

std::vector<Page> BuildPages() {
    std::vector<Page> p;
    p.push_back({L"General",
                 {Toggle("startup.launchAtLogin", L"Open Velora when I sign in", L"Adds Velora to your Windows startup apps (current user). Off by default.", false),
                  Button("restore-windows", L"Restore Windows desktop", L"Brings back the taskbar, your wallpaper and every window style Velora changed, right now."),
                  Note(L"If Velora ever stops responding, Ctrl+Alt+Shift+Esc also restores Windows.")}});
    p.push_back({L"Appearance",
                 {Toggle("wallpaper.enabled", L"Velora wallpaper", L"Shows Velora's own wallpaper while Velora Mode is on. Your wallpaper comes back when you exit.", true),
                  Note(L"Light and dark follow the Windows app mode (Windows Settings > Personalization > Colors).")}});
    p.push_back({L"Dock",
                 {Toggle("dock.magnification", L"Magnification", L"Icons grow under the pointer.", true),
                  Note(L"The Dock lists your pinned apps and the apps that are running.")}});
    p.push_back({L"Window theming",
                 {Toggle("theming.enabled", L"Theme other apps' windows", L"Rounded corners, dark or light caption and a neutral border through Windows' own window styles. Nothing is injected into apps.", true),
                  Toggle("theming.corners", L"Rounded corners", L"", true, "theming.enabled"),
                  Toggle("theming.caption", L"Dark / light caption and border", L"Follows the Windows app mode.", true, "theming.enabled"),
                  Pick("theming.backdrop", L"Title bar material", L"Takes effect where the app allows it.", "mica",
                       {{"mica", L"Mica"}, {"acrylic", L"Acrylic"}, {"tabbed", L"Tabbed"}, {"none", L"None"}}, "theming.enabled"),
                  List("theming.exclude", L"Never theme these apps", L"Type an app's file name, for example notepad++.exe.", "theming.enabled"),
                  Note(L"Elevated, fullscreen, game and anti-cheat windows are never touched. Turning this off puts every window back.")}});
    p.push_back({L"Menu bar",
                 {Toggle("menus.probeChromium", L"Show menus of Chrome, Edge and Electron apps", L"Turns on accessibility in those apps, which can slow them down. Off by default.", false),
                  Note(L"Apps whose menus cannot be read show only their name.")}});
    p.push_back({L"Hotkeys",
                 {Pick("search.hotkey", L"Search", L"The key combination that opens Search. If another program owns it, the next free one is used.", "Alt+Space",
                       {{"Alt+Space", L"Alt+Space"},
                        {"Ctrl+Space", L"Ctrl+Space"},
                        {"Ctrl+Alt+Space", L"Ctrl+Alt+Space"},
                        {"Alt+Shift+Space", L"Alt+Shift+Space"},
                        {"Ctrl+Shift+Space", L"Ctrl+Shift+Space"},
                        {"none", L"None"}}),
                  Note(L"Hotkey changes apply when Velora restarts.")}});
    p.push_back({L"Gaming mode",
                 {Pick("gaming.mode", L"Gaming mode", L"Turns animations, blur and background work off while a game is in front. Velora never touches the game itself.", "auto",
                       {{"auto", L"Automatic"}, {"on", L"Always on"}, {"off", L"Off"}}),
                  Note(L"Automatic = when a fullscreen or borderless-fullscreen app is in front.")}});
    p.push_back({L"Apple services",
                 {Toggle("apple.enabled", L"Show Apple apps", L"Adds an Apple group to Apps for Apple Music, Apple TV, Apple Devices, iCloud and iTunes when they are installed. Off by default.", false),
                  Toggle("apple.icloudFolders", L"iCloud in Files", L"Shows iCloud Drive and iCloud Photos in the Files sidebar when iCloud for Windows is set up.", true, "apple.enabled"),
                  Toggle("apple.appStoreWeb", L"App Store (web)", L"A link to apps.apple.com in your browser. Browse only.", true, "apple.enabled"),
                  Note(L"Velora never asks for, reads or stores your Apple Account. Sign in only inside Apple's own apps.")}});
    p.push_back({L"Updates",
                 {Pick("updates.channel", L"Channel", L"", "stable", {{"stable", L"Stable"}, {"preview", L"Preview"}}),
                  Button("check-updates", L"Open the releases page", L"Updates are installed by you. Velora does not download anything on its own."),
                  Note(L"Check the SHA-256 checksum on the release page before running an installer.")}});
    return p;
}

// ---- store ----------------------------------------------------------------------------------------------------------

bool Store::Load() {
    root_ = json::Value{};
    HANDLE h = CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    std::string text;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (4 << 20)) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr)) got = 0;
        text.resize(got);
    }
    CloseHandle(h);
    if (text.empty()) return true;
    auto doc = json::Parse(text);
    if (!doc || !doc->IsObject()) return false;
    root_ = std::move(*doc);
    return true;
}

bool Store::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    std::filesystem::path tmp = file_;
    tmp += L".tmp";
    const std::string text = (root_.IsObject() ? root_ : json::Value(json::Object{})).Dump();
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr) && w == text.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

const json::Value* Store::Lookup(const std::string& key) const {
    const json::Value* v = &root_;
    size_t pos = 0;
    while (v) {
        const size_t dot = key.find('.', pos);
        v = v->Find(std::string_view(key).substr(pos, dot == std::string::npos ? std::string::npos : dot - pos));
        if (dot == std::string::npos) return v;
        pos = dot + 1;
    }
    return nullptr;
}

void Store::SetValue(const std::string& key, json::Value v) {
    // Rebuild the chain of objects along the path (copies are small; unknown sibling keys are kept).
    std::vector<std::string> parts;
    for (size_t pos = 0;;) {
        const size_t dot = key.find('.', pos);
        parts.push_back(key.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos));
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    std::vector<json::Value> chain;  // the existing object at each level (or empty)
    const json::Value* cur = &root_;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const json::Value* child = cur ? cur->Find(parts[i]) : nullptr;
        chain.push_back(child && child->IsObject() ? *child : json::Value(json::Object{}));
        cur = child;
    }
    json::Value value = std::move(v);
    for (size_t i = parts.size(); i-- > 1;) {
        chain[i - 1].Set(parts[i], std::move(value));
        value = std::move(chain[i - 1]);
    }
    if (!root_.IsObject()) root_ = json::Value(json::Object{});
    root_.Set(parts[0], std::move(value));
}

bool Store::GetBool(const std::string& key, bool def) const {
    const json::Value* v = Lookup(key);
    return v ? v->AsBool(def) : def;
}

std::string Store::GetString(const std::string& key, const std::string& def) const {
    const json::Value* v = Lookup(key);
    return v && v->IsString() ? v->AsString() : def;
}

std::vector<std::string> Store::GetList(const std::string& key) const {
    std::vector<std::string> out;
    const json::Value* v = Lookup(key);
    if (v && v->IsArray())
        for (const auto& e : v->AsArray())
            if (e.IsString()) out.push_back(e.AsString());
    return out;
}

void Store::SetBool(const std::string& key, bool v) { SetValue(key, json::Value(v)); }
void Store::SetString(const std::string& key, const std::string& v) { SetValue(key, json::Value(v)); }
void Store::SetList(const std::string& key, const std::vector<std::string>& v) {
    json::Array a;
    for (const auto& s : v) a.emplace_back(s);
    SetValue(key, json::Value(std::move(a)));
}

bool ItemEnabled(const Store& s, const Item& item) { return item.dependsOn.empty() || s.GetBool(item.dependsOn, true); }

int ChoiceIndex(const Store& s, const Item& item) {
    if (item.choices.empty()) return -1;
    const std::string cur = s.GetString(item.key, item.defString);
    for (size_t i = 0; i < item.choices.size(); ++i)
        if (item.choices[i].value == cur) return static_cast<int>(i);
    for (size_t i = 0; i < item.choices.size(); ++i)
        if (item.choices[i].value == item.defString) return static_cast<int>(i);
    return 0;
}

std::string NormalizeExeName(const std::string& typed) {
    std::wstring w = Utf8ToWide(typed);
    auto bad = [](wchar_t c) { return c < 32 || std::wstring_view(L"\\/:*?\"<>|").find(c) != std::wstring_view::npos; };
    while (!w.empty() && (std::iswspace(w.front()) || w.front() == L'"')) w.erase(w.begin());
    while (!w.empty() && (std::iswspace(w.back()) || w.back() == L'"')) w.pop_back();
    if (w.empty() || w.size() > 100 || std::any_of(w.begin(), w.end(), bad)) return {};
    std::transform(w.begin(), w.end(), w.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (w.find(L'.') == std::wstring::npos) w += L".exe";
    return WideToUtf8(w);
}

}  // namespace velora::settings
