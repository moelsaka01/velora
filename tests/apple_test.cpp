// Apple services: classification of Start entries, the "Apple" group in the launcher index, Store search links, iCloud folders.
// Pure apart from temp folders. Nothing here contacts Apple or reads anything of an Apple account.
#include <cstdio>
#include <filesystem>

#include "apple/apple_apps.h"

using namespace velora;
using namespace velora::apple;
using velora::apps::AppEntry;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

static AppEntry Entry(const wchar_t* name, const wchar_t* aumid = L"", const wchar_t* exe = L"") {
    AppEntry e;
    e.name = name;
    e.aumid = aumid;
    e.exePath = exe;
    e.launch = L"shell:AppsFolder\\" + std::wstring(name);
    e.icon = e.launch;
    return e;
}

int main() {
    // Classification.
    auto kind = [](const AppEntry& e) { return Classify(e) ? static_cast<int>(Classify(e)->product) : -1; };
    CHECK(kind(Entry(L"Whatever", L"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App")) == static_cast<int>(Product::Music));
    CHECK(kind(Entry(L"Apple TV")) == static_cast<int>(Product::TV));
    CHECK(kind(Entry(L"x", L"AppleInc.AppleDevices_nzyj5cx40ttqa!App")) == static_cast<int>(Product::Devices));
    CHECK(kind(Entry(L"iCloud", L"", L"C:\\Program Files\\Common Files\\Apple\\Internet Services\\iCloud.exe")) == static_cast<int>(Product::ICloud));
    CHECK(kind(Entry(L"Some shortcut", L"", L"C:\\Program Files\\iTunes\\iTunes.exe")) == static_cast<int>(Product::ITunes));
    CHECK(kind(Entry(L"iTunes")) == static_cast<int>(Product::ITunes));
    CHECK(kind(Entry(L"Pineapple Notes")) == -1 && kind(Entry(L"Apple Music Helper")) == -1 && kind(Entry(L"Notepad")) == -1);
    CHECK(kind(Entry(L"iTunesHelper", L"", L"C:\\x\\iTunesHelper.exe")) == -1);  // exact names only

    // Group: off by default -> unchanged.
    std::vector<AppEntry> index = {Entry(L"Calculator"), Entry(L"Apple Music", L"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App"), Entry(L"Zoom"),
                                   Entry(L"iCloud", L"", L"C:\\icloud.exe")};
    Options off;
    CHECK(WithAppleGroup(index, off) == index);

    Options on;
    on.enabled = true;
    auto g = WithAppleGroup(index, on);
    CHECK(g.size() == index.size() + 3 + 1);  // 2 installed moved to the front, 3 missing added, + App Store (web)
    CHECK(g[0].name == L"Apple Music" && g[0].aumid == index[1].aumid);  // the installed app is kept as it is (its own icon / launch)
    CHECK(g[1].name == L"Get Apple TV" && g[1].launch == L"ms-windows-store://search/?query=Apple%20TV");
    CHECK(g[2].name == L"Get Apple Devices");
    CHECK(g[3].name == L"iCloud" && g[3].exePath == L"C:\\icloud.exe");
    CHECK(g[4].name == L"Get iTunes");
    CHECK(g[5].name == L"App Store (web)" && g[5].launch == kAppStoreUrl);
    CHECK(g[6].name == L"Calculator" && g[7].name == L"Zoom");  // the rest keeps its order

    on.appStoreWeb = false;
    g = WithAppleGroup(index, on);
    CHECK(g.size() == index.size() + 3);
    for (const auto& e : g) CHECK(e.name != L"App Store (web)");
    // The same index twice gives the same result (no state).
    CHECK(WithAppleGroup(index, on) == g);
    // Two entries of one product: the first moves, the other stays in the rest.
    {
        auto two = index;
        two.push_back(Entry(L"Apple Music"));
        auto r = WithAppleGroup(two, on);
        int music = 0;
        for (const auto& e : r) music += e.name == L"Apple Music";
        CHECK(music == 2 && r[0].aumid == index[1].aumid);
    }

    // Store search links: percent-encoded, no product ids.
    CHECK(StoreSearchUri(L"Apple Music") == L"ms-windows-store://search/?query=Apple%20Music");
    CHECK(StoreSearchUri(L"a&b=c") == L"ms-windows-store://search/?query=a%26b%3Dc");
    CHECK(StoreSearchUri(L"caf\u00e9") == L"ms-windows-store://search/?query=caf%C3%A9");

    // iCloud folders: only those that exist; Photos from either place.
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::filesystem::path root = std::filesystem::path(tmp) / (L"velora-icloud-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root / L"home");
        std::filesystem::create_directories(root / L"pictures");
        CHECK(ICloudFoldersIn((root / L"home").wstring(), (root / L"pictures").wstring()).empty());
        std::filesystem::create_directories(root / L"home" / L"iCloudDrive");
        auto f = ICloudFoldersIn((root / L"home").wstring(), (root / L"pictures").wstring());
        CHECK(f.size() == 1 && f[0].label == L"iCloud Drive");
        std::filesystem::create_directories(root / L"pictures" / L"iCloud Photos");
        f = ICloudFoldersIn((root / L"home").wstring(), (root / L"pictures").wstring());
        CHECK(f.size() == 2 && f[1].label == L"iCloud Photos" && f[1].path == (root / L"pictures" / L"iCloud Photos").wstring());
        Options o;
        CHECK(ICloudFolders(o).empty());  // off by default
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
