#include "ui/backdrop.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "dock/dock_layout.h"
#include "wallpaper/wallpaper.h"

namespace velora::ui {

using Microsoft::WRL::ComPtr;

bool LoadWallpaperRegion(const std::wstring& path, bool stretch, const RECT& mon, const RECT& region, int outW, int outH,
                         Pixels* out) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)))
        return false;
    UINT iw = 0, ih = 0;
    if (FAILED(frame->GetSize(&iw, &ih)) || !iw || !ih) return false;
    const int rw = region.right - region.left, rh = region.bottom - region.top;
    if (rw < 1 || rh < 1 || outW < 1 || outH < 1) return false;
    dock::SourceRect s = dock::WallpaperSourceRect(static_cast<int>(iw), static_cast<int>(ih), mon.right - mon.left,
                                                   mon.bottom - mon.top, stretch, region.left - mon.left, region.top - mon.top, rw, rh);
    int x0 = std::max(0, static_cast<int>(std::floor(s.x))), y0 = std::max(0, static_cast<int>(std::floor(s.y)));
    int x1 = std::min(static_cast<int>(iw), static_cast<int>(std::ceil(s.x + s.w))),
        y1 = std::min(static_cast<int>(ih), static_cast<int>(std::ceil(s.y + s.h)));
    if (x1 - x0 < 1 || y1 - y0 < 1) return false;
    WICRect clip{x0, y0, x1 - x0, y1 - y0};
    ComPtr<IWICBitmapClipper> clipper;
    ComPtr<IWICFormatConverter> conv;
    ComPtr<IWICBitmapScaler> scaler;
    // Premultiply before scaling (scaling straight alpha bleeds dark edges), then scale with the Fant filter.
    if (FAILED(wic->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(frame.Get(), &clip)) ||
        FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(clipper.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)) ||
        FAILED(wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(conv.Get(), static_cast<UINT>(outW), static_cast<UINT>(outH), WICBitmapInterpolationModeFant)))
        return false;
    out->width = outW;
    out->height = outH;
    out->bgra.assign(static_cast<size_t>(outW) * outH * 4, 0);
    return SUCCEEDED(scaler->CopyPixels(nullptr, static_cast<UINT>(outW) * 4, static_cast<UINT>(out->bgra.size()), out->bgra.data()));
}

bool BlurredWallpaper(const RECT& mon, const RECT& region, int shrink, float blurDip, UINT dpi, Pixels* out) {
    std::wstring path;
    int position = 4;  // DWPOS_FILL
    if (!wallpaper::CurrentFor(mon.left, mon.top, mon.right, mon.bottom, &path, &position)) return false;
    shrink = std::max(1, shrink);
    const int w = std::max(1, static_cast<int>(region.right - region.left + shrink - 1) / shrink),
              h = std::max(1, static_cast<int>(region.bottom - region.top + shrink - 1) / shrink);
    if (!LoadWallpaperRegion(path, position == 2 /* DWPOS_STRETCH */, mon, region, w, h, out)) return false;
    dock::BoxBlur(out->bgra, out->width, out->height,
                  std::max(1, static_cast<int>(std::lround(blurDip * Scale(dpi) / static_cast<float>(shrink)))));
    return true;
}

}  // namespace velora::ui
