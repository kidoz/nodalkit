#pragma once

/// @file text_shaper.h
/// @brief Abstract text measurement and rasterization interface.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/text/font.h>
#include <nk/text/shaped_text.h>
#include <string_view>

namespace nk {

/// Abstract interface for measuring and rasterizing text.
/// Platform backends provide implementations (CoreText on macOS,
/// FreeType on Linux).
class TextShaper {
public:
    virtual ~TextShaper() = default;

    TextShaper(const TextShaper&) = delete;
    TextShaper& operator=(const TextShaper&) = delete;

    /// Measure text without rasterizing. Returns dimensions.
    [[nodiscard]] virtual Size measure(std::string_view text, const FontDescriptor& font) const = 0;

    /// Shape and rasterize text into a bitmap.
    [[nodiscard]] virtual ShapedText
    shape(std::string_view text, const FontDescriptor& font, Color color) const = 0;

    /// Notify the shaper of the active device scale for future rasterization.
    virtual void set_scale_factor(float scale_factor);

    /// Provide the platform's configured default font family. When `monospace`
    /// is false this is the UI font (e.g. GNOME `font-name`); when true it is
    /// the configured monospace family. The shaper uses this to resolve the
    /// generic "System"/"" (and "monospace") family names instead of falling
    /// back to a hardcoded generic. Base implementation is a no-op.
    virtual void set_system_default_family(std::string_view family, bool monospace = false);

    /// Provide the platform's configured document font family. The generic
    /// "document" family resolves to this value when available.
    virtual void set_system_document_family(std::string_view family);

    /// Measure text with word wrapping within max_width.
    [[nodiscard]] virtual Size
    measure_wrapped(std::string_view text, const FontDescriptor& font, float max_width) const;

    /// Shape and rasterize text with word wrapping within max_width.
    [[nodiscard]] virtual ShapedText shape_wrapped(std::string_view text,
                                                   const FontDescriptor& font,
                                                   Color color,
                                                   float max_width) const;

    /// Create the platform-appropriate text shaper.
    [[nodiscard]] static std::unique_ptr<TextShaper> create();

protected:
    TextShaper() = default;
};

} // namespace nk
