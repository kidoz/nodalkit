#pragma once

#include <algorithm>
#include <cmath>
#include <nk/style/theme.h>
#include <nk/text/font.h>
#include <variant>

namespace nk::detail {

inline float text_scale() {
    const auto* value = Theme::active()->token("text-scale");
    if (value != nullptr) {
        if (const auto* scale = std::get_if<float>(value);
            scale != nullptr && std::isfinite(*scale)) {
            return std::clamp(*scale, 1.0F, 2.25F);
        }
    }
    return 1.0F;
}

// Widget fonts are nominal logical sizes. Apply accessibility scaling exactly
// once at measurement and snapshot boundaries, independently of device scale.
inline FontDescriptor scaled_font(FontDescriptor font) {
    font.size *= text_scale();
    return font;
}

} // namespace nk::detail
