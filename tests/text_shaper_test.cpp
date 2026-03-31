/// @file text_shaper_test.cpp
/// @brief Regression tests for platform text shaping output.

#include <catch2/catch_test_macros.hpp>
#include <nk/text/text_shaper.h>

#include <algorithm>
#include <cstddef>

namespace {

std::size_t alpha_sum_for_rows(
    nk::ShapedText const& shaped,
    int row_begin,
    int row_end) {
    auto const* bitmap = shaped.bitmap_data();
    int const width = shaped.bitmap_width();
    int const height = shaped.bitmap_height();
    if (bitmap == nullptr || width <= 0 || height <= 0) {
        return 0;
    }

    row_begin = std::clamp(row_begin, 0, height);
    row_end = std::clamp(row_end, 0, height);

    std::size_t alpha_sum = 0;
    for (int y = row_begin; y < row_end; ++y) {
        for (int x = 0; x < width; ++x) {
            auto const idx =
                static_cast<std::size_t>((y * width + x) * 4 + 3);
            alpha_sum += bitmap[idx];
        }
    }
    return alpha_sum;
}

} // namespace

TEST_CASE("Shaped text bitmaps stay top-heavy for upright glyphs", "[text]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 36.0F,
        .weight = nk::FontWeight::Bold,
    };

    auto shaped = shaper->shape("F", font, nk::Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(shaped.bitmap_data() != nullptr);
    REQUIRE(shaped.bitmap_width() > 0);
    REQUIRE(shaped.bitmap_height() > 0);

    int const height = shaped.bitmap_height();
    int const band = std::max(1, height / 3);

    auto const top_alpha = alpha_sum_for_rows(shaped, 0, band);
    auto const bottom_alpha = alpha_sum_for_rows(shaped, height - band, height);

    // An upright "F" should carry visibly more ink near the top than near
    // the bottom. This catches vertically inverted text bitmaps.
    REQUIRE(top_alpha > bottom_alpha);
}
