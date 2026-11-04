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
inline constexpr float kLarge  = 16.0f;  // panels, Control Center
inline constexpr float kDockShelf = 20.5f;  // Dock shelf (measured, docs/MACOS_UI_SPEC.md)
inline constexpr float kWindow = 12.0f;  // Velora app windows
}  // namespace radius

namespace spacing {
inline constexpr float k1 = 2.0f, k2 = 4.0f, k3 = 8.0f, k4 = 12.0f,
                       k5 = 16.0f, k6 = 24.0f, k7 = 32.0f;
}  // namespace spacing

namespace size {
inline constexpr float kMenuBarHeight   = 30.0f;  // spec derives ~29 (notch Air reserves 31); Velora targets 30
inline constexpr float kDockIcon        = 44.0f;  // measured, docs/MACOS_UI_SPEC.md "Dock"
inline constexpr float kDockIconMax     = 74.8f;  // kDockIcon x kMagnifyPeak
inline constexpr float kDockPadding     = 15.0f;
inline constexpr float kDockBottomInset = 5.5f;
}  // namespace size

// Dock (docs/MACOS_UI_SPEC.md, "Dock"; all measured from public screenshots unless noted).
namespace dock {
inline constexpr float kShelfHeight = 74.0f;      // icon + 2 x padding
inline constexpr float kSpacing     = 14.0f;      // between icons
inline constexpr float kIconRadiusFraction = 0.227f;  // icon art corner radius / icon size
inline constexpr float kDot         = 4.0f;       // running indicator diameter
inline constexpr float kDotBelowIcon = 9.25f;     // dot centre below the icon's bottom edge (unmagnified)
inline constexpr float kShadowPad   = 12.0f;      // room around the shelf for the soft shadow
// Magnification: peak is a working value (1.6-2.0 in the spec, "needs device video"); the curve and the 6-icon reach
// are PuruVJ/macos-web (MIT, see THIRD_PARTY_NOTICES.md). The reach is kMagnifyReach x (icon + spacing).
inline constexpr float kMagnifyPeak  = 1.7f;
inline constexpr float kMagnifyReach = 6.0f;
// Material (light: derived from the measured regression, rgb(150,150,152) at 31 %; dark: estimate, unmeasured).
inline constexpr float kVeilAlphaLight = 0.31f, kVeilAlphaDark = 0.38f;
inline constexpr float kVeilAlphaUnblurred = 0.58f;  // when there is no backdrop to blur: stay legible
inline constexpr float kBlurSigma = 12.0f;            // DIPs
}  // namespace dock

// Menu bar and menus (docs/MACOS_UI_SPEC.md, "Menu bar and menus"; measured from public screenshots).
namespace menu {
inline constexpr float kFontSize     = 13.0f;
inline constexpr float kTitleGap     = 21.75f;   // text to text between titles / status items
inline constexpr float kCapsuleHeight = 23.0f;   // selected-title capsule (fully rounded)
inline constexpr float kCapsulePadX  = 15.0f;    // capsule edge to label
inline constexpr float kPopupWidth   = 201.5f;
inline constexpr float kPopupPadding = 5.0f;     // popup edge to highlight
inline constexpr float kRowHeight    = 24.0f;
inline constexpr float kSeparatorHeight = 11.0f; // 5.5 above + 5.5 below the hairline (group pitch 35 = 24 + 11)
inline constexpr float kHighlightRadius = 6.0f;
inline constexpr float kPopupRadius  = 11.0f;
inline constexpr float kPopupGap     = 4.5f;     // bar bottom (capsule bottom) to popup top
inline constexpr float kRowTextInset = 10.0f;    // highlight edge to label (working value, not measured)
// Bar veil: the spec's bar is fully transparent with white text; over an arbitrary wallpaper that is unreadable, so Velora keeps a
// light veil over the DWM backdrop (working values, not measured).
inline constexpr std::uint32_t kBarVeilLight = 0x40F5F5F7, kBarVeilDark = 0x59242426;
// Popup: white-ish fill at about 77 % over a blur (light, measured, low confidence); dark is an estimate.
inline constexpr std::uint32_t kPopupFillLight = 0xC4F8F8FA, kPopupFillDark = 0xC4262628;
}  // namespace menu

// Apps panel (docs/MACOS_UI_SPEC.md, "Apps"; measured, low confidence unless noted).
namespace appspanel {
inline constexpr float kWidth      = 843.0f;   // medium
inline constexpr float kIcon       = 60.0f;
inline constexpr float kColumnPitch = 115.0f;
inline constexpr float kRowPitch   = 102.0f;
inline constexpr float kRadius     = 45.0f;    // "about 45 pt or more"; clamped to half the panel's short side
inline constexpr float kInset      = 16.0f;    // panel edge to the first column (working value)
inline constexpr float kBlurSigma  = 20.0f;    // DIPs (working value)
inline constexpr std::uint32_t kVeil = 0xB35A5A5C;  // rgb(90,90,92) at 70 %: neutral dark frosted glass, light and dark mode alike (was a warm brown fitted to one wallpaper)
inline constexpr std::uint32_t kVeilUnblurred = 0xE03A3A3C;  // no wallpaper image to blur, or no transparency
inline constexpr float kOpenFrom   = 0.94f;    // scale at the start of the open spring (working value)
}  // namespace appspanel

// Windows of Velora's own apps (Files, later Settings): docs/MACOS_UI_SPEC.md, "Windows, toolbar, sidebar" (measured unless noted).
namespace window {
inline constexpr float kSidebarWidth = 216.0f, kSidebarRowPitch = 32.0f;
inline constexpr float kToolbarHeight = 52.0f, kToolbarControl = 36.0f;
inline constexpr float kTrafficDiameter = 13.0f, kTrafficPitch = 23.0f, kTrafficLeft = 20.0f, kTrafficCenterY = 26.0f;
inline constexpr float kCornerRadius = 16.0f;  // spec: 15.25 circular-equivalent; drawn by DWM (8 px system rounding) until a custom clip is added
inline constexpr std::uint32_t kSidebarLight = 0xF5ECEBEA;  // rgb(236,235,234) at 96 %
inline constexpr std::uint32_t kSidebarDark = 0xF528282A;   // estimate, unmeasured
inline constexpr std::uint32_t kContentLight = 0xFFFFFFFF;  // measured
inline constexpr std::uint32_t kContentDark = 0xFF1E1E20;   // estimate, unmeasured
}  // namespace window

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

// Spring from a perceptual duration (s) and bounce (0 = no overshoot, ~0.15 subtle, ~0.3 clear): mass 1,
// stiffness (2 pi / duration)^2, damping 4 pi (1 - bounce) / duration (WWDC23 10158; the damping coefficient is
// 2 x damping ratio x sqrt(stiffness) with ratio 1 - bounce, which is how the session's formula reads dimensionally).
constexpr Spring SpringFrom(float durationSeconds, float bounce) {
    constexpr float kTwoPi = 6.2831853f;
    return {(kTwoPi / durationSeconds) * (kTwoPi / durationSeconds), 2.0f * kTwoPi * (1.0f - bounce) / durationSeconds, 1.0f};
}
inline constexpr Spring kDockMagnify = SpringFrom(0.4f, 0.1f);  // spec Motion: duration 0.4 s, bounce 0.1
inline constexpr Spring kPanelOpen   = SpringFrom(0.45f, 0.0f); // spec Motion: panels, duration 0.45 s, bounce 0
}  // namespace timing

// 0xAARRGGBB
struct Color { std::uint32_t argb; };

// Glass surface: a neutral veil over the blurred backdrop, a light rim and a soft shadow.
struct Glass {
    Color veil;       // alpha is the veil strength (0xAA); the Dock multiplies in its own measured value
    Color rimTop;     // 1-pt inner rim, fading to rimBottom
    Color rimBottom;
    Color shadow;
};
inline constexpr Glass kGlassLight {{0xFF969698}, {0x8CFFFFFF}, {0x33FFFFFF}, {0x47000000}};  // rgb(150,150,152)
inline constexpr Glass kGlassDark  {{0xFF3C3C40}, {0x47FFFFFF}, {0x14FFFFFF}, {0x73000000}};  // estimate
constexpr const Glass& GlassFor(Theme t) { return t == Theme::Dark ? kGlassDark : kGlassLight; }

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
