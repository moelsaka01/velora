// Design tokens shared by all modules (radius, blur, spacing, timing curves,
// colors). Owner: A (render/). Values are original, not copied from any OS.
// Header-only and <windows.h>-free so tests and on-demand apps can use it.
#pragma once

#include <array>
#include <cstdint>

namespace velora::tokens {

enum class Theme : std::uint8_t { Light, Dark };

// Lengths are in device-independent pixels (DIPs, 96 dpi).
namespace radius {
inline constexpr float kSmall  = 6.0f;   // buttons, menu items
inline constexpr float kMedium = 10.0f;  // menus, popovers
inline constexpr float kLarge  = 16.0f;  // panels, Dock, Control Center
inline constexpr float kWindow = 12.0f;  // Velora app windows
}  // namespace radius

namespace spacing {
inline constexpr float k1 = 2.0f, k2 = 4.0f, k3 = 8.0f, k4 = 12.0f,
                       k5 = 16.0f, k6 = 24.0f, k7 = 32.0f;
}  // namespace spacing

namespace size {
inline constexpr float kMenuBarHeight   = 28.0f;
inline constexpr float kDockIcon        = 52.0f;
inline constexpr float kDockIconMax     = 84.0f;  // at full magnification
inline constexpr float kDockPadding     = 8.0f;
inline constexpr float kDockBottomInset = 8.0f;
}  // namespace size

// Blur radius (DIPs) for backdrop effects. Gaming mode forces blur off.
namespace blur {
inline constexpr float kNone = 0.0f, kLight = 12.0f, kMedium = 24.0f, kHeavy = 40.0f;
}  // namespace blur

// Timing in milliseconds. Curves are cubic-bezier control points (x1,y1,x2,y2).
namespace timing {
inline constexpr std::uint32_t kInstant = 0, kFast = 120, kNormal = 220,
                               kSlow = 350, kOverview = 300;

struct Bezier { float x1, y1, x2, y2; };
inline constexpr Bezier kStandard   {0.2f, 0.0f, 0.0f, 1.0f};   // general motion
inline constexpr Bezier kDecelerate {0.0f, 0.0f, 0.2f, 1.0f};   // entering
inline constexpr Bezier kAccelerate {0.4f, 0.0f, 1.0f, 1.0f};   // leaving
inline constexpr Bezier kLinear     {0.0f, 0.0f, 1.0f, 1.0f};

// Spring for Dock magnification and minimize effects (critically damped-ish).
struct Spring { float stiffness, damping, mass; };
inline constexpr Spring kDockSpring {320.0f, 28.0f, 1.0f};
}  // namespace timing

// 0xAARRGGBB
struct Color { std::uint32_t argb; };

struct Palette {
    Color surface;        // panels, menus (tinted over backdrop blur)
    Color surfaceStrong;  // opaque fallback when blur is disabled
    Color text;
    Color textSecondary;
    Color accent;
    Color separator;
    Color shadow;
};

inline constexpr Palette kLight {
    {0xB3F5F5F7}, {0xFFF2F2F4}, {0xFF1D1D1F}, {0xFF6E6E73}, {0xFF0A6CFF}, {0x26000000}, {0x40000000}};
inline constexpr Palette kDark {
    {0xB3242426}, {0xFF2A2A2C}, {0xFFF5F5F7}, {0xFFA1A1A6}, {0xFF3B8BFF}, {0x33FFFFFF}, {0x80000000}};

constexpr const Palette& PaletteFor(Theme t) { return t == Theme::Dark ? kDark : kLight; }

}  // namespace velora::tokens
