// dock: matching of windows to apps, grouping, click decisions, geometry, pinned-list persistence.
// No window is created and no app is launched.
#include <cstdio>

#include "dock/dock.h"
#include "dock/dock_model.h"

using namespace velora::dock;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                  \
        }                                                                \
    } while (0)

DockApp ExeApp(const wchar_t* name, const wchar_t* exe) {
    DockApp a;
    a.name = name;
    a.exePath = exe;
    a.launch = exe;
    a.icon = exe;
    return a;
}

DockWindow Win(std::uint64_t h, const wchar_t* exe, const wchar_t* aumid = L"", bool fg = false, bool minimized = false, bool ctl = true) {
    DockWindow w;
    w.hwnd = h;
    w.exePath = exe;
    w.aumid = aumid;
    w.foreground = fg;
    w.minimized = minimized;
    w.controllable = ctl;
    w.title = L"t";
    return w;
}

DockApp Describe(const DockWindow& w) {
    DockApp a;
    a.name = w.exePath.empty() ? w.aumid : w.exePath;
    a.exePath = w.exePath;
    a.aumid = w.aumid;
    return a;
}
}  // namespace

int main() {
    // Matching: AUMID wins when both sides have one; exe path is case-insensitive.
    DockApp notepad = ExeApp(L"Notepad", L"C:\\Windows\\Notepad.exe");
    CHECK(Matches(notepad, Win(1, L"c:\\windows\\notepad.exe")));
    CHECK(!Matches(notepad, Win(1, L"C:\\other.exe")));
    DockApp uwp;
    uwp.name = L"Calc";
    uwp.aumid = L"Microsoft.Calc!App";
    CHECK(Matches(uwp, Win(2, L"C:\\Windows\\System32\\ApplicationFrameHost.exe", L"microsoft.calc!app")));
    CHECK(!Matches(uwp, Win(2, L"C:\\Windows\\System32\\ApplicationFrameHost.exe", L"Other!App")));
    DockApp internal;
    internal.internalApps = true;
    internal.name = L"Apps";
    CHECK(!Matches(internal, Win(3, L"")));

    // Building: pinned first, windows grouped under their pinned app, unpinned running apps appended and grouped.
    std::vector<DockApp> pinned = {internal, notepad};
    std::vector<DockWindow> windows = {Win(10, L"C:\\Windows\\notepad.exe"), Win(11, L"C:\\x\\a.exe"), Win(12, L"C:\\x\\a.exe"),
                                       Win(13, L"C:\\x\\b.exe")};
    auto items = BuildItems(pinned, windows, Describe);
    CHECK(items.size() == 4);
    CHECK(items[0].app.internalApps && !items[0].Running());
    CHECK(items[1].pinned && items[1].windows.size() == 1);
    CHECK(!items[2].pinned && items[2].windows.size() == 2);
    CHECK(!items[3].pinned && items[3].windows.size() == 1);
    // A UWP window (frame host process) is described by its AUMID, not by the host exe.
    auto uwpItems = BuildItems({}, {Win(20, L"C:\\Windows\\System32\\ApplicationFrameHost.exe", L"Pkg!App"), Win(21, L"C:\\Windows\\System32\\ApplicationFrameHost.exe", L"Pkg!App")}, Describe);
    CHECK(uwpItems.size() == 1 && uwpItems[0].windows.size() == 2 && uwpItems[0].app.exePath.empty() && uwpItems[0].app.aumid == L"Pkg!App");
    CHECK(BuildItems({}, {}, Describe).empty());

    // Click decisions.
    CHECK(DecideClick(items[0]).kind == ClickKind::Launch);  // Apps
    DockItem idle;
    idle.app = notepad;
    CHECK(DecideClick(idle).kind == ClickKind::Launch);
    DockItem background;
    background.windows = {Win(30, L"x", L"", false, true), Win(31, L"x", L"", false, false)};
    auto a = DecideClick(background);
    CHECK(a.kind == ClickKind::Activate && a.hwnd == 31);  // first visible window
    DockItem allMin;
    allMin.windows = {Win(32, L"x", L"", false, true)};
    a = DecideClick(allMin);
    CHECK(a.kind == ClickKind::Activate && a.hwnd == 32);  // restore
    DockItem active;
    active.windows = {Win(33, L"x", L"", true)};
    a = DecideClick(active);
    CHECK(a.kind == ClickKind::Minimize && a.hwnd == 33);
    DockItem activeElevated;
    activeElevated.windows = {Win(34, L"x", L"", true, false, false)};
    CHECK(DecideClick(activeElevated).kind == ClickKind::None);  // cannot control it
    CHECK(activeElevated.AnyUncontrollable());
    DockItem several;
    several.windows = {Win(35, L"x"), Win(36, L"x", L"", true), Win(37, L"x")};
    a = DecideClick(several);
    CHECK(a.kind == ClickKind::Activate && a.hwnd == 37);  // next after the foreground one
    several.windows = {Win(35, L"x", L"", true), Win(36, L"x")};
    a = DecideClick(several);
    CHECK(a.kind == ClickKind::Activate && a.hwnd == 36);
    several.windows = {Win(35, L"x"), Win(36, L"x", L"", true)};
    CHECK(DecideClick(several).hwnd == 35);  // wraps

    // Geometry.
    DockGeometry g0 = ComputeGeometry(0, 0);
    CHECK(g0.itemX.empty() && g0.pillWidth > 0);
    DockGeometry g = ComputeGeometry(5, 3);
    CHECK(g.itemX.size() == 5 && g.separatorX > g.itemX[2] && g.separatorX < g.itemX[3]);
    CHECK(g.itemX[1] > g.itemX[0] && g.itemX[3] - g.itemX[2] > g.itemX[2] - g.itemX[1]);  // wider gap at the separator
    CHECK(g.pillWidth > g.itemX[4]);
    DockGeometry gNoSep = ComputeGeometry(3, 3);
    CHECK(gNoSep.separatorX < 0);
    CHECK(ComputeGeometry(1, 5).itemX.size() == 1);  // pinned count above item count is clamped

    // Pinned list round trip through the settings document, other sections untouched by the parser.
    std::vector<DockApp> list = {internal, ExeApp(L"Caf\u00E9", L"C:\\p\\caf\u00E9.exe"), uwp};
    list[2].launch = L"shell:AppsFolder\\Microsoft.Calc!App";
    std::string doc = "{\"other\":{\"x\":1},\"dock\":" + PinnedToJson(list) + "}";
    std::vector<DockApp> back;
    CHECK(PinnedFromJson(doc, &back));
    CHECK(back == list);
    CHECK(!PinnedFromJson("{}", &back));
    CHECK(!PinnedFromJson("garbage", &back));
    std::vector<DockApp> junk;
    CHECK(PinnedFromJson("{\"dock\":{\"pinned\":[{\"name\":\"\"},{\"x\":1},5]}}", &junk) && junk.empty());

    auto defaults = DefaultPinned();
    CHECK(!defaults.empty() && defaults[0].internalApps);

    if (!g_failed) std::printf("dock_test: all passed\n");
    return g_failed ? 1 : 0;
}
