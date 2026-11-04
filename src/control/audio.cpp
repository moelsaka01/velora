#include "control/controls.h"

#include <endpointvolume.h>
#include <propsys.h>
#include <propkey.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>


namespace velora::control {

using Microsoft::WRL::ComPtr;

namespace {

// PKEY_Device_FriendlyName (functiondiscoverykeys_devpkey.h does not compile with the project's windows.h macros).
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

struct Com {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool usable = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    ~Com() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

bool DefaultRender(ComPtr<IMMDevice>* device) {
    ComPtr<IMMDeviceEnumerator> e;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)))) return false;
    return SUCCEEDED(e->GetDefaultAudioEndpoint(eRender, eMultimedia, &*device)) && *device;
}

bool Endpoint(ComPtr<IAudioEndpointVolume>* ep, std::wstring* name) {
    ComPtr<IMMDevice> dev;
    if (!DefaultRender(&dev)) return false;
    if (FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ep->GetAddressOf())))) return false;
    if (name) {
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) *name = v.pwszVal;
            PropVariantClear(&v);
        }
    }
    return true;
}

}  // namespace

AudioState ReadAudio() {
    AudioState s;
    Com com;
    if (!com.usable) return s;
    ComPtr<IAudioEndpointVolume> ep;
    if (!Endpoint(&ep, &s.device)) return s;
    float v = 0;
    BOOL m = FALSE;
    if (FAILED(ep->GetMasterVolumeLevelScalar(&v))) return s;
    ep->GetMute(&m);
    s.available = true;
    s.volume = std::clamp(v, 0.0f, 1.0f);
    s.muted = m != FALSE;
    return s;
}

bool SetVolume(float volume) {
    Com com;
    ComPtr<IAudioEndpointVolume> ep;
    if (!com.usable || !Endpoint(&ep, nullptr)) return false;
    return SUCCEEDED(ep->SetMasterVolumeLevelScalar(std::clamp(volume, 0.0f, 1.0f), nullptr));
}

bool SetMuted(bool muted) {
    Com com;
    ComPtr<IAudioEndpointVolume> ep;
    if (!com.usable || !Endpoint(&ep, nullptr)) return false;
    return SUCCEEDED(ep->SetMute(muted ? TRUE : FALSE, nullptr));
}

}  // namespace velora::control
