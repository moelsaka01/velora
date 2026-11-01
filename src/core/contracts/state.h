// Persistent switch state (%LOCALAPPDATA%\Velora\state.json). Owner: A.
// Written before the taskbar is touched; consumed by exit and by recovery.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace velora {

inline constexpr int kStateSchemaVersion = 1;
inline constexpr char kStateFileName[]   = "state.json";
inline constexpr char kDirtyMarkerName[] = "dirty.marker";

struct Rect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
    friend bool operator==(const Rect&, const Rect&) = default;
};

struct MonitorState {
    std::wstring deviceName;  // e.g. DISPLAY1 device path
    Rect workArea;            // work area before Velora registered appbars
    bool taskbarVisible = true;
    friend bool operator==(const MonitorState&, const MonitorState&) = default;
};

struct SwitchState {
    int schemaVersion = kStateSchemaVersion;
    bool taskbarAutoHide = false;     // ABS_AUTOHIDE bit before enter
    std::vector<MonitorState> monitors;
    std::wstring wallpaperPath;
    std::uint64_t foregroundHwnd = 0;  // HWND as integer; 0 = none
    std::uint32_t shellPid = 0;
    std::uint32_t watchdogPid = 0;
    // Added within the sprint (optional, backward compatible): creation time of
    // the shell process as a FILETIME tick count. Recovery uses it with shellPid
    // so a reused PID is never mistaken for the shell. 0 = unknown.
    std::uint64_t shellStartTime = 0;
};

}  // namespace velora
