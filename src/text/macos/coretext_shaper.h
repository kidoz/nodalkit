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

    [[nodiscard]] Size measure(
        std::string_view text,
        FontDescriptor const& font) const override;

    [[nodiscard]] ShapedText shape(
        std::string_view text,
        FontDescriptor const& font,
        Color color) const override;
};

} // namespace nk
