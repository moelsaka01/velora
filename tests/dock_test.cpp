// dock: matching of windows to apps, grouping, click decisions, geometry, pinned-list persistence.
// No window is created and no app is launched.
#include <algorithm>
#include <cmath>
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

    // Measured spec values (docs/MACOS_UI_SPEC.md): shelf 74 = 44 icon + 2 x 15 padding, icon pitch 58 = 44 + 14.
    {
        DockGeometry one = ComputeGeometry(1, 1);
        CHECK(one.pillHeight == 74.0f && one.itemX[0] == 15.0f && one.itemSize[0] == 44.0f && one.pillWidth == 74.0f);
        DockGeometry three = ComputeGeometry(3, 3);
        CHECK(three.itemX[1] - three.itemX[0] == 58.0f && three.itemX[2] - three.itemX[1] == 58.0f && three.pillWidth == 15 + 3 * 44 + 2 * 14 + 15);
        CHECK(IconBaseline() == 59.0f && DotCentre() == 68.25f);  // dot centre 9.25 below the icon, inside the 74 shelf
        // Scaled icons grow the shelf, keep their order and their gaps.
        std::vector<float> s = {2.0f, 1.0f, 1.5f};
        DockGeometry g3 = ComputeGeometry(3, 3, &s);
        CHECK(g3.itemSize[0] == 88.0f && g3.itemX[1] == 15 + 88 + 14 && g3.itemX[2] == g3.itemX[1] + 44 + 14);
        CHECK(g3.pillWidth == g3.itemX[2] + 66 + 15);
    }

    // Magnification curve: macos-web points (1 / 0.414 / 0.1 / 0), symmetric, monotonic, zero beyond the reach.
    {
        const float R = MagnifyReach();
        CHECK(R == 6.0f * 58.0f);
        CHECK(MagnifyFalloff(0, R) == 1.0f && std::fabs(MagnifyFalloff(0.5f * R, R) - 0.414f) < 1e-4f &&
              std::fabs(MagnifyFalloff(0.8f * R, R) - 0.1f) < 1e-4f && MagnifyFalloff(R, R) == 0.0f && MagnifyFalloff(3 * R, R) == 0.0f);
        CHECK(MagnifyFalloff(-70.0f, R) == MagnifyFalloff(70.0f, R));
        float prev = 2;
        bool mono = true;
        for (float d = 0; d <= R; d += 5) {
            float f = MagnifyFalloff(d, R);
            mono = mono && f <= prev + 1e-6f;
            prev = f;
        }
        CHECK(mono);

        // The pointer is mapped back through the stretched layout: the icon under it is the one that peaks.
        const float peak = 1.7f;
        auto sc = MagnifyScales(7, 7, 0.0f, peak);  // pointer at the shelf centre = centre of icon 3
        CHECK(sc.size() == 7 && sc[3] > 1.69f && sc[3] <= peak + 1e-4f && sc[3] > sc[2] && sc[3] > sc[4]);
        CHECK(std::fabs(sc[2] - sc[4]) < 0.01f);  // symmetric around the centre icon
        // Pointer over another icon: that one peaks. Find where icon 5 lands in the layout and point at it.
        float best = 0;
        size_t bestIdx = 0;
        for (float p = -200; p <= 200; p += 4) {
            auto t = MagnifyScales(7, 7, p, peak);
            for (size_t i = 0; i < t.size(); ++i)
                if (t[i] > best) {
                    best = t[i];
                    bestIdx = i;
                }
        }
        (void)bestIdx;
        CHECK(best <= peak + 1e-4f && best > 1.69f);
        auto none = MagnifyScales(7, 7, 0.0f, 1.0f);  // peak 1 = magnification off
        CHECK(none == std::vector<float>(7, 1.0f));
        CHECK(MagnifyScales(0, 0, 0.0f, peak).empty());
        for (float p : {-1000.0f, 1000.0f}) {  // pointer far outside never gives scales outside [1, peak]
            for (float v : MagnifyScales(7, 7, p, peak)) CHECK(v >= 1.0f && v <= peak + 1e-4f);
        }
        // Sweeping the pointer left to right moves the peak icon left to right.
        size_t last = 0;
        bool order = true;
        for (float p = -180; p <= 180; p += 6) {
            auto t = MagnifyScales(7, 7, p, peak);
            size_t arg = static_cast<size_t>(std::max_element(t.begin(), t.end()) - t.begin());
            order = order && arg >= last;
            last = arg;
        }
        CHECK(order && last == 6);
        // Growth: positive, bounded by every icon at full peak, zero when off.
        float growth = MaxGrowth(8, 5, peak);
        CHECK(growth > 40.0f && growth < 8 * (peak - 1.0f) * 44.0f);
        CHECK(MaxGrowth(8, 5, 1.0f) == 0.0f && MaxGrowth(0, 0, peak) == 0.0f);
    }

    // Wallpaper crop: fill covers the monitor and centres, stretch maps exactly.
    {
        SourceRect fill = WallpaperSourceRect(2000, 1000, 1000, 1000, false, 0, 0, 100, 100);  // scale 1, 500 px cropped left
        CHECK(fill.x == 500.0f && fill.y == 0.0f && fill.w == 100.0f && fill.h == 100.0f);
        SourceRect b = WallpaperSourceRect(500, 500, 1000, 1000, false, 200, 300, 100, 100);  // scale 2
        CHECK(b.x == 100.0f && b.y == 150.0f && b.w == 50.0f && b.h == 50.0f);
        SourceRect c = WallpaperSourceRect(1000, 500, 500, 500, true, 100, 100, 100, 100);  // stretch: x 2x, y 1x
        CHECK(c.x == 200.0f && c.y == 100.0f && c.w == 200.0f && c.h == 100.0f);
        CHECK(WallpaperSourceRect(0, 10, 10, 10, false, 0, 0, 1, 1).w == 0.0f);
    }

    // Box blur: constant stays constant, an impulse spreads without changing the total, radius 0 does nothing.
    {
        const int w = 40, h = 30;
        std::vector<std::uint8_t> flat(w * h * 4, 117);
        auto copy = flat;
        BoxBlur(copy, w, h, 5);
        CHECK(copy == flat);
        std::vector<std::uint8_t> imp(w * h * 4, 0);
        for (int c = 0; c < 4; ++c) imp[(15 * w + 20) * 4 + c] = 255;
        auto same = imp;
        BoxBlur(same, w, h, 0);
        CHECK(same == imp);
        BoxBlur(imp, w, h, 2);
        long total = 0;
        int peakVal = 0, nonzero = 0;
        for (int i = 0; i < w * h; ++i) {
            total += imp[i * 4];
            peakVal = std::max<int>(peakVal, imp[i * 4]);
            nonzero += imp[i * 4] > 0;
        }
        CHECK(peakVal < 40 && nonzero > 60);       // spread out
        CHECK(std::labs(total - 255) < 40);        // energy kept (rounding in 8 bits)
        std::vector<std::uint8_t> tiny(8, 5);       // smaller than the data it claims: ignored, no crash
        BoxBlur(tiny, 100, 100, 3);
    }

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

    // Hidden pins: missing files; Apps item, shell targets and existing files stay.
    {
        DockApp files = ExeApp(L"Files", L"C:\\velora\\velora-files.exe");
        DockApp settings = ExeApp(L"Settings", L"C:\\velora\\velora-settings.exe");
        DockApp present = ExeApp(L"Present", L"C:\\p\\present.exe");
        DockApp gone = ExeApp(L"Gone", L"C:\\p\\gone.exe");
        DockApp store = uwp;
        store.launch = L"shell:AppsFolder\\Microsoft.Calc!App";
        auto exists = [](const std::wstring& p) { return p == L"C:\\p\\present.exe" || p.find(L"velora-") != std::wstring::npos; };
        auto vis = VisiblePinned({internal, files, present, gone, settings, store}, exists);
        CHECK(vis.size() == 5);  // Files and Settings are G1 now; only missing files are hidden
        CHECK(vis[0].internalApps && vis[1].name == L"Files" && vis[2].name == L"Present" && vis[3].name == L"Settings" && vis[4].name == store.name);
    }

    CHECK(IsShellHostWindow(L"C:\\Windows\\SystemApps\\ShellExperienceHost_cw5n1h2txyewy\\ShellExperienceHost.exe") && IsShellHostWindow(L"C:\\Windows\\System32\\LockApp.exe"));
    CHECK(!IsShellHostWindow(L"C:\\Windows\\explorer.exe") && !IsShellHostWindow(L"") && !IsShellHostWindow(L"C:\\x\\NotShellExperienceHost.exe"));

    auto defaults = DefaultPinned();
    CHECK(!defaults.empty() && defaults[0].internalApps);

    if (!g_failed) std::printf("dock_test: all passed\n");
    return g_failed ? 1 : 0;
}
