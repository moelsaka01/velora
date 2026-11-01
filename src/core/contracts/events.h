// Internal event bus contract. Owner: A. Events are posted on the UI thread.
#pragma once

#include <cstdint>

namespace velora {

enum class EventType : std::uint16_t {
    // switch
    Entered, Exiting,
    // windows
    WindowCreated, WindowDestroyed, WindowActivated, WindowMinimized,
    WindowRestored, WindowTitleChanged, WindowCloaked, WindowUncloaked,
    // environment
    MonitorsChanged, DpiChanged, ThemeChanged, ExplorerRestarted,
    // modes
    FullscreenEntered, FullscreenLeft, GamingModeChanged,
    // desktops
    DesktopChanged,
    // settings / notifications
    SettingsChanged, NotificationPosted,
    Count_  // keep last
};

// HWNDs are carried as integers so contracts stay free of <windows.h>.
struct WindowEvent {
    std::uint64_t hwnd = 0;
    std::uint32_t pid = 0;
    bool elevated = false;  // cannot be controlled; show a badge
};

struct EventHeader {
    EventType type{};
    std::uint64_t timestampMs = 0;  // steady clock
};

}  // namespace velora
