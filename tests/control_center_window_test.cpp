// Control Center driven through a fake backend (no volume, radio, brightness or theme of this machine is touched): the panel opens,
// shows the snapshot, every control calls the right backend function with the right value, the slow ones run off the UI thread, and
// Esc / losing focus close it. Opens a window: runs in CI.
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "control/control_center.h"

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

static void Pump(int ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

struct Fake {
    std::mutex mu;
    Snapshot snap;
    std::vector<std::string> calls;
    DWORD workerThread = 0, uiThread = GetCurrentThreadId();
    int reads = 0;

    void Log(const std::string& s) {
        std::lock_guard l(mu);
        calls.push_back(s);
    }
    bool Has(const std::string& s) {
        std::lock_guard l(mu);
        for (const auto& c : calls)
            if (c == s) return true;
        return false;
    }
    Backend Make() {
        Backend b;
        b.read = [this] {
            std::lock_guard l(mu);
            workerThread = GetCurrentThreadId();
            ++reads;
            return snap;
        };
        b.setVolume = [this](float v) { Log("volume " + std::to_string(static_cast<int>(v * 100 + 0.5f))); };
        b.setMuted = [this](bool m) { Log(m ? "mute" : "unmute"); };
        b.setBrightness = [this](int p) {
            Log("brightness " + std::to_string(p));
            if (GetCurrentThreadId() == uiThread) Log("BRIGHTNESS ON UI THREAD");
        };
        b.setRadio = [this](RadioKind k, bool on) {
            Log(std::string(k == RadioKind::Wifi ? "wifi " : "bt ") + (on ? "on" : "off"));
            if (GetCurrentThreadId() == uiThread) Log("RADIO ON UI THREAD");
            std::lock_guard l(mu);
            (k == RadioKind::Wifi ? snap.wifiRadio : snap.bluetooth).on = on;
        };
        b.connect = [this](const std::wstring& p) { Log("connect " + std::string(p.begin(), p.end())); };
        b.setDark = [this](bool d) { Log(d ? "dark" : "light"); };
        b.setDnd = [this](bool d) { Log(d ? "dnd on" : "dnd off"); };
        b.openSoundSettings = [this] { Log("sound settings"); };
        b.openWifiSettings = [this] { Log("wifi settings"); };
        return b;
    }
};

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Fake fake;
    fake.snap.audio = {true, 0.4f, false, L"Test Speakers"};
    fake.snap.wifiRadio = {true, true};
    fake.snap.bluetooth = {true, false};
    fake.snap.brightness = {true, 70, true};
    fake.snap.darkKnown = true;
    fake.snap.dark = false;
    fake.snap.battery = {true, 83, true, true};
    fake.snap.wifi.available = true;
    fake.snap.wifi.connected = true;
    fake.snap.wifi.ssid = L"Home";
    fake.snap.wifi.networks = {{L"Home", L"Home", 80, true, true}, {L"Cafe", L"Cafe", 60, true, false}, {L"Guest", L"", 50, false, false}};

    ControlCenter cc(GetModuleHandleW(nullptr), fake.Make());
    cc.Show();
    CHECK(cc.Visible());
    cc.WaitIdle();
    CHECK(fake.reads == 1 && fake.workerThread != 0 && fake.workerThread != GetCurrentThreadId());  // read off the UI thread
    CHECK(cc.State().audio.available && cc.State().wifi.ssid == L"Home" && cc.State().battery.percent == 83);
    RECT r{};
    GetWindowRect(cc.Hwnd(), &r);
    CHECK(r.bottom > r.top && r.right > r.left);

    using K = ControlCenter::Kind;
    auto find = [&](K kind, int index = -1) {
        for (const auto& h : cc.Hits())
            if (h.kind == kind && (index < 0 || h.index == index)) return h;
        return ControlCenter::Hit{static_cast<K>(-1)};
    };

    cc.Click(find(K::Dark));
    CHECK(fake.Has("dark") && cc.State().dark);
    cc.Click(find(K::Dark));
    CHECK(fake.Has("light") && !cc.State().dark);
    cc.Click(find(K::Dnd));
    CHECK(fake.Has("dnd on"));
    cc.Click(find(K::Volume), 0.75f);
    CHECK(fake.Has("volume 75") && cc.State().audio.volume > 0.74f);
    cc.Click(find(K::Mute));
    CHECK(fake.Has("mute") && cc.State().audio.muted);
    cc.Click(find(K::Volume), 0.2f);  // moving the slider while muted unmutes
    CHECK(fake.Has("unmute") && fake.Has("volume 20"));
    cc.Click(find(K::Brightness), 0.3f);
    cc.WaitIdle();
    CHECK(fake.Has("brightness 30") && !fake.Has("BRIGHTNESS ON UI THREAD"));

    // Radios: optimistic UI, backend on a worker, then a fresh read.
    const int readsBefore = fake.reads;
    cc.Click(find(K::Bluetooth));
    CHECK(cc.State().bluetooth.on);
    cc.WaitIdle();
    CHECK(fake.Has("bt on") && !fake.Has("RADIO ON UI THREAD") && fake.reads > readsBefore && cc.State().bluetooth.on);
    cc.Click(find(K::Wifi));
    cc.WaitIdle();
    CHECK(fake.Has("wifi off") && !cc.State().wifiRadio.on);

    // Networks: hidden until the tile is expanded, only while Wi-Fi is on.
    CHECK(find(K::WifiNetwork).kind != K::WifiNetwork);
    cc.Click(find(K::Wifi));  // on again
    cc.WaitIdle();
    cc.Click(find(K::WifiExpand));
    CHECK(find(K::WifiNetwork, 0).kind == K::WifiNetwork && find(K::WifiNetwork, 2).kind == K::WifiNetwork);
    cc.Click(find(K::WifiNetwork, 0));  // the connected one: nothing
    cc.Click(find(K::WifiNetwork, 1));  // saved: connect
    cc.WaitIdle();
    CHECK(fake.Has("connect Cafe"));
    cc.Click(find(K::WifiNetwork, 2));  // not saved: Windows' own Wi-Fi settings, the panel closes
    CHECK(fake.Has("wifi settings") && !cc.Visible());

    // Esc closes; a second Show works and reads again; the link opens Sound settings and closes.
    cc.Show();
    cc.WaitIdle();
    CHECK(cc.Visible());
    SendMessageW(cc.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK(!cc.Visible());
    cc.Show();
    cc.WaitIdle();
    cc.Click(find(K::SoundSettings));
    CHECK(fake.Has("sound settings") && !cc.Visible());

    // A snapshot that arrives after the panel was closed is dropped without touching freed state.
    cc.Show();
    cc.Hide();
    Pump(300);
    CHECK(!cc.Visible());

    // No hardware at all: the panel still opens and draws; nothing is clickable that cannot work.
    {
        Fake bare;
        ControlCenter c2(GetModuleHandleW(nullptr), bare.Make());
        c2.Show();
        c2.WaitIdle();
        CHECK(c2.Visible() && !c2.State().audio.available);
        bool hasSlider = false;
        for (const auto& h : c2.Hits()) hasSlider = hasSlider || h.kind == K::Volume || h.kind == K::Brightness;
        CHECK(!hasSlider);
        Pump(100);
        c2.Hide();
    }
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
