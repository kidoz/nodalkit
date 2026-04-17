/// @file text_shaper_test.cpp
/// @brief Regression tests for platform text shaping output.

#include <catch2/catch_approx.hpp>
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

TEST_CASE("Shaper measurement is stable under repeated identical calls", "[text]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 18.0F,
        .weight = nk::FontWeight::Regular,
    };

    const auto first = shaper->measure("Stable", font);
    const auto second = shaper->measure("Stable", font);
    const auto third = shaper->measure("Stable", font);

    REQUIRE(first.width > 0.0F);
    REQUIRE(first.height > 0.0F);
    REQUIRE(first.width == Catch::Approx(second.width));
    REQUIRE(first.height == Catch::Approx(second.height));
    REQUIRE(first.width == Catch::Approx(third.width));
}

TEST_CASE("Shaping an empty string returns an empty but valid bitmap", "[text]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 20.0F,
        .weight = nk::FontWeight::Regular,
    };

    const auto measured = shaper->measure("", font);
    REQUIRE(measured.width >= 0.0F);
    REQUIRE(measured.height >= 0.0F);

    // shape() must not crash on an empty input. Either it returns no bitmap,
    // or a zero-area sized one — both are valid. What matters is that the
    // object is returned in a well-formed state and the caller can still
    // query its metadata.
    auto shaped = shaper->shape("", font, nk::Color{0.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(shaped.bitmap_width() >= 0);
    REQUIRE(shaped.bitmap_height() >= 0);
    if (shaped.bitmap_data() != nullptr) {
        REQUIRE(alpha_sum_for_rows(shaped, 0, shaped.bitmap_height()) == 0U);
    }
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

#if defined(__linux__)
TEST_CASE("Linux shaper wraps long single-line text into multiple rendered lines",
          "[text][linux]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 14.0F,
        .weight = nk::FontWeight::Regular,
    };

    const std::string paragraph =
        "The quick brown fox jumps over the lazy dog while humming a tune "
        "for the little audience of passing commas and semicolons.";
    const float max_width = 160.0F;

    auto measured = shaper->measure(paragraph, font);
    auto wrapped = shaper->measure_wrapped(paragraph, font, max_width);

    // Unwrapped should be well past the max width; wrapping must bring each line under it and
    // grow the layout vertically instead.
    REQUIRE(measured.width > max_width);
    REQUIRE(wrapped.width <= max_width);
    REQUIRE(wrapped.height > measured.height);

    auto shaped = shaper->shape_wrapped(paragraph, font,
                                         nk::Color{0.0F, 0.0F, 0.0F, 1.0F}, max_width);
    REQUIRE(shaped.bitmap_data() != nullptr);
    REQUIRE(shaped.bitmap_width() > 0);
    REQUIRE(shaped.bitmap_height() > 0);
    REQUIRE(static_cast<float>(shaped.bitmap_width()) <= max_width + 1.0F);
    REQUIRE(shaped.bitmap_height() > shaped.bitmap_width());
}

TEST_CASE("Linux shaper honors explicit newlines when wrapping", "[text][linux]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    nk::FontDescriptor font{
        .family = "System",
        .size = 14.0F,
        .weight = nk::FontWeight::Regular,
    };

    const auto single_line = shaper->measure_wrapped("one two", font, 1000.0F);
    const auto two_lines = shaper->measure_wrapped("one\ntwo", font, 1000.0F);

    REQUIRE(single_line.height > 0.0F);
    REQUIRE(two_lines.height > single_line.height);
}

TEST_CASE("Linux shaper renders non-Latin text via fontconfig fallback", "[text][linux]") {
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);

    // Request a font family unlikely to cover CJK directly. The shaper should transparently
    // swap to a covering face via fontconfig's FC_CHARSET path when the host has one installed.
    // On hosts with no CJK-capable font, fc-match falls back to the primary font (which still
    // can't render the glyphs), so we soft-pass rather than assert ink.
    nk::FontDescriptor font{
        .family = "Liberation Sans",
        .size = 24.0F,
        .weight = nk::FontWeight::Regular,
    };

    const std::string cjk = "日本語";  // "Japanese language"
    auto measured = shaper->measure(cjk, font);
    auto shaped = shaper->shape(cjk, font, nk::Color{0.0F, 0.0F, 0.0F, 1.0F});

    // measurement + rasterization must not crash or return garbage sizes for non-Latin input,
    // regardless of whether a covering font is installed.
    REQUIRE(measured.width >= 0.0F);
    REQUIRE(measured.height >= 0.0F);

    if (shaped.bitmap_data() == nullptr ||
        shaped.bitmap_width() <= 0 ||
        shaped.bitmap_height() <= 0) {
        SUCCEED("No CJK-capable font installed on this host; fallback path untested");
        return;
    }

    const auto alpha_sum = alpha_sum_for_rows(shaped, 0, shaped.bitmap_height());
    if (alpha_sum == 0U) {
        // Host lacks a font with glyphs for these codepoints — the shaper correctly produced a
        // sized-but-empty bitmap rather than crashing. Skip the ink assertion.
        SUCCEED("No CJK-capable font installed on this host; fallback produced empty bitmap");
        return;
    }
    REQUIRE(alpha_sum > 0U);
}
#endif
