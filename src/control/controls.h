// System controls behind Control Center (CLAUDE.md sec. 5 control G1). Owner: C.
//
// Documented APIs only: Core Audio (volume, mute, current output device), the WLAN API (status, network list, connect to a saved
// profile), Windows.Devices.Radios (Wi-Fi and Bluetooth on/off), WMI WmiMonitorBrightness with DDC/CI as the fallback for external
// monitors, GetSystemPowerStatus, and the per-user "apps use light theme" setting. Every function fails soft: `available == false`
// or a `false` return, never an exception, never a prompt. The slow ones (WLAN, radios, WMI) are meant for a worker thread.
//
// Not here, on purpose: switching the default output device (only possible through the undocumented IPolicyConfig; the panel
// links to Windows' Sound settings instead).
#pragma once

#include <string>
#include <vector>

#include "core/win.h"

namespace velora::control {

// ---- audio ----------------------------------------------------------------------------------------------------------------
struct AudioState {
    bool available = false;
    float volume = 0;  // 0..1 (master scalar)
    bool muted = false;
    std::wstring device;  // friendly name of the default output device
};
AudioState ReadAudio();
bool SetVolume(float volume);  // clamped to 0..1
bool SetMuted(bool muted);

// ---- Wi-Fi ----------------------------------------------------------------------------------------------------------------
struct WifiNetwork {
    std::wstring ssid, profile;  // profile is non-empty for a saved network
    int signal = 0;              // 0..100
    bool secure = false, connected = false;
};
struct WifiState {
    bool available = false;  // a WLAN interface exists
    bool connected = false;
    std::wstring ssid;
    std::vector<WifiNetwork> networks;  // see NormalizeNetworks
};
// Duplicates (the same SSID on several access points) are merged keeping the strongest signal; hidden (empty) names are dropped;
// the connected network first, then saved ones, then by signal.
std::vector<WifiNetwork> NormalizeNetworks(std::vector<WifiNetwork> raw);
WifiState ReadWifi();
bool ConnectSaved(const std::wstring& profile);

// ---- radios ---------------------------------------------------------------------------------------------------------------
enum class RadioKind { Wifi, Bluetooth };
struct RadioState {
    bool present = false;
    bool on = false;
};
RadioState ReadRadio(RadioKind kind);
bool SetRadio(RadioKind kind, bool on);

// ---- brightness -----------------------------------------------------------------------------------------------------------
struct BrightnessState {
    bool available = false;
    int percent = 0;
    bool internalPanel = false;  // WMI (laptop panel); otherwise DDC/CI of an external monitor
};
BrightnessState ReadBrightness();
bool SetBrightness(int percent);  // 0..100

// ---- theme ----------------------------------------------------------------------------------------------------------------
// The per-user Windows light/dark preference (HKCU ...\Themes\Personalize). `subkey` exists for tests.
inline constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
bool ReadDarkMode(bool* dark, const wchar_t* subkey = kPersonalizeKey);
bool SetDarkMode(bool dark, const wchar_t* subkey = kPersonalizeKey, bool broadcast = true);

// ---- battery --------------------------------------------------------------------------------------------------------------
struct BatteryState {
    bool present = false;
    int percent = 0;
    bool charging = false, onAc = false;
};
BatteryState ReadBattery();
BatteryState BatteryFromStatus(unsigned char acLine, unsigned char flags, unsigned char percent);  // pure

}  // namespace velora::control
