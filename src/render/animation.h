// Time-based animation engine: cubic-bezier tweens and spring motion driven by
// the design tokens (render/tokens.h). Owner: A (render/).
//
// Pure C++ (no <windows.h>): the caller owns the clock and the frame timer.
//   engine.Start(...)            register an animation
//   while (engine.Tick(nowMs))   call once per frame while anything is active
// With SetEnabled(false) (gaming mode / reduced motion) every animation jumps to
// its end value immediately.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "render/tokens.h"

namespace velora::render {

// y for a given x on the CSS-style cubic bezier (0,0)-(x1,y1)-(x2,y2)-(1,1).
inline float EvalBezier(const tokens::timing::Bezier& b, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    auto coord = [](float p1, float p2, float t) {
        float u = 1.0f - t;
        return 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t;
    };
    auto slope = [](float p1, float p2, float t) {
        float u = 1.0f - t;
        return 3.0f * u * u * p1 + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (1.0f - p2);
    };
    // Solve x(t) = x: Newton, falling back to bisection when the slope is flat.
    float t = x;
    for (int i = 0; i < 8; ++i) {
        float err = coord(b.x1, b.x2, t) - x;
        if (std::fabs(err) < 1e-6f) return coord(b.y1, b.y2, t);
        float d = slope(b.x1, b.x2, t);
        if (std::fabs(d) < 1e-6f) break;
        t -= err / d;
    }
    float lo = 0.0f, hi = 1.0f;
    t = x;
    for (int i = 0; i < 40; ++i) {
        float v = coord(b.x1, b.x2, t);
        if (std::fabs(v - x) < 1e-6f) break;
        (v < x ? lo : hi) = t;
        t = 0.5f * (lo + hi);
    }
    return coord(b.y1, b.y2, t);
}

class AnimationEngine {
public:
    using Id = std::uint32_t;
    using Callback = std::function<void(float value, bool finished)>;

    struct Tween {
        float from = 0, to = 1;
        std::uint32_t durationMs = tokens::timing::kNormal;
        std::uint32_t delayMs = 0;
        tokens::timing::Bezier curve = tokens::timing::kStandard;
    };

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool Enabled() const { return enabled_; }

    Id Start(const Tween& t, Callback cb, std::uint64_t nowMs) {
        Id id = ++nextId_;
        if (!enabled_ || t.durationMs == 0) {
            if (cb) cb(t.to, true);
            return id;
        }
        Anim a;
        a.id = id;
        a.kind = Kind::Tween;
        a.tween = t;
        a.startMs = nowMs + t.delayMs;
        a.value = t.from;
        a.cb = std::move(cb);
        pending_.push_back(std::move(a));
        return id;
    }

    // Spring toward `to`; retarget with Retarget() for interruptible motion (Dock magnification).
    Id StartSpring(float from, float to, tokens::timing::Spring spring, Callback cb, std::uint64_t nowMs) {
        Id id = ++nextId_;
        if (!enabled_) {
            if (cb) cb(to, true);
            return id;
        }
        Anim a;
        a.id = id;
        a.kind = Kind::Spring;
        a.spring = spring;
        a.value = from;
        a.target = to;
        a.lastMs = nowMs;
        a.cb = std::move(cb);
        pending_.push_back(std::move(a));
        return id;
    }

    bool Retarget(Id id, float newTarget) {
        for (auto* list : {&active_, &pending_})
            for (auto& a : *list)
                if (a.id == id && a.kind == Kind::Spring && !a.done) {
                    a.target = newTarget;
                    return true;
                }
        return false;
    }

    void Cancel(Id id) {
        for (auto* list : {&active_, &pending_})
            for (auto& a : *list)
                if (a.id == id) a.done = true;
    }

    // Advances all animations to nowMs. Returns true while anything is still running,
    // i.e. while the caller should keep scheduling frames.
    bool Tick(std::uint64_t nowMs) {
        for (auto& a : pending_) active_.push_back(std::move(a));
        pending_.clear();

        // Callbacks may Start()/Cancel(): iterate by index over what existed at entry.
        const size_t n = active_.size();
        for (size_t i = 0; i < n; ++i) {
            Anim& a = active_[i];
            if (a.done) continue;
            Step(a, nowMs);
        }
        std::erase_if(active_, [](const Anim& a) { return a.done; });
        return !active_.empty() || !pending_.empty();
    }

    size_t ActiveCount() const { return active_.size() + pending_.size(); }

private:
    enum class Kind { Tween, Spring };
    struct Anim {
        Id id = 0;
        Kind kind = Kind::Tween;
        bool done = false;
        Tween tween;
        tokens::timing::Spring spring{};
        std::uint64_t startMs = 0, lastMs = 0;
        float value = 0, velocity = 0, target = 0;
        Callback cb;
    };

    static void Step(Anim& a, std::uint64_t nowMs) {
        if (a.kind == Kind::Tween) {
            if (nowMs < a.startMs) return;  // still in the delay
            float p = std::min(1.0f, static_cast<float>(nowMs - a.startMs) / static_cast<float>(a.tween.durationMs));
            bool finished = p >= 1.0f;
            a.value = finished ? a.tween.to : a.tween.from + (a.tween.to - a.tween.from) * EvalBezier(a.tween.curve, p);
            a.done = finished;
            if (a.cb) a.cb(a.value, finished);
            return;
        }
        // Spring: fixed 1/240 s substeps (semi-implicit Euler), capped so a long stall cannot blow up.
        constexpr float kStep = 1.0f / 240.0f;
        float remaining = std::min(0.25f, static_cast<float>(nowMs - a.lastMs) / 1000.0f);
        a.lastMs = nowMs;
        while (remaining > 0.0f) {
            float dt = std::min(kStep, remaining);
            float force = -a.spring.stiffness * (a.value - a.target) - a.spring.damping * a.velocity;
            a.velocity += (force / a.spring.mass) * dt;
            a.value += a.velocity * dt;
            remaining -= dt;
        }
        bool settled = std::fabs(a.value - a.target) < 0.01f && std::fabs(a.velocity) < 0.05f;
        if (settled) {
            a.value = a.target;
            a.done = true;
        }
        if (a.cb) a.cb(a.value, settled);
    }

    std::vector<Anim> active_, pending_;
    Id nextId_ = 0;
    bool enabled_ = true;
};

}  // namespace velora::render
