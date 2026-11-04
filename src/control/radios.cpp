// Wi-Fi and Bluetooth on/off through Windows.Devices.Radios (C++/WinRT). Blocking WinRT calls: run on a worker thread
// (the functions join an MTA themselves and must not be called from a UI thread).
#include "control/controls.h"

#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>


namespace velora::control {

namespace {

namespace radios = winrt::Windows::Devices::Radios;

struct Apartment {
    bool ok = false;
    Apartment() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            ok = true;
        } catch (const winrt::hresult_error&) {
            ok = false;  // already initialised as STA on this thread: the call below would block, so give up
        }
    }
};

radios::RadioKind Wanted(RadioKind k) { return k == RadioKind::Wifi ? radios::RadioKind::WiFi : radios::RadioKind::Bluetooth; }

}  // namespace

RadioState ReadRadio(RadioKind kind) {
    RadioState s;
    Apartment apt;
    if (!apt.ok) return s;
    try {
        auto all = radios::Radio::GetRadiosAsync().get();
        for (auto const& r : all) {
            if (r.Kind() != Wanted(kind)) continue;
            s.present = true;
            if (r.State() == radios::RadioState::On) s.on = true;
        }
    } catch (const winrt::hresult_error&) {
        s = {};
    }
    return s;
}

bool SetRadio(RadioKind kind, bool on) {
    Apartment apt;
    if (!apt.ok) return false;
    try {
        if (radios::Radio::RequestAccessAsync().get() != radios::RadioAccessStatus::Allowed) return false;
        bool any = false, all = true;
        for (auto const& r : radios::Radio::GetRadiosAsync().get()) {
            if (r.Kind() != Wanted(kind)) continue;
            any = true;
            const auto result = r.SetStateAsync(on ? radios::RadioState::On : radios::RadioState::Off).get();
            all = all && result == radios::RadioAccessStatus::Allowed;
        }
        return any && all;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

}  // namespace velora::control
