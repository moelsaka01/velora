#include "control/controls.h"

namespace velora::control {

namespace {

bool ReadValue(HKEY key, const wchar_t* name, DWORD* v) {
    DWORD size = sizeof *v, type = 0;
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(v), &size) == ERROR_SUCCESS && type == REG_DWORD;
}

}  // namespace

bool ReadDarkMode(bool* dark, const wchar_t* subkey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD apps = 1;
    const bool ok = ReadValue(key, L"AppsUseLightTheme", &apps);
    RegCloseKey(key);
    if (ok) *dark = apps == 0;
    return ok;
}

bool SetDarkMode(bool dark, const wchar_t* subkey, bool broadcast) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const DWORD v = dark ? 0 : 1;  // 0 = dark
    bool ok = RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v) == ERROR_SUCCESS;
    ok = RegSetValueExW(key, L"SystemUsesLightTheme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v) == ERROR_SUCCESS && ok;
    RegCloseKey(key);
    if (ok && broadcast) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"), SMTO_ABORTIFHUNG, 200, &result);
    }
    return ok;
}

BatteryState BatteryFromStatus(unsigned char acLine, unsigned char flags, unsigned char percent) {
    BatteryState b;
    if ((flags & 128) || percent > 100) return b;  // no system battery / unknown
    b.present = true;
    b.percent = percent;
    b.onAc = acLine == 1;
    b.charging = (flags & 8) != 0;
    return b;
}

BatteryState ReadBattery() {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return {};
    return BatteryFromStatus(sps.ACLineStatus, sps.BatteryFlag, sps.BatteryLifePercent);
}

}  // namespace velora::control
