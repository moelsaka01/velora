#include "ui/builtin_icons.h"

#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shlwapi.h>

#include <cstring>
#include <cwctype>
#include <mutex>

#include "core/log.h"

using Microsoft::WRL::ComPtr;

namespace velora::ui {

// Defined in the generated builtin_icons_data.cpp (CMake embeds assets/icons/*.svg, name = file stem, '-' -> '_').
extern const char* const kSvg_files;
extern const char* const kSvg_settings;
extern const char* const kSvg_terminal;
extern const char* const kSvg_cmd;
extern const char* const kSvg_calculator;
extern const char* const kSvg_camera;
extern const char* const kSvg_clock;
extern const char* const kSvg_control_panel;
extern const char* const kSvg_notepad;
extern const char* const kSvg_task_manager;
extern const char* const kSvg_character_map;
extern const char* const kSvg_computer_management;
extern const char* const kSvg_component_services;
extern const char* const kSvg_disk_cleanup;
extern const char* const kSvg_event_viewer;
extern const char* const kSvg_registry_editor;
extern const char* const kSvg_services;
extern const char* const kSvg_powershell;
extern const char* const kSvg_snipping_tool;
extern const char* const kSvg_paint;

namespace {

std::wstring LowerStr(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
bool Has(const std::wstring& s, const wchar_t* part) { return s.find(part) != std::wstring::npos; }

// Last path component without extension, lowercase ("c:\\x\\file explorer.lnk" -> "file explorer").
std::wstring Stem(const std::wstring& s) {
    size_t slash = s.find_last_of(L"\\/");
    std::wstring n = slash == std::wstring::npos ? s : s.substr(slash + 1);
    size_t dot = n.find_last_of(L'.');
    if (dot != std::wstring::npos && n.size() - dot <= 5) n.erase(dot);
    return n;
}

}  // namespace

BuiltinIcon BuiltinIconFor(const std::wstring& source) {
    const std::wstring s = LowerStr(source), stem = Stem(s);
    // AppUserModelIDs of the inbox Store apps; exe names for the classic ones; shortcut stems for Start-menu entries.
    if (Has(s, L"shell:appsfolder")) {
        if (Has(s, L"microsoft.windowscalculator")) return BuiltinIcon::Calculator;
        if (Has(s, L"microsoft.windows.camera")) return BuiltinIcon::Camera;
        if (Has(s, L"microsoft.windowsalarms")) return BuiltinIcon::Clock;
        if (Has(s, L"microsoft.windowsnotepad")) return BuiltinIcon::Notepad;
        if (Has(s, L"windowspowershell") || Has(s, L"system32\\powershell.exe")) return BuiltinIcon::PowerShell;
        if (Has(s, L"microsoft.windowsterminal") || Has(s, L"powershell")) return BuiltinIcon::Terminal;
        // Classic tools appear in AppsFolder under their exe / snap-in path (the system folder GUID varies in front of it).
        if (Has(s, L"cmd.exe")) return BuiltinIcon::CommandPrompt;
        if (Has(s, L"charmap")) return BuiltinIcon::CharacterMap;
        if (Has(s, L"compmgmt")) return BuiltinIcon::ComputerManagement;
        if (Has(s, L"comexp")) return BuiltinIcon::ComponentServices;
        if (Has(s, L"cleanmgr")) return BuiltinIcon::DiskCleanup;
        if (Has(s, L"eventvwr")) return BuiltinIcon::EventViewer;
        if (Has(s, L"regedit")) return BuiltinIcon::RegistryEditor;
        if (Has(s, L"services.msc")) return BuiltinIcon::Services;
        if (Has(s, L"microsoft.screensketch") || Has(s, L"snippingtool")) return BuiltinIcon::SnippingTool;
        if (Has(s, L"microsoft.paint") || Has(s, L"mspaint")) return BuiltinIcon::Paint;
        if (Has(s, L"immersivecontrolpanel")) return BuiltinIcon::Settings;
        if (Has(s, L"microsoft.windows.explorer") || Has(s, L"microsoft.windows.fileexplorer")) return BuiltinIcon::Files;
        if (Has(s, L"taskmgr")) return BuiltinIcon::TaskManager;
        if (Has(s, L"controlpanel")) return BuiltinIcon::ControlPanel;
        return BuiltinIcon::None;
    }
    // Velora's own apps (Dock pins point at the executables): the tile would otherwise be the letter "F" / "S".
    if (stem == L"velora-files") return BuiltinIcon::Files;
    if (stem == L"velora-settings") return BuiltinIcon::Settings;
    if (stem == L"explorer" || stem == L"file explorer") return BuiltinIcon::Files;
    if (stem == L"settings") return BuiltinIcon::Settings;
    if (stem == L"powershell" || stem.rfind(L"windows powershell", 0) == 0) return BuiltinIcon::PowerShell;
    if (stem == L"wt" || stem == L"windows terminal" || stem == L"terminal" || stem == L"pwsh" || stem == L"powershell 7" ||
        stem.rfind(L"powershell 7", 0) == 0)
        return BuiltinIcon::Terminal;
    if (stem == L"cmd" || stem.rfind(L"command prompt", 0) == 0) return BuiltinIcon::CommandPrompt;
    if (stem == L"charmap" || stem == L"character map") return BuiltinIcon::CharacterMap;
    if (stem == L"computer management" || stem == L"compmgmt") return BuiltinIcon::ComputerManagement;
    if (stem == L"component services" || stem == L"comexp") return BuiltinIcon::ComponentServices;
    if (stem == L"disk cleanup" || stem == L"cleanmgr") return BuiltinIcon::DiskCleanup;
    if (stem == L"event viewer" || stem == L"eventvwr") return BuiltinIcon::EventViewer;
    if (stem == L"registry editor" || stem == L"regedit") return BuiltinIcon::RegistryEditor;
    if (stem == L"services") return BuiltinIcon::Services;
    if (stem == L"snipping tool" || stem == L"snippingtool") return BuiltinIcon::SnippingTool;
    if (stem == L"paint" || stem == L"mspaint") return BuiltinIcon::Paint;
    if (stem == L"calc" || stem == L"calculator") return BuiltinIcon::Calculator;
    if (stem == L"camera") return BuiltinIcon::Camera;
    if (stem == L"clock" || stem == L"alarms & clock") return BuiltinIcon::Clock;
    if (stem == L"control" || stem == L"control panel") return BuiltinIcon::ControlPanel;
    if (stem == L"notepad") return BuiltinIcon::Notepad;
    if (stem == L"taskmgr" || stem == L"task manager") return BuiltinIcon::TaskManager;
    return BuiltinIcon::None;
}

const char* BuiltinIconSvg(BuiltinIcon i) {
    switch (i) {
        case BuiltinIcon::Files: return kSvg_files;
        case BuiltinIcon::Settings: return kSvg_settings;
        case BuiltinIcon::Terminal: return kSvg_terminal;
        case BuiltinIcon::CommandPrompt: return kSvg_cmd;
        case BuiltinIcon::Calculator: return kSvg_calculator;
        case BuiltinIcon::Camera: return kSvg_camera;
        case BuiltinIcon::Clock: return kSvg_clock;
        case BuiltinIcon::ControlPanel: return kSvg_control_panel;
        case BuiltinIcon::Notepad: return kSvg_notepad;
        case BuiltinIcon::TaskManager: return kSvg_task_manager;
        case BuiltinIcon::CharacterMap: return kSvg_character_map;
        case BuiltinIcon::ComputerManagement: return kSvg_computer_management;
        case BuiltinIcon::ComponentServices: return kSvg_component_services;
        case BuiltinIcon::DiskCleanup: return kSvg_disk_cleanup;
        case BuiltinIcon::EventViewer: return kSvg_event_viewer;
        case BuiltinIcon::RegistryEditor: return kSvg_registry_editor;
        case BuiltinIcon::Services: return kSvg_services;
        case BuiltinIcon::PowerShell: return kSvg_powershell;
        case BuiltinIcon::SnippingTool: return kSvg_snipping_tool;
        case BuiltinIcon::Paint: return kSvg_paint;
        default: return "";
    }
}

const char* BuiltinIconName(BuiltinIcon i) {
    switch (i) {
        case BuiltinIcon::Files: return "files";
        case BuiltinIcon::Settings: return "settings";
        case BuiltinIcon::Terminal: return "terminal";
        case BuiltinIcon::CommandPrompt: return "cmd";
        case BuiltinIcon::Calculator: return "calculator";
        case BuiltinIcon::Camera: return "camera";
        case BuiltinIcon::Clock: return "clock";
        case BuiltinIcon::ControlPanel: return "control-panel";
        case BuiltinIcon::Notepad: return "notepad";
        case BuiltinIcon::TaskManager: return "task-manager";
        case BuiltinIcon::CharacterMap: return "character-map";
        case BuiltinIcon::ComputerManagement: return "computer-management";
        case BuiltinIcon::ComponentServices: return "component-services";
        case BuiltinIcon::DiskCleanup: return "disk-cleanup";
        case BuiltinIcon::EventViewer: return "event-viewer";
        case BuiltinIcon::RegistryEditor: return "registry-editor";
        case BuiltinIcon::Services: return "services";
        case BuiltinIcon::PowerShell: return "powershell";
        case BuiltinIcon::SnippingTool: return "snipping-tool";
        case BuiltinIcon::Paint: return "paint";
        default: return "none";
    }
}

namespace {

// One software (WARP) D2D device for all rasterising: no GPU dependency, created on first use.
std::mutex g_deviceMutex;
ComPtr<ID2D1Device> g_device;
bool g_deviceTried = false;

ComPtr<ID2D1Device> Device() {
    std::lock_guard lock(g_deviceMutex);
    if (g_deviceTried) return g_device;
    g_deviceTried = true;
    ComPtr<ID3D11Device> d3d;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &d3d, nullptr, nullptr))) {
        log::Warn("builtin icons: no WARP device; system icons are used");
        return g_device;
    }
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(d3d.As(&dxgi)) ||
        FAILED(D2D1CreateDevice(dxgi.Get(), D2D1::CreationProperties(D2D1_THREADING_MODE_MULTI_THREADED, D2D1_DEBUG_LEVEL_NONE,
                                                                       D2D1_DEVICE_CONTEXT_OPTIONS_NONE),
                                &g_device)))
        log::Warn("builtin icons: cannot create a D2D device; system icons are used");
    return g_device;
}

}  // namespace

bool RasterizeSvg(const char* svg, int px, Pixels* out) {
    if (!svg || !*svg || px <= 0 || !out) return false;
    ComPtr<ID2D1Device> dev = Device();
    if (!dev) return false;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1DeviceContext5> dc5;
    if (FAILED(dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) || FAILED(dc.As(&dc5))) return false;
    const D2D1_PIXEL_FORMAT fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ComPtr<ID2D1Bitmap1> target, cpu;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(px, px), nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt), &target)) ||
        FAILED(dc->CreateBitmap(D2D1::SizeU(px, px), nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ, fmt), &cpu)))
        return false;
    ComPtr<IStream> stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(svg), static_cast<UINT>(strlen(svg)));
    ComPtr<ID2D1SvgDocument> doc;
    if (!stream || FAILED(dc5->CreateSvgDocument(stream.Get(), D2D1::SizeF(static_cast<float>(px), static_cast<float>(px)), &doc))) return false;
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    dc5->DrawSvgDocument(doc.Get());
    if (FAILED(dc->EndDraw())) return false;
    if (FAILED(cpu->CopyFromBitmap(nullptr, target.Get(), nullptr))) return false;
    D2D1_MAPPED_RECT m{};
    if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return false;
    out->width = out->height = px;
    out->bgra.resize(static_cast<size_t>(px) * px * 4);
    for (int y = 0; y < px; ++y)
        std::memcpy(&out->bgra[static_cast<size_t>(y) * px * 4], m.bits + static_cast<size_t>(y) * m.pitch, static_cast<size_t>(px) * 4);
    cpu->Unmap();
    return true;
}

bool LoadBuiltinIcon(const std::wstring& source, int px, Pixels* out) {
    const BuiltinIcon i = BuiltinIconFor(source);
    return i != BuiltinIcon::None && RasterizeSvg(BuiltinIconSvg(i), px, out);
}

}  // namespace velora::ui
