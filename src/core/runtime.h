// Process-wide runtime configuration: data directory, per-user pipe names and
// the test profile. Owner: A.
//
// Test profile (VELORA_TEST_PROFILE=<id>): lets the automated gate run against
// *fake* taskbar windows, a private data directory and private pipes, so tests
// never hide the real taskbar, touch the real state.json, or broadcast the real
// TaskbarCreated message. Without the variable nothing here changes behaviour.
#pragma once

#include <filesystem>
#include <string>

namespace velora {

struct Profile {
    std::wstring id;  // empty = production
    std::wstring trayClass = L"Shell_TrayWnd";
    std::wstring secondaryTrayClass = L"Shell_SecondaryTrayWnd";
    std::wstring taskbarCreatedMessage = L"TaskbarCreated";

    bool IsTest() const { return !id.empty(); }
};

// Read once from the environment on first use.
const Profile& CurrentProfile();

// Production: %LOCALAPPDATA%\Velora. Test profile: VELORA_DATA_DIR (required).
// Created on demand. Returns an empty path if it cannot be determined.
std::filesystem::path DataDir();
std::filesystem::path StatePath();
std::filesystem::path DirtyMarkerPath();
std::filesystem::path LogDir();

// SID string of the current user ("S-1-5-21-...").
std::wstring CurrentUserSid();

// \\.\pipe\velora-<kind>-<SID>[-<test profile id>]
std::wstring ShellPipeName();
std::wstring WatchdogPipeName();

// Directory containing the running executable.
std::filesystem::path ExeDir();

}  // namespace velora
