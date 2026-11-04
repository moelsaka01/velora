#include "dock/dock_model.h"

#include <algorithm>
#include <cwctype>

#include "core/json.h"
#include "core/text.h"

namespace velora::dock {

bool Ieq(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

bool DockItem::Active() const {
    return std::any_of(windows.begin(), windows.end(), [](const DockWindow& w) { return w.foreground; });
}

bool DockItem::AnyUncontrollable() const {
    return std::any_of(windows.begin(), windows.end(), [](const DockWindow& w) { return !w.controllable; });
}

bool Matches(const DockApp& app, const DockWindow& w) {
    if (app.internalApps) return false;
    if (!app.aumid.empty() && !w.aumid.empty()) return Ieq(app.aumid, w.aumid);
    return !app.exePath.empty() && !w.exePath.empty() && Ieq(app.exePath, w.exePath);
}

namespace {
bool IsFrameHost(const std::wstring& exe) {
    size_t slash = exe.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? exe : exe.substr(slash + 1);
    return Ieq(name, L"applicationframehost.exe");
}
}  // namespace

namespace {
std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool IsFilesystemTarget(const std::wstring& launch) {
    return launch.find(L"shell:") != 0 && launch.find(L"://") == std::wstring::npos && launch.find(L"ms-") != 0;
}
}  // namespace

std::vector<DockApp> VisiblePinned(const std::vector<DockApp>& pinned,
                                   const std::function<bool(const std::wstring&)>& fileExists) {
    std::vector<DockApp> out;
    out.reserve(pinned.size());
    for (const auto& a : pinned) {
        if (a.internalApps) {
            out.push_back(a);
            continue;
        }
        const std::wstring& target = !a.exePath.empty() ? a.exePath : a.launch;
        if (IsFilesystemTarget(a.launch) && !fileExists(a.launch)) continue;
        out.push_back(a);
    }
    return out;
}

std::vector<DockItem> BuildItems(const std::vector<DockApp>& pinned, const std::vector<DockWindow>& windows,
                                 const std::function<DockApp(const DockWindow&)>& describe) {
    std::vector<DockItem> items;
    items.reserve(pinned.size() + 8);
    for (const auto& p : pinned) {
        DockItem it;
        it.app = p;
        it.pinned = true;
        items.push_back(std::move(it));
    }
    const size_t pinnedCount = items.size();
    for (const auto& w : windows) {
        bool placed = false;
        for (size_t i = 0; i < pinnedCount; ++i) {
            if (Matches(items[i].app, w)) {
                items[i].windows.push_back(w);
                placed = true;
                break;
            }
        }
        if (placed) continue;
        // Unpinned: group with an earlier running app that shares the same identity.
        DockWindow probe = w;
        if (IsFrameHost(probe.exePath)) probe.exePath.clear();  // the host process is not the app
        bool grouped = false;
        for (size_t i = pinnedCount; i < items.size(); ++i) {
            if (Matches(items[i].app, probe)) {
                items[i].windows.push_back(w);
                grouped = true;
                break;
            }
        }
        if (grouped) continue;
        DockItem it;
        it.app = describe(probe);
        it.windows.push_back(w);
        items.push_back(std::move(it));
    }
    return items;
}

ClickAction DecideClick(const DockItem& item) {
    if (item.app.internalApps) return {ClickKind::Launch, 0};
    if (!item.Running()) return {ClickKind::Launch, 0};
    if (item.Active()) {
        if (item.windows.size() == 1) {
            const DockWindow& w = item.windows[0];
            return w.controllable ? ClickAction{ClickKind::Minimize, w.hwnd} : ClickAction{ClickKind::None, 0};
        }
        // Cycle to the window after the foreground one.
        for (size_t i = 0; i < item.windows.size(); ++i)
            if (item.windows[i].foreground) return {ClickKind::Activate, item.windows[(i + 1) % item.windows.size()].hwnd};
    }
    for (const auto& w : item.windows)
        if (!w.minimized) return {ClickKind::Activate, w.hwnd};
    return {ClickKind::Activate, item.windows.front().hwnd};
}

std::string PinnedToJson(const std::vector<DockApp>& pinned) {
    json::Array arr;
    for (const auto& a : pinned) {
        json::Value o;
        o.Set("name", WideToUtf8(a.name));
        o.Set("launch", WideToUtf8(a.launch));
        o.Set("icon", WideToUtf8(a.icon));
        o.Set("aumid", WideToUtf8(a.aumid));
        o.Set("exe", WideToUtf8(a.exePath));
        o.Set("apps", a.internalApps);
        arr.push_back(std::move(o));
    }
    json::Value dock;
    dock.Set("pinned", std::move(arr));
    return dock.Dump();
}

bool PinnedFromJson(const std::string& settingsJson, std::vector<DockApp>* out) {
    auto doc = json::Parse(settingsJson);
    if (!doc || !doc->IsObject()) return false;
    const auto* dock = doc->Find("dock");
    const auto* arr = dock ? dock->Find("pinned") : nullptr;
    if (!arr || !arr->IsArray()) return false;
    auto str = [](const json::Value& o, const char* k) {
        const auto* f = o.Find(k);
        return f && f->IsString() ? Utf8ToWide(f->AsString()) : std::wstring{};
    };
    std::vector<DockApp> list;
    for (const auto& o : arr->AsArray()) {
        if (!o.IsObject()) continue;
        DockApp a;
        a.name = str(o, "name");
        a.launch = str(o, "launch");
        a.icon = str(o, "icon");
        a.aumid = str(o, "aumid");
        a.exePath = str(o, "exe");
        const auto* apps = o.Find("apps");
        a.internalApps = apps && apps->AsBool();
        if (a.name.empty() || (a.launch.empty() && !a.internalApps)) continue;
        list.push_back(std::move(a));
    }
    *out = std::move(list);
    return true;
}

}  // namespace velora::dock
