#include <cstdio>

#include "core/debounce.h"

using velora::BurstDebounce;
using A = BurstDebounce::Action;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };
    BurstDebounce d(500);
    check(d.OnEvent(1000) == A::RunNow, "first event runs now");
    check(d.OnEvent(1100) == A::Schedule, "second event in the window schedules a trailing run");
    check(d.TrailingDelayMs(1100) == 400, "trailing delay is the rest of the window");
    check(d.OnEvent(1200) == A::Drop, "third event is dropped");
    check(d.OnEvent(1499) == A::Drop, "still dropped");
    check(d.OnTimer(1500), "trailing timer runs the handler once");
    check(!d.OnTimer(1500), "no second trailing run");
    check(d.OnEvent(1600) == A::Schedule, "after the trailing run a new window starts");
    check(d.OnTimer(2000), "second trailing");
    check(d.OnEvent(2600) == A::RunNow, "event after a quiet window runs now");
    // a lone event never schedules anything
    BurstDebounce e(500);
    check(e.OnEvent(0) == A::RunNow, "lone event at t=0 runs now");
    check(e.OnEvent(5000) == A::RunNow, "lone event later runs now");
    if (failed == 0) std::printf("debounce_test: all passed\n");
    return failed ? 1 : 0;
}
