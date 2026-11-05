// Gaming mode (CLAUDE.md sec. 5 gaming mode): the decision logic, pure and unit-tested. Owner: A.
//
// Automatic: on while a fullscreen / borderless-fullscreen window is in front, off a moment after it is gone (so an Alt+Tab
// flicker does not flip the shell back and forth). Manual: settings.json gaming.mode = "on" | "off" | "auto".
// What "on" does lives in main.cpp (animations off, no blur, bars hidden, indexing and thumbnails paused, minimal timers) and only
// ever touches Velora's own windows and threads: the game process and its window are never touched.
#pragma once

#include <cstdint>
#include <string>

namespace velora::gaming {

enum class Mode { Auto, On, Off };

inline Mode ParseMode(const std::string& s) {
    if (s == "on") return Mode::On;
    if (s == "off") return Mode::Off;
    return Mode::Auto;  // unknown strings behave as the default
}

class Controller {
public:
    explicit Controller(std::uint64_t leaveDelayMs = 1500) : leaveDelay_(leaveDelayMs) {}

    // Returns true when the active state changed. Call on events and on a slow tick (the delay needs the clock).
    bool Update(Mode mode, bool fullscreenForeground, std::uint64_t nowMs) {
        bool want;
        switch (mode) {
            case Mode::On: want = true; break;
            case Mode::Off: want = false; break;
            default: want = fullscreenForeground; break;
        }
        const bool before = active_;
        if (mode != Mode::Auto) {
            active_ = want;
            gone_ = 0;
        } else if (want) {
            active_ = true;
            gone_ = 0;
        } else if (active_) {
            if (gone_ == 0) gone_ = nowMs ? nowMs : 1;
            if (nowMs - gone_ >= leaveDelay_) {
                active_ = false;
                gone_ = 0;
            }
        }
        return active_ != before;
    }
    bool Active() const { return active_; }

private:
    std::uint64_t leaveDelay_, gone_ = 0;
    bool active_ = false;
};

}  // namespace velora::gaming
