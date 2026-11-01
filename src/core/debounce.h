// Burst debounce with a trailing run: at most one handling per window. Pure logic
// (caller owns the clock and the timer) so it is unit-tested. Used for the
// TaskbarCreated broadcast, which Explorer can send several times in a burst.
#pragma once

#include <cstdint>

namespace velora {

class BurstDebounce {
public:
    explicit BurstDebounce(std::uint64_t windowMs = 500) : window_(windowMs) {}

    enum class Action {
        RunNow,    // first event of a burst: handle immediately
        Schedule,  // inside the window: arm a one-shot timer for TrailingDelayMs() and call OnTimer() then
        Drop       // the trailing run is already scheduled
    };

    Action OnEvent(std::uint64_t nowMs) {
        if (!ran_ || nowMs - lastRun_ >= window_) {
            ran_ = true;
            lastRun_ = nowMs;
            return Action::RunNow;
        }
        if (trailing_) return Action::Drop;
        trailing_ = true;
        return Action::Schedule;
    }

    // Delay for the trailing timer, valid right after OnEvent() returned Schedule.
    std::uint64_t TrailingDelayMs(std::uint64_t nowMs) const {
        std::uint64_t end = lastRun_ + window_;
        return end > nowMs ? end - nowMs : 0;
    }

    // The trailing timer fired. Returns true if the caller should handle the event now.
    bool OnTimer(std::uint64_t nowMs) {
        if (!trailing_) return false;
        trailing_ = false;
        lastRun_ = nowMs;
        return true;
    }

private:
    std::uint64_t window_;
    std::uint64_t lastRun_ = 0;
    bool ran_ = false;
    bool trailing_ = false;
};

}  // namespace velora
