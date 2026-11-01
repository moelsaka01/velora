// menubar: pure helpers. Power actions and the popup menu are exercised manually
// (docs/QA.md); no test ever calls Sleep / Restart / Shut Down.
#include <cstdio>

#include "menubar/menubar.h"

using namespace velora::menubar;

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };
    check(BatteryText(87, 0, 0) == L"87%", "battery on battery power");
    check(BatteryText(87, 1, 0) == L"87% \u26A1", "battery on AC");
    check(BatteryText(50, 0, 8) == L"50% \u26A1", "battery charging flag");
    check(BatteryText(255, 1, 128).empty(), "no battery hides the item");
    check(BatteryText(80, 1, 128).empty(), "no-battery flag hides the item");
    check(AppDisplayName(L"C:\\Windows\\notepad.exe", L"Notepad", L"x.txt - Notepad") == L"Notepad", "uses FileDescription");
    check(AppDisplayName(L"C:\\a\\tool.exe", L"", L"title") == L"tool", "falls back to the exe stem");
    check(AppDisplayName(L"C:\\Windows\\System32\\ApplicationFrameHost.exe", L"Application Frame Host", L"Calculator") == L"Calculator",
          "UWP host uses the window title");
    check(AppDisplayName(L"", L"", L"") == L"Desktop", "nothing known => Desktop");
    if (!failed) std::printf("menubar_test: all passed\n");
    return failed ? 1 : 0;
}
