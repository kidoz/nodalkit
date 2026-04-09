#pragma once

/// @file freetype_shaper.h
/// @brief FreeType + fontconfig text shaper for Linux (private header).

#include <nk/text/text_shaper.h>

#include <memory>

namespace nk {

class FreeTypeShaper : public TextShaper {
public:
    FreeTypeShaper();
    ~FreeTypeShaper() override;

    [[nodiscard]] Size measure(
        std::string_view text, FontDescriptor const& font) const override;

    [[nodiscard]] ShapedText shape(
        std::string_view text, FontDescriptor const& font,
        Color color) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
