#include "render/tokens.h"

using namespace velora::tokens;

static_assert(size::kDockIconMax > size::kDockIcon);
static_assert(radius::kSmall < radius::kMedium && radius::kMedium < radius::kLarge);
static_assert(timing::kFast < timing::kNormal && timing::kNormal < timing::kSlow);
static_assert(PaletteFor(Theme::Dark).text.argb != PaletteFor(Theme::Light).text.argb);

int main() {
    // Bezier x control points must lie in [0,1] for a valid timing function.
    for (const auto& b : {timing::kStandard, timing::kDecelerate, timing::kAccelerate, timing::kLinear})
        if (b.x1 < 0 || b.x1 > 1 || b.x2 < 0 || b.x2 > 1) return 1;
    return 0;
}
