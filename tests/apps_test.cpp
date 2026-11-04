// apps: matching/filtering, merge, JSON cache round trip, grid layout math, and one real scan
// (read-only: AppsFolder + Start-menu shortcuts). Never launches anything.
#include <cmath>
#include <cstdio>

#include "apps/app_index.h"
#include "apps/apps_grid.h"
#include "apps/files_launch.h"
#include "core/win.h"

using namespace velora::apps;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

AppEntry E(const wchar_t* name, const wchar_t* exe = L"") {
    AppEntry e;
    e.name = name;
    e.exePath = exe;
    e.launch = std::wstring(L"shell:AppsFolder\\") + name;
    e.icon = e.launch;
    return e;
}
}  // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // MatchScore ordering: prefix > word prefix > substring > subsequence > none.
    CHECK(MatchScore(L"", L"Anything") > 0);
    int prefix = MatchScore(L"note", L"Notepad");
    int word = MatchScore(L"pad", L"Note Pad");
    int sub = MatchScore(L"tepa", L"Notepad");
    int subseq = MatchScore(L"ntpd", L"Notepad");
    CHECK(prefix > word && word > sub && sub > subseq && subseq > 0);
    CHECK(MatchScore(L"zzz", L"Notepad") == 0);
    CHECK(MatchScore(L"NOTE", L"notepad") == prefix);  // case-insensitive

    // velora-files command line: quoting survives spaces and a trailing backslash.
    CHECK(FilesCommandLine(L"C:\\v\\velora-files.exe", L"", L"") == L"\"C:\\v\\velora-files.exe\"");
    CHECK(FilesCommandLine(L"C:\\v\\f.exe", L"C:\\My Docs", L"") == L"\"C:\\v\\f.exe\" \"C:\\My Docs\"");
    CHECK(FilesCommandLine(L"C:\\v\\f.exe", L"C:\\", L"") == L"\"C:\\v\\f.exe\" \"C:\\\\\"");
    CHECK(FilesCommandLine(L"f.exe", L"ignored", L"C:\\a b\\c.txt") == L"\"f.exe\" --select \"C:\\a b\\c.txt\"");

    CHECK(IsNoiseShortcut(L"Uninstall Foo"));
    CHECK(IsNoiseShortcut(L"Foo Readme"));
    CHECK(IsNoiseShortcut(L"Foo Help"));

    CHECK(RealProgram(L"C:\\Office\\root\\Client\\AppVLP.exe", L"\"C:\\Office\\DCF\\TOOL.EXE\" /x") == L"C:\\Office\\DCF\\TOOL.EXE");
    CHECK(RealProgram(L"C:\\a\\app.exe", L"\"C:\\b\\other.exe\"") == L"C:\\a\\app.exe");
    CHECK(RealProgram(L"C:\\Office\\AppVLP.exe", L"no quotes") == L"C:\\Office\\AppVLP.exe");
    CHECK(RealProgram(L"C:\\Office\\AppVLP.exe", L"\"C:\\x\\readme.txt\"") == L"C:\\Office\\AppVLP.exe");

    // Stale shortcuts: a classic app whose program is gone is dropped; MSIX, unknown and unexpanded targets never are.
    {
        auto none = [](const std::wstring&) { return false; };
        auto all = [](const std::wstring&) { return true; };
        AppEntry gone;
        gone.name = L"Gone";
        gone.exePath = L"C:\\x\\gone.exe";
        CHECK(IsStaleEntry(gone, none) && !IsStaleEntry(gone, all));
        AppEntry msix = gone;  // a packaged app has an AUMID and no exe path of its own
        msix.aumid = L"Pkg!App";
        msix.exePath.clear();
        CHECK(!IsStaleEntry(msix, none));
        AppEntry classic = gone;  // the shell gives classic shortcuts an AUMID too: it does not make them exempt
        classic.aumid = L"zoom.us.Zoom Video Meetings";
        CHECK(IsStaleEntry(classic, none));
        AppEntry unknown;
        unknown.name = L"Unknown";
        CHECK(!IsStaleEntry(unknown, none));
        AppEntry env = gone;
        env.exePath = L"%ProgramFiles%\\x\\a.exe";
        CHECK(!IsStaleEntry(env, none));
    }
    CHECK(!IsNoiseShortcut(L"Notepad"));
    CHECK(!IsNoiseShortcut(L"Helper Tool"));

    // Merge: first source wins, exePath filled from the duplicate, sorted by name.
    auto merged = MergeEntries({{E(L"Zed"), E(L"alpha")}, {E(L"ALPHA", L"C:\\a.exe"), E(L"Mid")}});
    CHECK(merged.size() == 3);
    CHECK(merged[0].name == L"alpha" && merged[0].exePath == L"C:\\a.exe");
    CHECK(merged[1].name == L"Mid" && merged[2].name == L"Zed");

    // Filter order.
    std::vector<AppEntry> list = {E(L"Notepad"), E(L"Windows Notes"), E(L"Calculator")};
    auto idx = Filter(list, L"note");
    CHECK(idx.size() == 2 && idx[0] == 0 && idx[1] == 1);
    CHECK(Filter(list, L"").size() == 3);
    CHECK(Filter(list, L"qqq").empty());

    // JSON cache round trip (including non-ASCII), and rejection of junk.
    list[0].name = L"Caf\u00E9 \u4E2D";
    list[0].aumid = L"Pkg_123!App";
    auto back = EntriesFromJson(EntriesToJson(list));
    CHECK(back == list);
    CHECK(EntriesFromJson("not json").empty());
    CHECK(EntriesFromJson("{\"version\":2,\"apps\":[]}").empty());

    // Layout.
    GridLayout g = ComputeGridLayout(1920);
    CHECK(g.columns >= 3 && g.columns <= 9);
    CHECK(g.left >= 0 && g.left + g.columns * g.tileW <= 1920.0f + 0.01f);
    CHECK(ComputeGridLayout(200).columns == 3);
    // The 843 pt panel shows 7 columns of the measured 115 pt pitch; tiles are 102 pt tall.
    GridLayout gp = ComputeGridLayout(843);
    CHECK(gp.columns == 7 && gp.tileW == 115.0f && gp.tileH == 102.0f);
    CHECK(gp.left >= 0 && gp.left + gp.columns * gp.tileW <= 843.0f + 0.01f);
    // Panel: 843 pt wide, centred, clear of the top and of the Dock; narrower on a small screen.
    PanelGeometry pg = ComputePanel(1920, 1080);
    CHECK(pg.width == 843.0f && std::fabs(pg.left - (1920 - 843) / 2.0f) < 0.01f);
    CHECK(pg.top >= 32 && pg.top + pg.height <= 1080 - 100);
    PanelGeometry narrow = ComputePanel(600, 500);
    CHECK(narrow.width <= 600 - 64 + 0.01f && narrow.left >= 0 && narrow.height >= 240);
    CHECK(ScrollToRow(0, 0, 100, 300) == 0);
    CHECK(ScrollToRow(0, 5, 100, 300) == 300);   // row 5 spans 500..600: scroll so its bottom is visible
    CHECK(ScrollToRow(500, 2, 100, 300) == 200);  // row above the viewport
    CHECK(ScrollToRow(100, 2, 100, 300) == 100);  // already visible

    // Real scan: read-only; on a developer machine there must be apps, on CI just no crash.
    auto apps = ScanInstalledApps();
    std::printf("apps_test: real scan found %zu apps\n", apps.size());
    wchar_t ci[8] = {};
    if (!GetEnvironmentVariableW(L"CI", ci, 8)) CHECK(apps.size() > 5);
    for (const auto& a : apps) {
        CHECK(!a.name.empty() && !a.launch.empty());
        if (g_failed) break;
    }

    if (!g_failed) std::printf("apps_test: all passed\n");
    return g_failed ? 1 : 0;
}
