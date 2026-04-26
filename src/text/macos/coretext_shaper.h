#pragma once

/// @file coretext_shaper.h
/// @brief CoreText-based text shaper for macOS.

#include <nk/text/text_shaper.h>

namespace nk {

/// macOS text shaper using CoreText. Zero external dependencies.
class CoreTextShaper : public TextShaper {
public:
    CoreTextShaper();
    ~CoreTextShaper() override;

    [[nodiscard]] Size measure(std::string_view text, const FontDescriptor& font) const override;

    [[nodiscard]] ShapedText
    shape(std::string_view text, const FontDescriptor& font, Color color) const override;

    [[nodiscard]] Size measure_wrapped(std::string_view text,
                                       const FontDescriptor& font,
                                       float max_width) const override;

    [[nodiscard]] ShapedText shape_wrapped(std::string_view text,
                                           const FontDescriptor& font,
                                           Color color,
                                           float max_width) const override;
};

} // namespace nk
