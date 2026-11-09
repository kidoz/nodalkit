#include "text_boundaries.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <nk/text/font.h>
#include <nk/text/text_shaper.h>
#include <string_view>

#ifdef __APPLE__
#include "macos/coretext_shaper.h"
#elif defined(_WIN32)
#include "windows/directwrite_text_shaper.h"
#include "windows/gdi_text_shaper.h"
#elif defined(__linux__)
#include "linux/freetype_shaper.h"
#endif

namespace nk {

std::size_t TextShaper::hit_test(std::string_view text, const FontDescriptor& font, float x) const {
    std::size_t best = 0;
    float distance = std::numeric_limits<float>::infinity();
    for (const auto boundary : detail::grapheme_boundaries(text)) {
        const float candidate = std::abs(measure(text.substr(0, boundary), font).width - x);
        if (candidate < distance) {
            best = boundary;
            distance = candidate;
        }
    }
    return best;
}

void TextShaper::set_scale_factor(float /*scale_factor*/) {}

void TextShaper::set_system_default_family(std::string_view /*family*/, bool /*monospace*/) {}

void TextShaper::set_system_document_family(std::string_view /*family*/) {}

Size TextShaper::measure_wrapped(std::string_view text,
                                 const FontDescriptor& font,
                                 float /*max_width*/) const {
    return measure(text, font);
}

ShapedText TextShaper::shape_wrapped(std::string_view text,
                                     const FontDescriptor& font,
                                     Color color,
                                     float /*max_width*/) const {
    return shape(text, font, color);
}

std::unique_ptr<TextShaper> TextShaper::create() {
#ifdef __APPLE__
    return std::make_unique<CoreTextShaper>();
#elif defined(_WIN32)
    return std::make_unique<DirectWriteTextShaper>();
#elif defined(__linux__)
    return std::make_unique<FreeTypeShaper>();
#else
    return nullptr;
#endif
}

} // namespace nk
