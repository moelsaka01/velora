#include "apple/apple_apps.h"

#include <algorithm>
#include <cwctype>

#include "core/settings_file.h"
#include "core/text.h"
#include "core/win.h"

namespace velora::apple {

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

}  // namespace

const std::vector<ProductInfo>& Products() {
    static const std::vector<ProductInfo> list = {
        {Product::Music, L"Apple Music", L"Apple Music", L"AppleInc.AppleMusicWin_", L""},
        {Product::TV, L"Apple TV", L"Apple TV", L"AppleInc.AppleTVWin_", L""},
        {Product::Devices, L"Apple Devices", L"Apple Devices", L"AppleInc.AppleDevices_", L""},
        {Product::ICloud, L"iCloud", L"iCloud", L"AppleInc.iCloud_", L"icloud.exe"},
        {Product::ITunes, L"iTunes", L"iTunes", L"AppleInc.iTunes_", L"itunes.exe"},
    };
    return list;
}

const ProductInfo* Classify(const apps::AppEntry& e) {
    const std::wstring aumid = Lower(e.aumid.empty() && e.launch.find(L"shell:AppsFolder\\") == 0 ? e.launch.substr(16) : e.aumid);
    const std::wstring exe = Lower(FileNameOf(!e.exePath.empty() ? e.exePath : e.launch));
    const std::wstring name = Lower(e.name);
    for (const ProductInfo& p : Products()) {
        if (p.aumidPrefix[0] && aumid.find(Lower(p.aumidPrefix)) == 0) return &p;
        if (p.exeName[0] && exe == p.exeName) return &p;
        if (name == Lower(p.name)) return &p;
    }
    return nullptr;
}

std::wstring StoreSearchUri(const std::wstring& query) {
    static const char* hex = "0123456789ABCDEF";
    std::wstring out = kStoreSearchPrefix;
    for (unsigned char c : WideToUtf8(query)) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<wchar_t>(c);
        } else {
            out += L'%';
            out += static_cast<wchar_t>(hex[c >> 4]);
            out += static_cast<wchar_t>(hex[c & 15]);
        }
    }
    return out;
}

std::vector<apps::AppEntry> WithAppleGroup(std::vector<apps::AppEntry> index, const Options& options) {
    if (!options.enabled) return index;
    std::vector<apps::AppEntry> group;
    for (const ProductInfo& p : Products()) {
        auto it = std::find_if(index.begin(), index.end(), [&](const apps::AppEntry& e) { return Classify(e) == &p; });
        if (it != index.end()) {
            group.push_back(std::move(*it));
            index.erase(it);
        } else {
            apps::AppEntry get;
            get.name = L"Get " + std::wstring(p.name);
            get.launch = StoreSearchUri(p.storeQuery);
            get.icon = get.launch;
            group.push_back(std::move(get));
        }
    }
    if (options.appStoreWeb) {
        apps::AppEntry web;
        web.name = L"App Store (web)";
        web.launch = kAppStoreUrl;
        web.icon = web.launch;
        group.push_back(std::move(web));
    }
    group.insert(group.end(), std::make_move_iterator(index.begin()), std::make_move_iterator(index.end()));
    return group;
}

}  // namespace velora::apple
