// Control Center (CLAUDE.md sec. 5 control G1): a glass popover under the menu bar with Wi-Fi (on/off, saved networks), Bluetooth,
// dark mode, Do Not Disturb, display brightness, sound volume with the current output device, and the battery. Drawn with
// Direct2D like the other Velora panels. System reads and writes go through `Backend`, which tests replace; the defaults use
// control/controls.h and run on worker threads so a slow radio or WMI call never freezes the panel. Owner: C.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <wrl/client.h>

#include "control/controls.h"
#include "render/render_host.h"

namespace velora::control {

struct Snapshot {
    AudioState audio;
    WifiState wifi;
    RadioState wifiRadio, bluetooth;
    BrightnessState brightness;
    bool darkKnown = false, dark = false;
    BatteryState battery;
    bool dnd = false;
};

struct Backend {
    std::function<Snapshot()> read;  // worker thread
    std::function<void(float)> setVolume;
    std::function<void(bool)> setMuted;
    std::function<void(int)> setBrightness;                // worker thread
    std::function<void(RadioKind, bool)> setRadio;         // worker thread
    std::function<void(const std::wstring&)> connect;      // worker thread
    std::function<void(bool)> setDark;
    std::function<void(bool)> setDnd;
    std::function<void()> openSoundSettings;
    std::function<void()> openWifiSettings;
    static Backend System();
};

class ControlCenter {
public:
    explicit ControlCenter(HINSTANCE instance, Backend backend = Backend::System());
    ~ControlCenter();
    ControlCenter(const ControlCenter&) = delete;
    ControlCenter& operator=(const ControlCenter&) = delete;

    void Toggle() { Visible() ? Hide() : Show(); }
    void Show();
    void Hide();
    bool Visible() const { return hwnd_ != nullptr; }
    HWND Hwnd() const { return hwnd_; }
    void SetOnHidden(std::function<void()> fn) { onHidden_ = std::move(fn); }

    // Everything the panel offers, for the automated tests (window DIPs).
    enum class Kind { Wifi, WifiExpand, WifiNetwork, Bluetooth, Dark, Dnd, Brightness, Volume, Mute, SoundSettings };
    struct Hit {
        Kind kind;
        int index = -1;  // network index
        float left = 0, top = 0, right = 0, bottom = 0;
    };
    std::vector<Hit> Hits() const { return hits_; }
    void Click(const Hit& hit, float fraction = 0.5f);  // fraction: where on a slider
    const Snapshot& State() const { return snap_; }
    void WaitIdle();  // test: pump until the worker answered
    int PendingWork() const { return pending_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void Refresh();
    void Layout();
    void Draw();
    void Resize();
    void Async(std::function<void()> work, bool refreshAfter);
    float Scale() const;
    int HitAt(float x, float y) const;
    void SliderTo(const Hit& hit, float fraction, bool commit);

    HINSTANCE instance_;
    Backend backend_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    render::RenderHost host_;
    Snapshot snap_;
    bool haveSnapshot_ = false, wifiOpen_ = false;
    std::vector<Hit> hits_;
    float height_ = 100;
    int hover_ = -1, drag_ = -1;
    std::uint64_t lastBrightnessSend_ = 0;
    int pending_ = 0;
    std::uint32_t generation_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> wall_;
    std::function<void()> onHidden_;
};

}  // namespace velora::control
