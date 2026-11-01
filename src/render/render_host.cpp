#include "render/render_host.h"

#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/log.h"

using Microsoft::WRL::ComPtr;

namespace velora::render {

namespace {

// Not in older SDK headers / only valid on Windows 11 22621+.
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr int kBackdropNone = 1, kBackdropMica = 2, kBackdropAcrylic = 3;
constexpr int kCornerDefault = 0, kCornerRound = 2;

std::string HrText(const char* what, HRESULT hr) {
    char b[96];
    std::snprintf(b, sizeof b, "%s failed: 0x%08lX", what, static_cast<unsigned long>(hr));
    return b;
}

}  // namespace

D2D1_COLOR_F ToD2D(tokens::Color c) {
    return D2D1::ColorF(static_cast<float>((c.argb >> 16) & 0xFF) / 255.0f, static_cast<float>((c.argb >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(c.argb & 0xFF) / 255.0f, static_cast<float>((c.argb >> 24) & 0xFF) / 255.0f);
}

bool ApplyBackdrop(HWND hwnd, Backdrop kind, tokens::Theme theme) {
    BOOL dark = theme == tokens::Theme::Dark;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof dark);
    int type = kind == Backdrop::Mica ? kBackdropMica : kind == Backdrop::Acrylic ? kBackdropAcrylic : kBackdropNone;
    // E_INVALIDARG on builds before 22621: report unsupported, never throw.
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &type, sizeof type));
}

bool ApplyRoundedCorners(HWND hwnd, bool round) {
    int pref = round ? kCornerRound : kCornerDefault;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &pref, sizeof pref));
}

bool RenderHost::CreateDevices(bool allowSoftware) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr) && allowSoftware) {
        log::Warn(HrText("hardware D3D11 device", hr) + "; using WARP");
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               &d3d_, nullptr, nullptr);
        software_ = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        log::Error(HrText("D3D11CreateDevice", hr));
        return false;
    }
    if (FAILED(d3d_.As(&dxgi_))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_->GetAdapter(&adapter)) || FAILED(adapter.As(&adapter_))) return false;
    if (FAILED(adapter_->GetParent(IID_PPV_ARGS(&factory_)))) return false;

    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(d2dFactory_.GetAddressOf()))))
        return false;
    ComPtr<ID2D1Device> dev;
    if (FAILED(d2dFactory_->CreateDevice(dxgi_.Get(), &dev)) || FAILED(dev.As(&d2dDevice_))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_))) return false;

    if (FAILED(DCompositionCreateDevice(dxgi_.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    if (FAILED(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_))) return false;
    if (FAILED(dcomp_->CreateVisual(&visual_))) return false;
    return true;
}

bool RenderHost::CreateTarget() {
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width_;
    sd.Height = height_;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sd.Scaling = DXGI_SCALING_STRETCH;
    HRESULT hr = factory_->CreateSwapChainForComposition(d3d_.Get(), &sd, nullptr, &swapChain_);
    if (FAILED(hr)) {
        log::Error(HrText("CreateSwapChainForComposition", hr));
        return false;
    }
    if (FAILED(visual_->SetContent(swapChain_.Get())) || FAILED(dcompTarget_->SetRoot(visual_.Get())) ||
        FAILED(dcomp_->Commit()))
        return false;

    ComPtr<IDXGISurface> surface;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    UINT dpi = GetDpiForWindow(hwnd_);
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), static_cast<float>(dpi),
        static_cast<float>(dpi));
    if (FAILED(context_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &target_))) return false;
    context_->SetTarget(target_.Get());
    return true;
}

void RenderHost::ReleaseTarget() {
    if (context_) context_->SetTarget(nullptr);
    target_.Reset();
}

bool RenderHost::Initialize(HWND hwnd, bool allowSoftwareFallback) {
    Shutdown();
    hwnd_ = hwnd;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    height_ = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    if (!CreateDevices(allowSoftwareFallback) || !CreateTarget()) {
        Shutdown();
        return false;
    }
    return true;
}

void RenderHost::Shutdown() {
    ReleaseTarget();
    visual_.Reset();
    dcompTarget_.Reset();
    dcomp_.Reset();
    context_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    swapChain_.Reset();
    factory_.Reset();
    adapter_.Reset();
    dxgi_.Reset();
    d3d_.Reset();
    hwnd_ = nullptr;
    width_ = height_ = 0;
    drawing_ = false;
}

bool RenderHost::Resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) return false;
    if (width == width_ && height == height_) return true;
    if (drawing_) return false;
    ReleaseTarget();
    HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        log::Error(HrText("ResizeBuffers", hr));
        return false;
    }
    width_ = width;
    height_ = height;
    ComPtr<IDXGISurface> surface;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    UINT dpi = GetDpiForWindow(hwnd_);
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), static_cast<float>(dpi),
        static_cast<float>(dpi));
    if (FAILED(context_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &target_))) return false;
    context_->SetTarget(target_.Get());
    return true;
}

ID2D1DeviceContext* RenderHost::BeginDraw(const D2D1_COLOR_F& clear) {
    if (!Ready() || drawing_) return nullptr;
    context_->BeginDraw();
    context_->Clear(clear);
    drawing_ = true;
    return context_.Get();
}

bool RenderHost::EndDraw() {
    if (!drawing_) return false;
    drawing_ = false;
    HRESULT hr = context_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET || FAILED(hr)) {
        log::Error(HrText("D2D EndDraw", hr));
        return false;  // caller re-Initializes (device lost)
    }
    hr = swapChain_->Present(1, 0);
    if (FAILED(hr)) {
        log::Error(HrText("Present", hr));
        return false;
    }
    return SUCCEEDED(dcomp_->Commit());
}

GpuStats RenderHost::Stats() const {
    GpuStats s;
    s.software = software_;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (adapter_ && SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        s.budgetBytes = info.Budget;
        s.currentUsageBytes = info.CurrentUsage;
    }
    return s;
}

}  // namespace velora::render
