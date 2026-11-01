// render/animation.h: easing math, tweens, springs, enable switch, re-entrancy.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "render/animation.h"

using namespace velora;
using namespace velora::render;

static int g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                 \
        }                                                               \
    } while (0)

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static void Easing() {
    for (auto c : {tokens::timing::kStandard, tokens::timing::kDecelerate, tokens::timing::kAccelerate}) {
        CHECK(EvalBezier(c, 0.0f) == 0.0f && EvalBezier(c, 1.0f) == 1.0f);
        float prev = 0.0f;
        for (int i = 1; i <= 100; ++i) {  // monotonic for these control points
            float y = EvalBezier(c, static_cast<float>(i) / 100.0f);
            CHECK(y >= prev - 1e-5f);
            prev = y;
        }
    }
    for (int i = 0; i <= 10; ++i) {
        float x = static_cast<float>(i) / 10.0f;
        CHECK(Near(EvalBezier(tokens::timing::kLinear, x), x, 1e-4f));
    }
    CHECK(EvalBezier(tokens::timing::kDecelerate, 0.5f) > 0.5f);  // fast start
    CHECK(EvalBezier(tokens::timing::kAccelerate, 0.5f) < 0.5f);  // slow start
}

static void Tweens() {
    AnimationEngine e;
    std::vector<float> values;
    int finishedCalls = 0;
    AnimationEngine::Tween t;
    t.from = 10;
    t.to = 20;
    t.durationMs = 100;
    t.delayMs = 50;
    t.curve = tokens::timing::kLinear;
    e.Start(t, [&](float v, bool fin) { values.push_back(v); finishedCalls += fin; }, 1000);

    CHECK(e.Tick(1000));  // still in the delay: no callback yet
    CHECK(values.empty());
    CHECK(e.Tick(1100));  // 50 ms into the tween
    CHECK(values.size() == 1 && Near(values[0], 15.0f));
    CHECK(!e.Tick(1200));  // done
    CHECK(finishedCalls == 1 && values.back() == 20.0f);
    CHECK(!e.Tick(1300) && finishedCalls == 1);  // never fires again
    CHECK(e.ActiveCount() == 0);

    // Cancel
    int calls = 0;
    auto id = e.Start({0, 1, 100, 0, tokens::timing::kLinear}, [&](float, bool) { ++calls; }, 0);
    e.Tick(10);
    e.Cancel(id);
    e.Tick(20);
    CHECK(calls == 1 && e.ActiveCount() == 0);

    // Disabled engine (gaming mode / reduced motion) jumps to the end value at once.
    e.SetEnabled(false);
    float got = -1;
    bool fin = false;
    e.Start({0, 5, 300, 0, tokens::timing::kStandard}, [&](float v, bool f) { got = v; fin = f; }, 0);
    CHECK(got == 5.0f && fin && e.ActiveCount() == 0);
    e.SetEnabled(true);

    // Re-entrancy: a finishing callback starts the next animation.
    int second = 0;
    e.Start({0, 1, 10, 0, tokens::timing::kLinear},
            [&](float, bool f) {
                if (f) e.Start({0, 1, 10, 0, tokens::timing::kLinear}, [&](float, bool) { ++second; }, 10);
            },
            0);
    CHECK(e.Tick(10));  // first finishes, second queued
    CHECK(!e.Tick(20) && second >= 1);
}

static void Springs() {
    AnimationEngine e;
    float v = 0;
    bool settled = false;
    float peak = 0;
    e.StartSpring(0, 100, tokens::timing::kDockSpring,
                  [&](float x, bool f) { v = x; settled = f; peak = std::max(peak, x); }, 0);
    std::uint64_t t = 0;
    while (e.Tick(t += 16) && t < 5000) {}
    CHECK(settled && v == 100.0f);
    CHECK(t < 2000);       // settles within 2 s at 60 fps
    CHECK(peak < 110.0f);  // dock spring is nearly critically damped: < 10% overshoot

    // Retarget mid-flight (Dock magnification follows the pointer).
    AnimationEngine e2;
    float x = 0;
    auto id = e2.StartSpring(0, 100, tokens::timing::kDockSpring, [&](float val, bool) { x = val; }, 0);
    t = 0;
    for (int i = 0; i < 6; ++i) e2.Tick(t += 16);
    CHECK(x > 0 && x < 100);
    CHECK(e2.Retarget(id, 20));
    while (e2.Tick(t += 16) && t < 5000) {}
    CHECK(x == 20.0f);

    // A long stall (laptop slept) must not blow up the integrator.
    AnimationEngine e3;
    float y = 0;
    e3.StartSpring(0, 1, tokens::timing::kDockSpring, [&](float val, bool) { y = val; }, 0);
    e3.Tick(600000);
    CHECK(std::isfinite(y) && y > -5 && y < 5);
}

int main() {
    Easing();
    Tweens();
    Springs();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("animation_test: all checks passed\n");
    return 0;
}
