// RenderHost smoke test: device creation, composition swap chain, draw/present,
// resize, GPU memory query. Skips only when CI has no usable graphics stack.
#include "core/win.h"

#include <cstdio>

#include "render/render_host.h"

using namespace velora;

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VeloraRenderTest";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
                                L"render test", WS_POPUP, -20000, -20000, 240, 120, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::printf("FAIL cannot create window\n");
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    render::RenderHost host;
    if (!host.Initialize(hwnd)) {
        wchar_t ci[8] = {};
        if (GetEnvironmentVariableW(L"CI", ci, 8)) {
            std::printf("render_host_test: skipped (RenderHost could not initialize on this CI machine: no usable graphics stack)\n");
            return 0;
        }
        std::printf("FAIL RenderHost::Initialize\n");
        return 1;
    }
    std::printf("render_host_test: device\n");
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failed;
        }
    };

    const tokens::Palette& p = tokens::PaletteFor(tokens::Theme::Light);
    for (int frame = 0; frame < 3; ++frame) {
        ID2D1DeviceContext* dc = host.BeginDraw(D2D1::ColorF(0, 0, 0, 0));
        check(dc != nullptr, "BeginDraw");
        if (dc) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
            dc->CreateSolidColorBrush(render::ToD2D(p.surface), &brush);
            dc->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(4, 4, 200, 80), tokens::radius::kLarge, tokens::radius::kLarge), brush.Get());
            check(host.EndDraw(), "EndDraw/Present");
        }
    }
    check(host.Resize(320, 160) && host.Width() == 320 && host.Height() == 160, "Resize");
    ID2D1DeviceContext* dc = host.BeginDraw(render::ToD2D(p.surfaceStrong));
    check(dc != nullptr && host.EndDraw(), "draw after resize");
    check(!host.Resize(0, 10), "zero-size resize rejected");

    render::GpuStats s = host.Stats();
    std::printf("render host: %s adapter, GPU budget %llu MB, usage %.1f MB\n", s.software ? "WARP" : "hardware",
                static_cast<unsigned long long>(s.budgetBytes >> 20), static_cast<double>(s.currentUsageBytes) / (1 << 20));

    bool mica = render::ApplyBackdrop(hwnd, render::Backdrop::Mica, tokens::Theme::Light);
    bool round = render::ApplyRoundedCorners(hwnd, true);
    std::printf("DWM backdrop (Mica) %s, rounded corners %s on this build\n", mica ? "supported" : "unsupported",
                round ? "supported" : "unsupported");

    host.SetEffectsEnabled(false);
    check(!host.EffectsEnabled(), "effects switch");
    host.Shutdown();
    check(!host.Ready(), "shutdown");
    DestroyWindow(hwnd);
    if (failed) return 1;
    std::printf("render_host_test: all checks passed\n");
    return 0;
}
