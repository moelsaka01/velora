// Direct2D + DirectComposition render host and DWM backdrop helper. Owner: A.
//
// One RenderHost owns a D3D11 device (hardware, WARP fallback), a composition
// swap chain with premultiplied alpha, a DirectComposition visual tree bound to
// an HWND, and a D2D device context drawing into the swap chain. Windows using
// it are layered by composition, not by WS_EX_LAYERED. Used by the Dock, menu
// bar, Overview and popovers (owners B/C).
#pragma once

#include <d2d1_3.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>

#include "core/win.h"
#include "render/tokens.h"

namespace velora::render {

enum class Backdrop { None, Mica, Acrylic };

// DWM-drawn backdrop (Windows 11 22621+). Returns false when unsupported; the
// caller then paints PaletteFor(theme).surfaceStrong, the opaque fallback.
bool ApplyBackdrop(HWND hwnd, Backdrop kind, tokens::Theme theme);
// Rounded corners drawn by DWM for plain top-level windows.
bool ApplyRoundedCorners(HWND hwnd, bool round);

D2D1_COLOR_F ToD2D(tokens::Color c);

struct GpuStats {
    std::uint64_t budgetBytes = 0;
    std::uint64_t currentUsageBytes = 0;
    bool software = false;  // WARP
};

class RenderHost {
public:
    RenderHost() = default;
    RenderHost(const RenderHost&) = delete;
    RenderHost& operator=(const RenderHost&) = delete;
    ~RenderHost() { Shutdown(); }

    // Creates device + composition target for `hwnd` at its client size. A false return
    // leaves the host unusable and logged; callers degrade to plain GDI/opaque painting.
    bool Initialize(HWND hwnd, bool allowSoftwareFallback = true);
    void Shutdown();
    // Returns idle GPU/driver memory to the system (DXGI Trim + D2D ClearResources). Safe between frames.
    void Trim();

    bool Resize(UINT width, UINT height);

    // Draw between BeginDraw()/EndDraw(); EndDraw presents and commits.
    ID2D1DeviceContext* BeginDraw(const D2D1_COLOR_F& clear);
    bool EndDraw();

    // Gaming mode: no blur, no composition effects, minimal work per frame.
    void SetEffectsEnabled(bool enabled) { effects_ = enabled; }
    bool EffectsEnabled() const { return effects_; }

    GpuStats Stats() const;
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    bool Ready() const { return swapChain_ != nullptr; }

private:
    bool CreateDevices(bool allowSoftware);
    bool CreateTarget();
    void ReleaseTarget();

    HWND hwnd_ = nullptr;
    UINT width_ = 0, height_ = 0;
    bool effects_ = true;
    bool software_ = false;
    bool drawing_ = false;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Factory3> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device2> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext2> context_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
};

}  // namespace velora::render
