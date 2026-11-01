// icons: the fallback chain (shell image factory -> shortcut icon location -> target exe) against the
// real machine's apps. Read-only. Named apps must load when installed; the letter tile is only for
// sources that really have no icon (a missing file here).
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "apps/app_index.h"
#include "core/text.h"
#include "core/win.h"
#include "ui/ui_common.h"

using namespace velora;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

bool Contains(const std::wstring& hay, const wchar_t* needle) { return ui::Lower(hay).find(ui::Lower(needle)) != std::wstring::npos; }
}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ui::Pixels px;
    std::string how;
    CHECK(!ui::LoadIconPixels(L"C:\\definitely\\not\\here\\nothing.exe", &px, &how));
    CHECK(!ui::LoadIconPixels(L"", &px, &how));
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    CHECK(ui::LoadIconPixels(std::wstring(sys) + L"\\notepad.exe", &px, &how) || ui::LoadIconPixels(std::wstring(sys) + L"\\cmd.exe", &px, &how));
    CHECK(px.width == px.height && px.width > 0 && px.bgra.size() == static_cast<size_t>(px.width) * px.height * 4);

    auto apps = apps::ScanInstalledApps();
    std::map<std::string, int> steps;
    std::vector<std::wstring> failed;
    int loaded = 0;
    for (const auto& a : apps) {
        ui::Pixels p;
        std::string h;
        if (ui::LoadIconPixels(a.icon, &p, &h)) {
            ++loaded;
            ++steps[h];
        } else {
            failed.push_back(a.name);
        }
    }
    std::printf("icons_test: %d of %zu apps got an icon\n", loaded, apps.size());
    for (const auto& [k, v] : steps) std::printf("  %-24s %d\n", k.c_str(), v);
    for (const auto& n : failed) std::printf("  no icon: %s\n", WideToUtf8(n).c_str());

    // The later steps of the chain on their own (shortcut icon location, IExtractIcon, target exe): most
    // apps must still get an icon when the shell image factory is out of the picture.
    std::map<std::string, int> later;
    int laterLoaded = 0, eligible = 0;  // eligible: has a file to fall back to (Win32 shortcut / known exe); MSIX apps often have none
    for (const auto& a : apps) {
        bool hasFile = !a.exePath.empty() || a.icon.find(L"shell:AppsFolder") != 0;
        ui::Pixels p;
        std::string h;
        bool ok = ui::LoadIconPixels(a.icon, &p, &h, true);
        if (hasFile) {
            ++eligible;
            if (ok) ++later[h];
        }
        if (ok) ++laterLoaded;
        if (hasFile && !ok) std::printf("  fallback chain failed for %s\n", WideToUtf8(a.name).c_str());
    }
    std::printf("icons_test: without the shell image factory %d of %zu apps still get an icon\n", laterLoaded, apps.size());
    for (const auto& [k, v] : later) std::printf("  %-24s %d\n", k.c_str(), v);
    int eligibleLoaded = 0;
    for (const auto& [k, v] : later) eligibleLoaded += v;
    std::printf("  of the %d apps with a file to fall back to, %d loaded\n", eligible, eligibleLoaded);
    CHECK(eligible == 0 || eligibleLoaded * 10 >= eligible * 9);

    // Apps named in the spec must never fall back to the letter tile when installed.
    for (const wchar_t* want : {L"Chrome", L"Edge", L"Steam", L"Discord", L"Epic Games", L"Calculator", L"Notepad"}) {
        bool installed = false, ok = true;
        for (const auto& a : apps)
            if (Contains(a.name, want)) {
                installed = true;
                ui::Pixels p;
                std::string h;
                bool got = ui::LoadIconPixels(a.icon, &p, &h);
                std::printf("  %-12s %-40s %s\n", WideToUtf8(want).c_str(), WideToUtf8(a.name).c_str(), got ? h.c_str() : "MISSING");
                ok = ok && got;
            }
        if (installed) CHECK(ok);
        else std::printf("  %-12s not installed here (unmeasured)\n", WideToUtf8(want).c_str());
    }
    wchar_t ci[8] = {};
    if (!GetEnvironmentVariableW(L"CI", ci, 8)) CHECK(loaded * 10 >= static_cast<int>(apps.size()) * 9);  // >= 90 % on a dev machine
    else CHECK(apps.empty() || loaded > 0);

    if (!g_failed) std::printf("icons_test: all passed\n");
    return g_failed ? 1 : 0;
}
