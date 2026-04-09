#pragma once

/// @file gdi_text_shaper.h
/// @brief GDI-based text shaper for Windows.

#include <nk/text/text_shaper.h>

namespace nk {

class GdiTextShaper : public TextShaper {
public:
    GdiTextShaper();
    ~GdiTextShaper() override;

    [[nodiscard]] Size measure(std::string_view text, const FontDescriptor& font) const override;

    [[nodiscard]] ShapedText
    shape(std::string_view text, const FontDescriptor& font, Color color) const override;
};

} // namespace nk
