// Per-monitor planning with simulated monitor sets: 1, 2 and 3 monitors, mixed DPI (100/150/200 %), negative coordinates, the
// primary on the right, hot-plug, primary change, dock modes, work areas, restore planning, follow-the-pointer. Pure: nothing here
// touches a real monitor, and none of it is a substitute for real multi-monitor hardware (docs/VALIDATION.md).
#include <cstdio>

#include "monitors/monitor_layout.h"

using namespace velora::monitors;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

static MonitorDesc Mon(const wchar_t* id, int l, int t, int r, int b, unsigned dpi, bool primary) {
    MonitorDesc m;
    m.id = id;
    m.rect = {l, t, r, b};
    m.work = m.rect;
    m.dpi = dpi;
    m.primary = primary;
    return m;
}

int main() {
    const Thickness th;  // 30 dip menu bar, 90 dip Dock band
    // ---- one monitor ----
    {
        std::vector<MonitorDesc> one = {Mon(L"D1", 0, 0, 1920, 1080, 96, true)};
        for (DockMode mode : {DockMode::Primary, DockMode::Every, DockMode::Follow}) {
            Plan p = MakePlan(one, mode, L"D1");
            CHECK(p.primaryId == L"D1" && p.dockHomeId == L"D1" && p.extraMenu.empty() && p.extraDock.empty());
        }
        auto w = ExpectedWorkAreas(one, MakePlan(one, DockMode::Primary, L""), th);
        CHECK(w[0] == (Rc{0, 30, 1920, 990}));
        CHECK(MakePlan({}, DockMode::Every, L"").primaryId.empty());  // no monitors: nothing planned, no crash
    }
    // ---- two monitors, mixed DPI, the second one to the left (negative coordinates) ----
    std::vector<MonitorDesc> two = {Mon(L"D1", 0, 0, 2560, 1440, 144, true), Mon(L"D2", -1920, 120, 0, 1200, 96, false)};
    {
        Plan p = MakePlan(two, DockMode::Primary, L"");
        CHECK(p.primaryId == L"D1" && p.dockHomeId == L"D1" && p.extraMenu == std::vector<std::wstring>{L"D2"} && p.extraDock.empty());
        Plan e = MakePlan(two, DockMode::Every, L"");
        CHECK(e.extraDock == std::vector<std::wstring>{L"D2"});
        auto w = ExpectedWorkAreas(two, e, th);
        CHECK(w[0] == (Rc{0, 45, 2560, 1440 - 135}));         // 150 %: 45 px bar, 135 px Dock band
        CHECK(w[1] == (Rc{-1920, 120 + 30, 0, 1200 - 90}));   // 100 %: 30 / 90 px, negative left edge kept
        auto wp = ExpectedWorkAreas(two, p, th);
        CHECK(wp[1] == (Rc{-1920, 150, 0, 1200}));            // no Dock on the second monitor in "primary" mode
        // Every maximised window fits its monitor's work area.
        for (size_t i = 0; i < two.size(); ++i) CHECK(two[i].rect.Contains(MaximizedWithin(two[i].rect, w[i])) && two[i].rect.Contains(w[i]));
        // Follow: the Dock goes where the pointer rests; unknown ids fall back to the primary.
        CHECK(MakePlan(two, DockMode::Follow, L"D2").dockHomeId == L"D2");
        CHECK(MakePlan(two, DockMode::Follow, L"D9").dockHomeId == L"D1");
        CHECK(MakePlan(two, DockMode::Follow, L"").dockHomeId == L"D1");
        CHECK(MakePlan(two, DockMode::Follow, L"D2").extraDock.empty());  // follow = one Dock only
        auto wf = ExpectedWorkAreas(two, MakePlan(two, DockMode::Follow, L"D2"), th);
        CHECK(wf[0] == (Rc{0, 45, 2560, 1440}) && wf[1] == (Rc{-1920, 150, 0, 1110}));
    }
    // ---- three monitors, primary on the right, 200 % one above ----
    std::vector<MonitorDesc> three = {Mon(L"D3", 1920, 0, 4480, 1440, 96, true), Mon(L"D1", 0, 0, 1920, 1080, 144, false),
                                      Mon(L"D2", 0, -2160, 3840, 0, 192, false)};
    {
        Plan e = MakePlan(three, DockMode::Every, L"");
        CHECK(e.primaryId == L"D3" && e.dockHomeId == L"D3");
        CHECK(e.extraMenu == (std::vector<std::wstring>{L"D2", L"D1"}));  // by position (left, then top), independent of enumeration order
        auto w = ExpectedWorkAreas(three, e, th);
        CHECK(w[0] == (Rc{1920, 30, 4480, 1440 - 90}));
        CHECK(w[1] == (Rc{0, 45, 1920, 1080 - 135}));
        CHECK(w[2] == (Rc{0, -2160 + 60, 3840, 0 - 180}));  // 200 %: 60 / 180 px
        for (size_t i = 0; i < three.size(); ++i) CHECK(three[i].rect.Contains(w[i]) && !w[i].Empty());
        // The work areas of different monitors never overlap.
        for (size_t i = 0; i < w.size(); ++i)
            for (size_t j = i + 1; j < w.size(); ++j)
                CHECK(w[i].right <= w[j].left || w[j].right <= w[i].left || w[i].bottom <= w[j].top || w[j].bottom <= w[i].top);
        // Same set in another enumeration order gives the same plan.
        std::vector<MonitorDesc> shuffled = {three[2], three[0], three[1]};
        CHECK(MakePlan(shuffled, DockMode::Every, L"") == e);
        // No primary flag at all (never happens): the first monitor is used, and the plan still has no duplicates.
        std::vector<MonitorDesc> noPrimary = three;
        for (auto& m : noPrimary) m.primary = false;
        Plan np = MakePlan(noPrimary, DockMode::Every, L"");
        CHECK(np.primaryId == L"D3" && np.extraMenu.size() == 2);
    }
    // ---- hot-plug and primary change (Compare) ----
    {
        Plan one = MakePlan({two[0]}, DockMode::Every, L"");
        Plan both = MakePlan(two, DockMode::Every, L"");
        Diff add = Compare(one, both);
        CHECK(add.createMenu == std::vector<std::wstring>{L"D2"} && add.createDock == std::vector<std::wstring>{L"D2"});
        CHECK(add.destroyMenu.empty() && add.destroyDock.empty() && !add.primaryChanged && !add.dockHomeChanged);
        Diff remove = Compare(both, one);
        CHECK(remove.destroyMenu == std::vector<std::wstring>{L"D2"} && remove.destroyDock == std::vector<std::wstring>{L"D2"} && remove.createMenu.empty());
        CHECK(Compare(both, both).Empty());
        // The primary changes from D1 to D2: the full bar moves, D1 gets a secondary bar and D2 loses its own.
        auto swapped = two;
        swapped[0].primary = false;
        swapped[1].primary = true;
        Diff sw = Compare(both, MakePlan(swapped, DockMode::Every, L""));
        CHECK(sw.primaryChanged && sw.dockHomeChanged);
        CHECK(sw.createMenu == std::vector<std::wstring>{L"D1"} && sw.destroyMenu == std::vector<std::wstring>{L"D2"});
        CHECK(sw.createDock == std::vector<std::wstring>{L"D1"} && sw.destroyDock == std::vector<std::wstring>{L"D2"});
        // The mode changes: Every -> Primary drops the extra Docks only; Primary -> Follow keeps bars and moves the Dock home.
        Diff modes = Compare(both, MakePlan(two, DockMode::Primary, L""));
        CHECK(modes.destroyDock == std::vector<std::wstring>{L"D2"} && modes.createMenu.empty() && modes.destroyMenu.empty());
        Diff follow = Compare(MakePlan(two, DockMode::Follow, L"D1"), MakePlan(two, DockMode::Follow, L"D2"));
        CHECK(follow.dockHomeChanged && !follow.primaryChanged && follow.createDock.empty() && follow.destroyDock.empty());
        // Three monitors to two (one unplugged) and back.
        Diff drop = Compare(MakePlan(three, DockMode::Every, L""), MakePlan({three[0], three[1]}, DockMode::Every, L""));
        CHECK(drop.destroyMenu == std::vector<std::wstring>{L"D2"} && drop.destroyDock == std::vector<std::wstring>{L"D2"});
        // A resolution or DPI change keeps the same ids: no instance is created or destroyed (the appbars just reposition).
        auto resized = two;
        resized[1].rect = {-2560, 0, 0, 1440};
        resized[1].dpi = 192;
        CHECK(Compare(both, MakePlan(resized, DockMode::Every, L"")).Empty());
    }
    // ---- sizes ----
    {
        CHECK(ThicknessPx(30, 96) == 30 && ThicknessPx(30, 120) == 38 && ThicknessPx(30, 144) == 45 && ThicknessPx(30, 192) == 60);
        CHECK(ThicknessPx(30.4f, 96) == 30 && ThicknessPx(30.6f, 96) == 31 && ThicknessPx(0, 144) == 0);
        CHECK(WorkAreaWith({0, 0, 100, 50}, 40, 40) == (Rc{0, 40, 100, 40}));  // bars taller than the monitor: empty, never inverted
        CHECK(WorkAreaWith({0, 0, 100, 100}, -5, -5) == (Rc{0, 0, 100, 100}));
    }
    // ---- restoring work areas ----
    {
        std::vector<MonitorDesc> now = two;
        now[0].work = {0, 45, 2560, 1305};   // reserved by our bars
        now[1].work = {-1920, 150, 0, 1110};
        std::vector<RecordedWork> rec = {{L"D1", two[0].rect}, {L"D2", two[1].rect}, {L"D3", {0, 0, 10, 10}}};
        auto fix = PlanWorkAreaRestore(rec, now);
        CHECK(fix.size() == 2 && fix[0].id == L"D1" && fix[0].want == two[0].rect && fix[1].id == L"D2" && fix[1].want == two[1].rect);  // D3 is gone
        // Already equal: nothing to do. A recorded rectangle that no longer fits the monitor (resolution dropped): left alone.
        now[0].work = two[0].rect;
        CHECK(PlanWorkAreaRestore({{L"D1", two[0].rect}}, now).empty());
        auto shrunk = now;
        shrunk[0].rect = {0, 0, 1280, 720};
        shrunk[0].work = {0, 30, 1280, 630};
        CHECK(PlanWorkAreaRestore({{L"D1", {0, 0, 2560, 1440}}}, shrunk).empty());
        CHECK(PlanWorkAreaRestore({{L"D1", {0, 0, 0, 0}}}, now).empty());  // an empty record is never applied
        // A work area with a taskbar recorded before Velora: restored exactly.
        auto reserved = two;
        reserved[1].work = {-1920, 120, 0, 1160};
        CHECK(PlanWorkAreaRestore({{L"D2", {-1920, 120, 0, 1160}}}, {two[0], two[1]}).size() == 1);
    }
    // ---- follow the pointer ----
    {
        FollowTracker f(300);
        CHECK(f.Feed(L"D1", 1000).empty() && f.Current() == L"D1");     // first sighting: becomes current, no move
        CHECK(f.Feed(L"D2", 1100).empty());                              // brushing over another monitor
        CHECK(f.Feed(L"D1", 1200).empty());                              // back before the dwell: nothing moved
        CHECK(f.Feed(L"D2", 1300).empty());
        CHECK(f.Feed(L"D2", 1550).empty());                              // 250 ms
        CHECK(f.Feed(L"D2", 1600) == L"D2" && f.Current() == L"D2");     // 300 ms: move
        CHECK(f.Feed(L"D2", 5000).empty());
        CHECK(f.Feed(L"", 5100).empty());                                // unknown position ignored
        f.Reset(L"D1");
        CHECK(f.Current() == L"D1" && f.Feed(L"D3", 6000).empty() && f.Feed(L"D3", 6400) == L"D3");
    }
    // ---- parsing ----
    CHECK(ParseDockMode("every") == DockMode::Every && ParseDockMode("follow") == DockMode::Follow);
    CHECK(ParseDockMode("primary") == DockMode::Primary && ParseDockMode("") == DockMode::Primary && ParseDockMode("EVERY") == DockMode::Primary);

    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
