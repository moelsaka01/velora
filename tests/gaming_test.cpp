// Gaming mode decisions (pure).
#include <cstdio>

#include "gaming/gaming_mode.h"

using namespace velora::gaming;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

int main() {
    CHECK(ParseMode("on") == Mode::On && ParseMode("off") == Mode::Off && ParseMode("auto") == Mode::Auto);
    CHECK(ParseMode("") == Mode::Auto && ParseMode("ON") == Mode::Auto && ParseMode("garbage") == Mode::Auto);

    // Auto: on at once with a fullscreen window, off only after the delay, and a quick return keeps it on.
    {
        Controller c(1500);
        CHECK(!c.Active());
        CHECK(!c.Update(Mode::Auto, false, 1000));
        CHECK(c.Update(Mode::Auto, true, 2000) && c.Active());
        CHECK(!c.Update(Mode::Auto, true, 2500));
        CHECK(!c.Update(Mode::Auto, false, 3000) && c.Active());   // left fullscreen: still on
        CHECK(!c.Update(Mode::Auto, false, 4400) && c.Active());   // 1.4 s
        CHECK(!c.Update(Mode::Auto, true, 4450) && c.Active());    // came back (Alt+Tab flicker): stays on, timer reset
        CHECK(!c.Update(Mode::Auto, false, 5000) && c.Active());
        CHECK(c.Update(Mode::Auto, false, 6500) && !c.Active());   // 1.5 s without fullscreen
        CHECK(!c.Update(Mode::Auto, false, 7000));
    }
    // Manual modes ignore fullscreen and apply at once.
    {
        Controller c;
        CHECK(c.Update(Mode::On, false, 100) && c.Active());
        CHECK(!c.Update(Mode::On, true, 200));
        CHECK(c.Update(Mode::Off, true, 300) && !c.Active());   // Off wins over a fullscreen window
        CHECK(!c.Update(Mode::Off, true, 400));
        CHECK(c.Update(Mode::Auto, true, 500) && c.Active());
        CHECK(c.Update(Mode::Off, false, 600) && !c.Active());  // switching to Off ends it without the delay
    }
    // Leaving "on" for auto with no fullscreen window waits out the delay, then turns off.
    {
        Controller c(1000);
        c.Update(Mode::On, false, 100);
        CHECK(!c.Update(Mode::Auto, false, 200) && c.Active());
        CHECK(c.Update(Mode::Auto, false, 1300) && !c.Active());
    }
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
