// Blurred-wallpaper material shared by the Dock shelf and the Apps panel. Owner: B.
//
// Windows cannot blur "what is behind a rounded rectangle" for a window that is not entirely glass, so these surfaces draw
// their own backdrop: the part of the wallpaper behind them, scaled and box-blurred. Needs COM on the calling thread.
#pragma once

#include <string>

#include "core/win.h"
#include "ui/ui_common.h"

namespace velora::ui {

// The part of the wallpaper image at `path` that lies behind the screen region `region` on the monitor `mon` (both in screen
// pixels), scaled to outW x outH pixels (premultiplied BGRA). WIC pulls rows on demand, so a large image is never decoded in
// full. Positions other than "stretch" are treated as "fill" (covers the monitor, centred), which is what Velora's own
// wallpaper uses. False when the image cannot be read.
bool LoadWallpaperRegion(const std::wstring& path, bool stretch, const RECT& mon, const RECT& region, int outW, int outH,
                         Pixels* out);

// The blurred wallpaper behind `region` of the monitor `mon`, at 1 / shrink of the region's size (the caller draws it
// stretched; a smaller bitmap costs less memory and blurs faster). `blurDip` is the Gaussian sigma in DIPs of a monitor at
// `dpi`. False when the monitor has no wallpaper image (a solid colour, a slideshow) or it cannot be read.
bool BlurredWallpaper(const RECT& mon, const RECT& region, int shrink, float blurDip, UINT dpi, Pixels* out);

}  // namespace velora::ui
