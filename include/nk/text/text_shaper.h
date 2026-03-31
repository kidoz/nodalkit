#pragma once

/// @file text_shaper.h
/// @brief Abstract text measurement and rasterization interface.

#include <nk/foundation/types.h>
#include <nk/text/font.h>
#include <nk/text/shaped_text.h>

#include <memory>
#include <string_view>

namespace nk {

/// Abstract interface for measuring and rasterizing text.
/// Platform backends provide implementations (CoreText on macOS,
/// FreeType on Linux).
class TextShaper {
public:
    virtual ~TextShaper() = default;

    TextShaper(TextShaper const&) = delete;
    TextShaper& operator=(TextShaper const&) = delete;

    /// Measure text without rasterizing. Returns dimensions.
    [[nodiscard]] virtual Size measure(
        std::string_view text, FontDescriptor const& font) const = 0;

    /// Shape and rasterize text into a bitmap.
    [[nodiscard]] virtual ShapedText shape(
        std::string_view text, FontDescriptor const& font,
        Color color) const = 0;

    /// Create the platform-appropriate text shaper.
    [[nodiscard]] static std::unique_ptr<TextShaper> create();

protected:
    TextShaper() = default;
};

} // namespace nk
