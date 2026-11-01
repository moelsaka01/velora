// IPC contract v1. Owner: A. Changes need a DECISIONS.md entry and must stay
// backward compatible within the sprint. Wire format: ipc/v1.json.
#pragma once

#include <cstdint>

namespace velora::ipc {

inline constexpr std::uint32_t kProtocolVersion = 1;

// Named pipe: \\.\pipe\velora-<kind>-<user SID>. Per-user; local access only.
inline constexpr wchar_t kShellPipePrefix[]    = L"\\\\.\\pipe\\velora-shell-";
inline constexpr wchar_t kWatchdogPipePrefix[] = L"\\\\.\\pipe\\velora-watchdog-";

// Each message is one UTF-8 JSON object, framed by a little-endian u32 length.
inline constexpr std::uint32_t kMaxMessageBytes = 64 * 1024;

// Shell heartbeat to the watchdog (CLAUDE.md §4). The §4 figure of 3 s is the
// end-to-end budget "hang -> Windows restored"; the timeout is set below it so
// detection (timeout) + recovery (~0.1 s) stays under 3 s whatever the phase of
// the last heartbeat. See DECISIONS.md.
inline constexpr std::uint32_t kHeartbeatIntervalMs = 500;
inline constexpr std::uint32_t kHeartbeatTimeoutMs  = 2500;
static_assert(kHeartbeatTimeoutMs + 500 <= 3000, "hang recovery budget is 3 s");

enum class Kind : std::uint8_t { Request, Response, Event };

enum class Status : std::uint16_t {
    Ok = 0,
    BadRequest = 400,
    Unsupported = 501,   // unsupported state: log and degrade, never crash
    VersionMismatch = 505,
    Internal = 500,
};

// Message "type" strings; the string is the contract, not the C++ name.
namespace msg {
inline constexpr char kHello[]        = "hello";
inline constexpr char kHeartbeat[]    = "heartbeat";
inline constexpr char kGetState[]     = "get_state";
inline constexpr char kEnter[]        = "enter";
inline constexpr char kExit[]         = "exit";
// shell -> watchdog, a synchronous request sent before the shell leaves Velora
// Mode; the watchdog acknowledges and exits without restoring or terminating.
inline constexpr char kDisarm[]       = "disarm";
inline constexpr char kRestore[]      = "restore";
inline constexpr char kOpenApp[]      = "open_app";    // files/settings -> shell
inline constexpr char kSettingsChanged[] = "settings_changed";
}  // namespace msg

}  // namespace velora::ipc
