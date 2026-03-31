#pragma once

/// @file freetype_shaper.h
/// @brief FreeType + fontconfig text shaper for Linux (private header).

#include <nk/text/text_shaper.h>

#include <string>

typedef struct FT_LibraryRec_* FT_Library;

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
    FT_Library ft_library_ = nullptr;

    /// Resolve a FontDescriptor to a file path via fontconfig.
    [[nodiscard]] std::string resolve_font_path(
        FontDescriptor const& desc) const;
};

} // namespace nk
