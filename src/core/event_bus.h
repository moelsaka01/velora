// Synchronous in-process event bus (UI thread only). Owner: A.
// Dispatch copies the handler list, so handlers may subscribe/unsubscribe.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "core/contracts/events.h"

namespace velora {

struct Event {
    EventHeader header;
    WindowEvent window;  // meaningful for window events only
};

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    using Token = std::size_t;

    Token Subscribe(EventType type, Handler handler) {
        Token t = ++next_;
        slots_[Index(type)].emplace_back(t, std::move(handler));
        return t;
    }

    void Unsubscribe(Token token) {
        for (auto& list : slots_)
            std::erase_if(list, [&](const auto& s) { return s.first == token; });
    }

    void Post(EventType type, const WindowEvent& w = {}) {
        Event e;
        e.header.type = type;
        e.header.timestampMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
        e.window = w;
        auto copy = slots_[Index(type)];
        for (auto& [token, fn] : copy) fn(e);
    }

private:
    static constexpr std::size_t Index(EventType t) { return static_cast<std::size_t>(t); }

    std::array<std::vector<std::pair<Token, Handler>>, static_cast<std::size_t>(EventType::Count_)> slots_;
    Token next_ = 0;
};

}  // namespace velora
