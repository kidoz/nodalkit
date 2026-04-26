#pragma once

/// @file freetype_shaper.h
/// @brief FreeType + fontconfig text shaper for Linux (private header).

#include <memory>
#include <nk/text/text_shaper.h>

namespace nk {

class FreeTypeShaper : public TextShaper {
public:
    FreeTypeShaper();
    ~FreeTypeShaper() override;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
