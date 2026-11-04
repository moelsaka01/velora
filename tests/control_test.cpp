// System controls: read-only probes of this machine (must never crash; values must be in range when reported), the pure helpers, and
// the dark-mode setting against a private registry key. Nothing here changes volume, radios, brightness or the real theme.
#include <cstdio>

#include "control/controls.h"

using namespace velora::control;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        ++g_checks;                                                  \
        if (!(c)) {                                                  \
            ++g_failed;                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                            \
    } while (0)

int main() {
    // Networks: duplicates merged on the strongest signal, hidden dropped, connected first, saved before unknown, then signal.
    {
        std::vector<WifiNetwork> raw = {
            {L"Cafe", L"", 80, false, false}, {L"Home", L"Home", 40, true, false}, {L"Home", L"", 90, true, false},
            {L"", L"", 99, false, false},     {L"Work", L"Work", 20, true, true},   {L"Guest", L"", 95, false, false},
        };
        auto n = NormalizeNetworks(raw);
        CHECK(n.size() == 4);
        CHECK(n[0].ssid == L"Work" && n[0].connected);                      // connected first although weakest
        CHECK(n[1].ssid == L"Home" && n[1].signal == 90 && n[1].profile == L"Home");  // merged, strongest signal, profile kept
        CHECK(n[2].ssid == L"Guest" && n[3].ssid == L"Cafe");               // unsaved by signal
        CHECK(NormalizeNetworks({}).empty());
    }
    // Battery: flag 128 = no battery, 255 percent = unknown, 8 = charging.
    {
        CHECK(!BatteryFromStatus(1, 128, 255).present);
        CHECK(!BatteryFromStatus(0, 0, 255).present);
        auto b = BatteryFromStatus(1, 8, 83);
        CHECK(b.present && b.percent == 83 && b.charging && b.onAc);
        b = BatteryFromStatus(0, 1, 40);
        CHECK(b.present && !b.charging && !b.onAc);
    }
    // Dark mode: the exact registry values (0 = dark) on a private key; the real Personalize key is only read.
    {
        const std::wstring sub = L"Software\VeloraTest\Personalize-" + std::to_wstring(GetCurrentProcessId());
        bool dark = true;
        CHECK(!ReadDarkMode(&dark, sub.c_str()));  // missing key
        CHECK(SetDarkMode(true, sub.c_str(), false));
        CHECK(ReadDarkMode(&dark, sub.c_str()) && dark);
        CHECK(SetDarkMode(false, sub.c_str(), false));
        CHECK(ReadDarkMode(&dark, sub.c_str()) && !dark);
        DWORD v = 99, size = sizeof v;
        CHECK(RegGetValueW(HKEY_CURRENT_USER, sub.c_str(), L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS && v == 1);
        RegDeleteTreeW(HKEY_CURRENT_USER, sub.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\VeloraTest");
        bool real = false;
        std::printf("INFO real dark mode readable: %d\n", ReadDarkMode(&real));
    }
    // Live probes (read only).
    {
        AudioState a = ReadAudio();
        CHECK(!a.available || (a.volume >= 0 && a.volume <= 1));
        std::printf("INFO audio: available=%d volume=%.2f muted=%d device=\"%ls\"\n", a.available, a.volume, a.muted, a.device.c_str());
        WifiState w = ReadWifi();
        CHECK(w.available || (!w.connected && w.networks.empty()));
        for (const auto& n : w.networks) CHECK(!n.ssid.empty() && n.signal >= 0 && n.signal <= 100);
        std::printf("INFO wifi: interface=%d connected=%d networks=%zu\n", w.available, w.connected, w.networks.size());
        RadioState wr = ReadRadio(RadioKind::Wifi), bt = ReadRadio(RadioKind::Bluetooth);
        CHECK(wr.present || !wr.on);
        CHECK(bt.present || !bt.on);
        std::printf("INFO radios: wifi present=%d on=%d, bluetooth present=%d on=%d\n", wr.present, wr.on, bt.present, bt.on);
        BrightnessState b = ReadBrightness();
        CHECK(!b.available || (b.percent >= 0 && b.percent <= 100));
        std::printf("INFO brightness: available=%d percent=%d internal=%d\n", b.available, b.percent, b.internalPanel);
        BatteryState bat = ReadBattery();
        CHECK(!bat.present || (bat.percent >= 0 && bat.percent <= 100));
        std::printf("INFO battery: present=%d percent=%d\n", bat.present, bat.percent);
    }
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
