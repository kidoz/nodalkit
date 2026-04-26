#pragma once

/// @file shaped_text.h
/// @brief Measured and rasterized text output.

#include <cstdint>
#include <memory>
#include <nk/foundation/types.h>
#include <vector>

namespace nk {

/// Result of text shaping: a measured glyph run with optional rasterized
/// pixel data for software rendering.
class ShapedText {
public:
    ShapedText();
    ~ShapedText();

    ShapedText(ShapedText&&) noexcept;
    ShapedText& operator=(ShapedText&&) noexcept;

    ShapedText(const ShapedText&) = delete;
    ShapedText& operator=(const ShapedText&) = delete;

    /// Logical size of the text (width x height in logical pixels).
    [[nodiscard]] Size text_size() const;
    void set_text_size(Size size);

    /// Baseline offset from top.
    [[nodiscard]] float baseline() const;
    void set_baseline(float baseline);

    /// Rasterized bitmap (RGBA8, row-major). May be empty if not rasterized.
    [[nodiscard]] const uint8_t* bitmap_data() const;
    [[nodiscard]] int bitmap_width() const;
    [[nodiscard]] int bitmap_height() const;

    /// Set the rasterized bitmap. Takes ownership of the data.
    void set_bitmap(std::vector<uint8_t> data, int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
