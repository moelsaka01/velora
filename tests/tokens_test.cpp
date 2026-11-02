#include "render/tokens.h"

using namespace velora::tokens;

static_assert(size::kDockIconMax > size::kDockIcon);
static_assert(size::kDockIcon + 2 * size::kDockPadding == dock::kShelfHeight);  // measured: 44 + 2 x 15 = 74
static_assert(size::kDockIconMax <= size::kDockIcon * dock::kMagnifyPeak + 0.01f);
static_assert(timing::kDockMagnify.stiffness > 240.0f && timing::kDockMagnify.stiffness < 250.0f);  // (2 pi / 0.4)^2
static_assert(timing::kDockMagnify.damping < 4 * 3.1415927f / 0.4f);  // under critical damping: a little bounce
static_assert(timing::kPanelOpen.damping * timing::kPanelOpen.damping > 3.99f * timing::kPanelOpen.stiffness &&
              timing::kPanelOpen.damping * timing::kPanelOpen.damping < 4.01f * timing::kPanelOpen.stiffness);  // bounce 0: critically damped
static_assert(size::kMenuBarHeight == 30.0f);
static_assert(menu::kRowHeight + menu::kSeparatorHeight == 35.0f);  // measured group pitch
static_assert(menu::kRowHeight == 24.0f && menu::kPopupWidth == 201.5f && menu::kTitleGap == 21.75f);
static_assert(radius::kSmall < radius::kMedium && radius::kMedium < radius::kLarge);
static_assert(timing::kFast < timing::kNormal && timing::kNormal < timing::kSlow);
static_assert(window::kSidebarWidth == 216.0f && window::kSidebarRowPitch == 32.0f && window::kToolbarControl == 36.0f);  // measured
static_assert(window::kTrafficPitch == 23.0f && window::kTrafficDiameter == 13.0f && window::kTrafficCenterY == 26.0f);  // measured
static_assert(window::kToolbarHeight == 2 * window::kTrafficCenterY);  // derived: the control centre line is 26 pt down
static_assert(PaletteFor(Theme::Dark).text.argb != PaletteFor(Theme::Light).text.argb);

int main() {
    // Bezier x control points must lie in [0,1] for a valid timing function.
    for (const auto& b : {timing::kStandard, timing::kDecelerate, timing::kAccelerate, timing::kLinear})
        if (b.x1 < 0 || b.x1 > 1 || b.x2 < 0 || b.x2 > 1) return 1;
    return 0;
}
