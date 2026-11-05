#include "control/controls.h"

#include <wlanapi.h>

#include <algorithm>
#include <map>

#include "core/text.h"

namespace velora::control {

std::vector<WifiNetwork> NormalizeNetworks(std::vector<WifiNetwork> raw) {
    std::map<std::wstring, WifiNetwork> best;
    for (auto& n : raw) {
        if (n.ssid.empty()) continue;  // hidden network
        auto it = best.find(n.ssid);
        if (it == best.end()) {
            best.emplace(n.ssid, n);
            continue;
        }
        WifiNetwork& cur = it->second;
        cur.connected = cur.connected || n.connected;
        if (cur.profile.empty()) cur.profile = n.profile;
        cur.secure = cur.secure || n.secure;
        cur.signal = std::max(cur.signal, n.signal);
    }
    std::vector<WifiNetwork> out;
    for (auto& [ssid, n] : best) out.push_back(std::move(n));
    std::sort(out.begin(), out.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.connected != b.connected) return a.connected;
        if (a.profile.empty() != b.profile.empty()) return !a.profile.empty();
        if (a.signal != b.signal) return a.signal > b.signal;
        return a.ssid < b.ssid;
    });
    return out;
}

namespace {

struct Handle {
    HANDLE h = nullptr;
    Handle() {
        DWORD ver = 0;
        if (WlanOpenHandle(2, nullptr, &ver, &h) != ERROR_SUCCESS) h = nullptr;
    }
    ~Handle() {
        if (h) WlanCloseHandle(h, nullptr);
    }
};

std::wstring SsidToString(const DOT11_SSID& s) {
    // SSIDs are bytes; Windows shows them as UTF-8 text.
    return Utf8ToWide(std::string(reinterpret_cast<const char*>(s.ucSSID), s.uSSIDLength));
}

}  // namespace

WifiState ReadWifi() {
    WifiState st;
    Handle wlan;
    if (!wlan.h) return st;
    PWLAN_INTERFACE_INFO_LIST ifs = nullptr;
    if (WlanEnumInterfaces(wlan.h, nullptr, &ifs) != ERROR_SUCCESS || !ifs) return st;
    if (ifs->dwNumberOfItems == 0) {
        WlanFreeMemory(ifs);
        return st;
    }
    st.available = true;
    const GUID guid = ifs->InterfaceInfo[0].InterfaceGuid;
    WlanFreeMemory(ifs);

    PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
    DWORD size = 0;
    if (WlanQueryInterface(wlan.h, &guid, wlan_intf_opcode_current_connection, nullptr, &size, reinterpret_cast<PVOID*>(&conn), nullptr) == ERROR_SUCCESS && conn) {
        if (conn->isState == wlan_interface_state_connected) {
            st.connected = true;
            st.ssid = SsidToString(conn->wlanAssociationAttributes.dot11Ssid);
        }
        WlanFreeMemory(conn);
    }
    PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
    if (WlanGetAvailableNetworkList(wlan.h, &guid, 0, nullptr, &list) == ERROR_SUCCESS && list) {
        std::vector<WifiNetwork> raw;
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
            const WLAN_AVAILABLE_NETWORK& a = list->Network[i];
            WifiNetwork n;
            n.ssid = SsidToString(a.dot11Ssid);
            n.profile = a.strProfileName;
            n.signal = static_cast<int>(a.wlanSignalQuality);
            n.secure = a.bSecurityEnabled != FALSE;
            n.connected = (a.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
            raw.push_back(std::move(n));
        }
        WlanFreeMemory(list);
        st.networks = NormalizeNetworks(std::move(raw));
    }
    return st;
}

bool ConnectSaved(const std::wstring& profile) {
    if (profile.empty()) return false;
    Handle wlan;
    if (!wlan.h) return false;
    PWLAN_INTERFACE_INFO_LIST ifs = nullptr;
    if (WlanEnumInterfaces(wlan.h, nullptr, &ifs) != ERROR_SUCCESS || !ifs) return false;
    bool ok = false;
    if (ifs->dwNumberOfItems > 0) {
        WLAN_CONNECTION_PARAMETERS p{};
        p.wlanConnectionMode = wlan_connection_mode_profile;
        p.strProfile = profile.c_str();
        p.dot11BssType = dot11_BSS_type_any;
        ok = WlanConnect(wlan.h, &ifs->InterfaceInfo[0].InterfaceGuid, &p, nullptr) == ERROR_SUCCESS;
    }
    WlanFreeMemory(ifs);
    return ok;
}

}  // namespace velora::control
