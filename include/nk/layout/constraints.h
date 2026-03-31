#pragma once

/// @file constraints.h
/// @brief Layout measurement constraints and size requests.

#include <nk/foundation/types.h>

#include <algorithm>
#include <limits>

namespace nk {

/// Box constraints for layout measurement, specifying min/max bounds.
struct Constraints {
    float min_width = 0;
    float min_height = 0;
    float max_width = std::numeric_limits<float>::infinity();
    float max_height = std::numeric_limits<float>::infinity();

    /// Unbounded constraints (no minimum, infinite maximum).
    static constexpr Constraints unbounded() { return {}; }

    /// Tight constraints that force an exact size.
    static constexpr Constraints tight(float w, float h) {
        return {w, h, w, h};
    }

    /// Tight constraints from a Size.
    static constexpr Constraints tight(Size s) {
        return tight(s.width, s.height);
    }

    /// Constrain a size to fit within these constraints.
    [[nodiscard]] Size constrain(Size s) const {
        return {
            std::clamp(s.width, min_width, max_width),
            std::clamp(s.height, min_height, max_height),
        };
    }
};

/// Result of Widget::measure(). Reports both the minimum size the widget
/// needs and the natural (preferred) size.
struct SizeRequest {
    float minimum_width = 0;
    float minimum_height = 0;
    float natural_width = 0;
    float natural_height = 0;
};

} // namespace nk
