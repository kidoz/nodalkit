/// @file text_shaper_test.cpp
/// @brief Regression tests for platform text shaping output.

#include <catch2/catch_test_macros.hpp>
#include <nk/text/text_shaper.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <optional>

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
            auto const idx = static_cast<std::size_t>((y * width + x) * 4 + 3);
            alpha_sum += bitmap[idx];
        }
    }
    return alpha_sum;
}

std::optional<std::size_t> first_covered_pixel_index(nk::ShapedText const& shaped) {
    auto const* bitmap = shaped.bitmap_data();
    int const width = shaped.bitmap_width();
    int const height = shaped.bitmap_height();
    if (bitmap == nullptr || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    auto const pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
        if (bitmap[pixel_index * 4 + 3] != 0) {
            return pixel_index * 4;
        }
    }

    return std::nullopt;
}

bool channel_matches_tint(uint8_t actual, float component) {
    const auto expected = static_cast<int>(component * 255.0F);
    return std::abs(static_cast<int>(actual) - expected) <= 1;
}

[[maybe_unused]] std::optional<int> first_covered_row(nk::ShapedText const& shaped) {
    auto const* bitmap = shaped.bitmap_data();
    int const width = shaped.bitmap_width();
    int const height = shaped.bitmap_height();
    if (bitmap == nullptr || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto const idx = static_cast<std::size_t>((y * width + x) * 4 + 3);
            if (bitmap[idx] != 0) {
                return y;
            }
        }
    }

    return std::nullopt;
}

} // namespace

TEST_CASE("Shaped text bitmaps keep more ink in the upper half for top-heavy glyphs", "[text]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 36.0F,
        .weight = nk::FontWeight::Bold,
    };

    auto shaped = shaper->shape("P", font, nk::Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(shaped.bitmap_data() != nullptr);
    REQUIRE(shaped.bitmap_width() > 0);
    REQUIRE(shaped.bitmap_height() > 0);

    int const height = shaped.bitmap_height();
    int const top_band = std::max(1, height / 2);
    int const bottom_band = std::max(1, height / 4);

    auto const top_alpha = alpha_sum_for_rows(shaped, 0, top_band);
    auto const bottom_alpha = alpha_sum_for_rows(shaped, height - bottom_band, height);

    // An upright "P" should carry visibly more ink in the upper half than in
    // the bottom quarter. This still catches vertically inverted text
    // bitmaps, while being less sensitive to stem weight differences across
    // platform fonts.
    REQUIRE(top_alpha > bottom_alpha);
}

TEST_CASE("Shaped text baseline stays within the rasterized bitmap", "[text]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 24.0F,
        .weight = nk::FontWeight::Regular,
    };

    auto shaped = shaper->shape("Baseline", font, nk::Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(shaped.bitmap_data() != nullptr);
    REQUIRE(shaped.bitmap_width() > 0);
    REQUIRE(shaped.bitmap_height() > 0);
    REQUIRE(shaped.baseline() >= 0.0F);
    REQUIRE(shaped.baseline() <= static_cast<float>(shaped.bitmap_height()));
}

TEST_CASE("Shaped text preserves the requested tint for covered pixels", "[text]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 28.0F,
        .weight = nk::FontWeight::SemiBold,
    };

    nk::Color color{0.25F, 0.5F, 0.75F, 1.0F};
    auto shaped = shaper->shape("Tint", font, color);
    REQUIRE(shaped.bitmap_data() != nullptr);

    auto pixel_index = first_covered_pixel_index(shaped);
    REQUIRE(pixel_index.has_value());

    auto const* bitmap = shaped.bitmap_data();
    REQUIRE(channel_matches_tint(bitmap[*pixel_index + 0], color.r));
    REQUIRE(channel_matches_tint(bitmap[*pixel_index + 1], color.g));
    REQUIRE(channel_matches_tint(bitmap[*pixel_index + 2], color.b));
    REQUIRE(bitmap[*pixel_index + 3] > 0);
}

#if defined(_WIN32)
TEST_CASE("Windows text shaper renders Unicode fallback glyphs", "[text][windows]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 28.0F,
        .weight = nk::FontWeight::Regular,
    };

    auto shaped = shaper->shape("\xE6\xBC\xA2\xE5\xAD\x97",
                                font,
                                nk::Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(shaped.bitmap_data() != nullptr);
    REQUIRE(shaped.bitmap_width() > 0);
    REQUIRE(shaped.bitmap_height() > 0);
    REQUIRE(alpha_sum_for_rows(shaped, 0, shaped.bitmap_height()) > 0);
}

TEST_CASE("Windows text shaper preserves top bitmap padding for baseline-correct placement",
          "[text][windows]") {
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

    auto top_row = first_covered_row(shaped);
    REQUIRE(top_row.has_value());
    REQUIRE(*top_row > 0);
}

TEST_CASE("Windows text shaper treats font sizes as logical UI pixels", "[text][windows]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 12.0F,
        .weight = nk::FontWeight::Regular,
    };

    auto measured = shaper->measure("Hg", font);
    auto shaped = shaper->shape("Hg", font, nk::Color{0.0F, 0.0F, 0.0F, 1.0F});

    REQUIRE(measured.width > 0.0F);
    REQUIRE(measured.height > 0.0F);
    REQUIRE(shaped.bitmap_data() != nullptr);
    REQUIRE(shaped.bitmap_width() > 0);
    REQUIRE(shaped.bitmap_height() > 0);

    // NodalKit uses FontDescriptor::size as logical UI pixels. The previous
    // Windows implementation treated it as points, inflating 12 px text into
    // an ~16 px request and pushing layout/placement out of sync.
    REQUIRE(measured.height < 19.0F);
    REQUIRE(static_cast<float>(shaped.bitmap_height()) < 19.0F);
}
#endif
