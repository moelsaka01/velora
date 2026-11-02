// Icon tiles (macOS 27 style): an icon that is not already a filled rounded square is drawn at ~86 % on a neutral
// rounded-square tile. Pure software on premultiplied BGRA, no COM, so it is unit-testable.
#pragma once

#include "ui/ui_common.h"

namespace velora::ui {

inline constexpr float kTileMargin = 0.03f;     // transparent border kept around the tile for its shadow
inline constexpr float kTileContent = 0.86f;    // icon edge / tile edge when the icon is placed on a tile (fills the tile like macOS art)
inline constexpr float kTileRadius = 0.227f;    // tokens::dock::kIconRadiusFraction (spec Dock icon radius)

// Fraction (0..1) of the rounded-square area (radius kTileRadius, inset 2 %) that is opaque. A full-bleed app tile
// scores ~1; a transparent glyph or a circle scores well below 0.9.
double SquircleCoverage(const Pixels& p);
// True when `p` already looks like a filled rounded-square icon (needs no tile).
bool IsFilledTile(const Pixels& p);
// `p` on the neutral tile (same size as `p`): tile with a vertical gradient and a soft shadow, the icon scaled to
// kTileContent of the tile. Square input expected (the loader always yields tier x tier).
Pixels ComposeOnTile(const Pixels& p);
// IsFilledTile ? unchanged : ComposeOnTile. Returns whether a tile was added.
bool EnsureTile(Pixels* p);

}  // namespace velora::ui
