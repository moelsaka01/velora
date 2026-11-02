// Velora's own icons for a handful of Windows apps (original SVG designs in assets/icons/), drawn instead of the
// system's artwork. SVG is rasterised with Direct2D's SVG renderer on a WARP device, so any tier is crisp.
#pragma once

#include <string>

#include "ui/ui_common.h"

namespace velora::ui {

enum class BuiltinIcon { None, Files, Settings, Terminal, CommandPrompt, Calculator, Camera, Clock, ControlPanel, Notepad, TaskManager };

// Which built-in icon stands in for `source` (an exe path, a .lnk path or "shell:AppsFolder\\<AppUserModelID>"), matched
// on the AppUserModelID, the exe name or the shortcut's name. Pure; case-insensitive.
BuiltinIcon BuiltinIconFor(const std::wstring& source);
// The SVG text of an icon (empty for None).
const char* BuiltinIconSvg(BuiltinIcon icon);
const char* BuiltinIconName(BuiltinIcon icon);
// Rasterises `svg` to px x px premultiplied BGRA. False when no device is available (software WARP is the only one used).
bool RasterizeSvg(const char* svg, int px, Pixels* out);
// BuiltinIconFor + RasterizeSvg. False when `source` has no built-in icon or rasterising failed.
bool LoadBuiltinIcon(const std::wstring& source, int px, Pixels* out);

}  // namespace velora::ui
