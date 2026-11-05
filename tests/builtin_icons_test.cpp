// Velora's own app icons: AppUserModelID / exe / shortcut mapping, and that every SVG rasterises to a filled tile.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "core/win.h"
#include "ui/builtin_icons.h"
#include "ui/icon_tile.h"

using namespace velora::ui;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

// VELORA_ICON_DUMP=<dir>: write each 256 px icon as a raw .bgra file (diagnostics; nothing is written otherwise).
void DumpIfAsked(const char* name, const Pixels& p) {
    char dir[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("VELORA_ICON_DUMP", dir, MAX_PATH)) return;
    FILE* f = std::fopen((std::string(dir) + "\\" + name + ".bgra").c_str(), "wb");
    if (!f) return;
    std::fwrite(p.bgra.data(), 1, p.bgra.size(), f);
    std::fclose(f);
}
}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct { const wchar_t* src; BuiltinIcon want; } map[] = {
        {L"C:\\Windows\\explorer.exe", BuiltinIcon::Files},
        {L"C:\\Program Files\\Velora\\velora-files.exe", BuiltinIcon::VeloraFiles},
        {L"C:\\Program Files\\Velora\\velora-settings.exe", BuiltinIcon::Settings},
        {L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\System Tools\\File Explorer.lnk", BuiltinIcon::Files},
        {L"shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel", BuiltinIcon::Settings},
        {L"shell:AppsFolder\\Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", BuiltinIcon::Terminal},
        {L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", BuiltinIcon::PowerShell},
        {L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Windows PowerShell\\Windows PowerShell.lnk", BuiltinIcon::PowerShell},
        {L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\System Tools\\Command Prompt.lnk", BuiltinIcon::CommandPrompt},
        {L"shell:AppsFolder\\{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\cmd.exe", BuiltinIcon::CommandPrompt},
        {L"shell:AppsFolder\\{F38BF404-1D43-42F2-9305-67DE0B28FC23}\\regedit.exe", BuiltinIcon::RegistryEditor},
        {L"C:\\Users\\x\\Start Menu\\Programs\\Administrative Tools\\Event Viewer.lnk", BuiltinIcon::EventViewer},
        {L"C:\\Users\\x\\Start Menu\\Programs\\Administrative Tools\\Services.lnk", BuiltinIcon::Services},
        {L"C:\\Users\\x\\Start Menu\\Programs\\Administrative Tools\\Computer Management.lnk", BuiltinIcon::ComputerManagement},
        {L"C:\\Users\\x\\Start Menu\\Programs\\Administrative Tools\\Component Services.lnk", BuiltinIcon::ComponentServices},
        {L"C:\\Users\\x\\Start Menu\\Programs\\Administrative Tools\\Disk Cleanup.lnk", BuiltinIcon::DiskCleanup},
        {L"C:\\Users\\x\\Start Menu\\Programs\\Windows Accessories\\Character Map.lnk", BuiltinIcon::CharacterMap},
        {L"shell:AppsFolder\\Microsoft.ScreenSketch_8wekyb3d8bbwe!App", BuiltinIcon::SnippingTool},
        {L"shell:AppsFolder\\Microsoft.Paint_8wekyb3d8bbwe!App", BuiltinIcon::Paint},
        {L"C:\\Program Files\\Application Verifier\\Application Verifier (X64).lnk", BuiltinIcon::None},
        {L"C:\\Windows\\System32\\cmd.exe", BuiltinIcon::CommandPrompt},
        {L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App", BuiltinIcon::Calculator},
        {L"shell:AppsFolder\\Microsoft.Windows.Camera_8wekyb3d8bbwe!App", BuiltinIcon::Camera},
        {L"shell:AppsFolder\\Microsoft.WindowsAlarms_8wekyb3d8bbwe!App", BuiltinIcon::Clock},
        {L"C:\\Windows\\System32\\control.exe", BuiltinIcon::ControlPanel},
        {L"shell:AppsFolder\\Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", BuiltinIcon::Notepad},
        {L"C:\\Windows\\notepad.exe", BuiltinIcon::Notepad},
        {L"C:\\Windows\\System32\\Taskmgr.exe", BuiltinIcon::TaskManager},
        {L"C:\\Program Files\\Firefox\\firefox.exe", BuiltinIcon::None},
        {L"shell:AppsFolder\\Microsoft.MicrosoftEdge_8wekyb3d8bbwe!App", BuiltinIcon::None},
    };
    for (auto& m : map) {
        if (BuiltinIconFor(m.src) != m.want) {
            std::printf("FAIL mapping: %ls -> %s, wanted %s\n", m.src, BuiltinIconName(BuiltinIconFor(m.src)), BuiltinIconName(m.want));
            ++g_failed;
        }
    }

    const BuiltinIcon all[] = {BuiltinIcon::Files, BuiltinIcon::VeloraFiles,      BuiltinIcon::Settings, BuiltinIcon::Terminal,     BuiltinIcon::CommandPrompt,
                               BuiltinIcon::Calculator, BuiltinIcon::Camera,   BuiltinIcon::Clock,        BuiltinIcon::ControlPanel,
                               BuiltinIcon::Notepad,    BuiltinIcon::TaskManager, BuiltinIcon::CharacterMap,
                               BuiltinIcon::ComputerManagement, BuiltinIcon::ComponentServices, BuiltinIcon::DiskCleanup,
                               BuiltinIcon::EventViewer, BuiltinIcon::RegistryEditor, BuiltinIcon::Services, BuiltinIcon::PowerShell,
                               BuiltinIcon::SnippingTool, BuiltinIcon::Paint};
    std::set<std::vector<std::uint8_t>> distinct;
    for (BuiltinIcon i : all) {
        for (int px : {64, 256}) {
            Pixels p;
            const bool ok = RasterizeSvg(BuiltinIconSvg(i), px, &p);
            if (!ok) {
                std::printf("FAIL rasterise %s @%d\n", BuiltinIconName(i), px);
                ++g_failed;
                continue;
            }
            CHECK(p.width == px && p.height == px);
            if (!IsFilledTile(p)) {
                std::printf("FAIL %s @%d is not a filled tile (coverage %.3f)\n", BuiltinIconName(i), px, SquircleCoverage(p));
                ++g_failed;
            }
            if (px == 64) distinct.insert(p.bgra);
            if (px == 256) DumpIfAsked(BuiltinIconName(i), p);
        }
    }
    CHECK(distinct.size() == std::size(all));  // all different designs

    // Blank / low-res guards of the tile path.
    Pixels blank{64, 64, std::vector<std::uint8_t>(64 * 64 * 4, 0)};
    CHECK(IsBlankIcon(blank));
    Pixels white{64, 64, std::vector<std::uint8_t>(64 * 64 * 4, 255)};
    CHECK(IsBlankIcon(white));
    Pixels cmdIcon;
    CHECK(RasterizeSvg(BuiltinIconSvg(BuiltinIcon::CommandPrompt), 64, &cmdIcon) && !IsBlankIcon(cmdIcon));
    Pixels big;
    CHECK(RasterizeSvg(BuiltinIconSvg(BuiltinIcon::Paint), 256, &big) && !IsLowRes(big));
    Pixels flat{256, 256, std::vector<std::uint8_t>(256 * 256 * 4, 90)};
    CHECK(IsLowRes(flat));
    Pixels a = flat, b = flat;
    a.lowRes = true;
    CHECK(ComposeOnTile(a).bgra != ComposeOnTile(b).bgra);  // low-res icons are drawn smaller
    std::printf("builtin_icons_test: %s\n", g_failed ? "FAIL" : "ok");
    return g_failed ? 1 : 0;
}
