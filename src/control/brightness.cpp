// Brightness: the laptop panel through WMI (root\wmi, WmiMonitorBrightness / WmiMonitorBrightnessMethods), external monitors
// through DDC/CI (the documented high-level monitor API). Slow: run on a worker thread.
#include "control/controls.h"

#include <oleauto.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <vector>


namespace velora::control {

using Microsoft::WRL::ComPtr;

namespace {

struct Com {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool usable = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    ~Com() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

bool ConnectWmi(ComPtr<IWbemServices>* svc) {
    ComPtr<IWbemLocator> loc;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc)))) return false;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &*svc);
    SysFreeString(ns);
    if (FAILED(hr)) return false;
    return SUCCEEDED(CoSetProxyBlanket(svc->Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                       nullptr, EOAC_NONE));
}

bool WmiRead(int* percent) {
    ComPtr<IWbemServices> svc;
    if (!ConnectWmi(&svc)) return false;
    BSTR lang = SysAllocString(L"WQL"), q = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE");
    ComPtr<IEnumWbemClassObject> en;
    HRESULT hr = svc->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(lang);
    SysFreeString(q);
    if (FAILED(hr) || !en) return false;
    ComPtr<IWbemClassObject> obj;
    ULONG got = 0;
    if (FAILED(en->Next(5000, 1, &obj, &got)) || got == 0) return false;
    VARIANT v;
    VariantInit(&v);
    bool ok = SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr)) && (v.vt == VT_UI1 || v.vt == VT_I4 || v.vt == VT_UI4);
    if (ok) *percent = std::clamp(static_cast<int>(v.vt == VT_UI1 ? v.bVal : v.lVal), 0, 100);
    VariantClear(&v);
    return ok;
}

bool WmiWrite(int percent) {
    ComPtr<IWbemServices> svc;
    if (!ConnectWmi(&svc)) return false;
    BSTR cls = SysAllocString(L"WmiMonitorBrightnessMethods");
    ComPtr<IEnumWbemClassObject> en;
    HRESULT hr = svc->CreateInstanceEnum(cls, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    if (FAILED(hr) || !en) {
        SysFreeString(cls);
        return false;
    }
    ComPtr<IWbemClassObject> classDef, inDef, in;
    bool ok = false;
    if (SUCCEEDED(svc->GetObject(cls, 0, nullptr, &classDef, nullptr)) && SUCCEEDED(classDef->GetMethod(L"WmiSetBrightness", 0, &inDef, nullptr)) &&
        SUCCEEDED(inDef->SpawnInstance(0, &in))) {
        VARIANT t, b;
        VariantInit(&t);
        VariantInit(&b);
        t.vt = VT_I4;
        t.lVal = 1;
        b.vt = VT_UI1;
        b.bVal = static_cast<BYTE>(std::clamp(percent, 0, 100));
        in->Put(L"Timeout", 0, &t, 0);
        in->Put(L"Brightness", 0, &b, 0);
        ComPtr<IWbemClassObject> inst;
        ULONG got = 0;
        while (SUCCEEDED(en->Next(5000, 1, &inst, &got)) && got == 1) {
            VARIANT path;
            VariantInit(&path);
            if (SUCCEEDED(inst->Get(L"__PATH", 0, &path, nullptr, nullptr)) && path.vt == VT_BSTR) {
                BSTR method = SysAllocString(L"WmiSetBrightness");
                ok = SUCCEEDED(svc->ExecMethod(path.bstrVal, method, 0, nullptr, in.Get(), nullptr, nullptr)) || ok;
                SysFreeString(method);
            }
            VariantClear(&path);
            inst.Reset();
        }
    }
    SysFreeString(cls);
    return ok;
}

struct Monitors {
    std::vector<PHYSICAL_MONITOR> list;
    ~Monitors() {
        if (!list.empty()) DestroyPhysicalMonitors(static_cast<DWORD>(list.size()), list.data());
    }
};

BOOL CALLBACK CollectMonitors(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    auto* out = reinterpret_cast<Monitors*>(lp);
    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(m, &n) || n == 0) return TRUE;
    std::vector<PHYSICAL_MONITOR> v(n);
    if (GetPhysicalMonitorsFromHMONITOR(m, n, v.data())) out->list.insert(out->list.end(), v.begin(), v.end());
    return TRUE;
}

bool DdcRead(int* percent) {
    Monitors mons;
    EnumDisplayMonitors(nullptr, nullptr, &CollectMonitors, reinterpret_cast<LPARAM>(&mons));
    for (const auto& m : mons.list) {
        DWORD lo = 0, cur = 0, hi = 0;
        if (GetMonitorBrightness(m.hPhysicalMonitor, &lo, &cur, &hi) && hi > lo) {
            *percent = static_cast<int>((cur - lo) * 100 / (hi - lo));
            return true;
        }
    }
    return false;
}

bool DdcWrite(int percent) {
    Monitors mons;
    EnumDisplayMonitors(nullptr, nullptr, &CollectMonitors, reinterpret_cast<LPARAM>(&mons));
    bool ok = false;
    for (const auto& m : mons.list) {
        DWORD lo = 0, cur = 0, hi = 0;
        if (GetMonitorBrightness(m.hPhysicalMonitor, &lo, &cur, &hi) && hi > lo)
            ok = SetMonitorBrightness(m.hPhysicalMonitor, lo + static_cast<DWORD>(std::clamp(percent, 0, 100)) * (hi - lo) / 100) || ok;
    }
    return ok;
}

}  // namespace

BrightnessState ReadBrightness() {
    BrightnessState s;
    Com com;
    if (!com.usable) return s;
    int p = 0;
    if (WmiRead(&p)) {
        s.available = s.internalPanel = true;
        s.percent = p;
    } else if (DdcRead(&p)) {
        s.available = true;
        s.percent = p;
    }
    return s;
}

bool SetBrightness(int percent) {
    Com com;
    if (!com.usable) return false;
    return WmiWrite(percent) || DdcWrite(percent);
}

}  // namespace velora::control
