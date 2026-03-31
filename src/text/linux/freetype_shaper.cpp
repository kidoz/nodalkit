/// @file freetype_shaper.cpp
/// @brief FreeType + fontconfig text shaper implementation.

#include "freetype_shaper.h"

#include <nk/foundation/logging.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <harfbuzz/hb-ft.h>

#include <algorithm>
#include <cmath>

namespace nk {

namespace {

struct TextRunBounds {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int baseline = 0;
};

int to_pixel_floor(hb_position_t value) {
    return static_cast<int>(
        std::floor(static_cast<double>(value) / 64.0));
}

int to_pixel_ceil(hb_position_t value) {
    return static_cast<int>(
        std::ceil(static_cast<double>(value) / 64.0));
}

FT_Face create_face(
    FT_Library library,
    std::string const& path,
    FontDescriptor const& font) {
    FT_Face face = nullptr;
    if (FT_New_Face(library, path.c_str(), 0, &face) != 0) {
        return nullptr;
    }

    auto const px = static_cast<FT_UInt>(
        std::round(static_cast<double>(font.size) * 96.0 / 72.0));
    FT_Set_Pixel_Sizes(face, 0, px);
    return face;
}

bool shape_run(
    FT_Face face,
    std::string_view text,
    hb_font_t*& hb_font,
    hb_buffer_t*& hb_buffer,
    hb_glyph_info_t*& glyph_infos,
    hb_glyph_position_t*& glyph_positions,
    unsigned int& glyph_count) {
    hb_font = hb_ft_font_create_referenced(face);
    hb_buffer = hb_buffer_create();
    if (hb_font == nullptr || hb_buffer == nullptr) {
        return false;
    }

    hb_buffer_add_utf8(
        hb_buffer,
        text.data(),
        static_cast<int>(text.size()),
        0,
        static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(hb_buffer);
    hb_shape(hb_font, hb_buffer, nullptr, 0);

    glyph_infos = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
    glyph_positions = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);
    return glyph_infos != nullptr && glyph_positions != nullptr;
}

TextRunBounds compute_text_run_bounds(
    FT_Face face,
    hb_glyph_info_t const* glyph_infos,
    hb_glyph_position_t const* glyph_positions,
    unsigned int glyph_count) {
    TextRunBounds bounds;

    int const ascender = static_cast<int>(face->size->metrics.ascender >> 6);
    int const descender =
        static_cast<int>(-(face->size->metrics.descender >> 6));
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = ascender + descender;
    hb_position_t pen_x = 0;

    for (unsigned int i = 0; i < glyph_count; ++i) {
        if (FT_Load_Glyph(
                face,
                glyph_infos[i].codepoint,
                FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            pen_x += glyph_positions[i].x_advance;
            continue;
        }

        auto const* glyph = face->glyph;
        int const gx =
            to_pixel_floor(pen_x + glyph_positions[i].x_offset)
            + glyph->bitmap_left;
        int const gy =
            ascender - to_pixel_floor(glyph_positions[i].y_offset)
            - glyph->bitmap_top;

        if (glyph->bitmap.width > 0 && glyph->bitmap.rows > 0) {
            min_x = std::min(min_x, gx);
            max_x = std::max(max_x, gx + static_cast<int>(glyph->bitmap.width));
            min_y = std::min(min_y, gy);
            max_y = std::max(max_y, gy + static_cast<int>(glyph->bitmap.rows));
        }

        pen_x += glyph_positions[i].x_advance;
    }

    int const advance_width = std::max(0, to_pixel_ceil(pen_x));
    int const right = std::max(max_x, advance_width);
    int const bottom = std::max(max_y, ascender + descender);
    bounds.left = std::min(min_x, 0);
    bounds.top = std::min(min_y, 0);
    bounds.width = std::max(0, right - bounds.left);
    bounds.height = std::max(0, bottom - bounds.top);
    bounds.baseline = ascender - bounds.top;
    return bounds;
}

void blend_alpha_mask(
    std::vector<uint8_t>& bitmap,
    int width,
    int height,
    int origin_x,
    int origin_y,
    FT_Bitmap const& source,
    uint8_t cr,
    uint8_t cg,
    uint8_t cb) {
    auto const stride = static_cast<std::size_t>(width) * 4;

    for (unsigned int row = 0; row < source.rows; ++row) {
        for (unsigned int col = 0; col < source.width; ++col) {
            int const dest_x = origin_x + static_cast<int>(col);
            int const dest_y = origin_y + static_cast<int>(row);
            if (dest_x < 0 || dest_x >= width
                || dest_y < 0 || dest_y >= height) {
                continue;
            }

            auto const alpha =
                source.buffer[row * static_cast<unsigned int>(source.pitch) + col];
            if (alpha == 0) {
                continue;
            }

            auto const idx = static_cast<std::size_t>(dest_y) * stride
                           + static_cast<std::size_t>(dest_x) * 4;
            float const src_a = static_cast<float>(alpha) / 255.0F;
            float const dst_a =
                static_cast<float>(bitmap[idx + 3]) / 255.0F;
            float const out_a = src_a + dst_a * (1.0F - src_a);
            bitmap[idx + 0] = cr;
            bitmap[idx + 1] = cg;
            bitmap[idx + 2] = cb;
            bitmap[idx + 3] = static_cast<uint8_t>(
                std::clamp(out_a * 255.0F, 0.0F, 255.0F));
        }
    }
}

} // namespace

FreeTypeShaper::FreeTypeShaper() {
    if (FT_Init_FreeType(&ft_library_) != 0) {
        NK_LOG_ERROR("FreeType", "Failed to initialize FreeType library");
        ft_library_ = nullptr;
    }
}

FreeTypeShaper::~FreeTypeShaper() {
    if (ft_library_) {
        FT_Done_FreeType(ft_library_);
    }
}

std::string FreeTypeShaper::resolve_font_path(
    FontDescriptor const& desc) const {

    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        return {};
    }

    char const* family = desc.family.c_str();
    if (desc.family.empty() || desc.family == "System") {
        family = "sans-serif";
    }
    FcPatternAddString(pattern, FC_FAMILY,
                       reinterpret_cast<FcChar8 const*>(family));
    FcPatternAddDouble(pattern, FC_SIZE, static_cast<double>(desc.size));

    // Map nk::FontWeight → fontconfig weight constant.
    int fc_weight = FC_WEIGHT_REGULAR;
    switch (desc.weight) {
    case FontWeight::Thin:      fc_weight = FC_WEIGHT_THIN; break;
    case FontWeight::Light:     fc_weight = FC_WEIGHT_LIGHT; break;
    case FontWeight::Regular:   fc_weight = FC_WEIGHT_REGULAR; break;
    case FontWeight::Medium:    fc_weight = FC_WEIGHT_MEDIUM; break;
    case FontWeight::SemiBold:  fc_weight = FC_WEIGHT_SEMIBOLD; break;
    case FontWeight::Bold:      fc_weight = FC_WEIGHT_BOLD; break;
    case FontWeight::ExtraBold: fc_weight = FC_WEIGHT_EXTRABOLD; break;
    case FontWeight::Black:     fc_weight = FC_WEIGHT_BLACK; break;
    }
    FcPatternAddInteger(pattern, FC_WEIGHT, fc_weight);

    int fc_slant = FC_SLANT_ROMAN;
    switch (desc.style) {
    case FontStyle::Normal:  fc_slant = FC_SLANT_ROMAN; break;
    case FontStyle::Italic:  fc_slant = FC_SLANT_ITALIC; break;
    case FontStyle::Oblique: fc_slant = FC_SLANT_OBLIQUE; break;
    }
    FcPatternAddInteger(pattern, FC_SLANT, fc_slant);

    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);

    std::string path;
    if (match) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch &&
            file) {
            path = reinterpret_cast<char const*>(file);
        }
        FcPatternDestroy(match);
    }

    FcPatternDestroy(pattern);
    return path;
}

Size FreeTypeShaper::measure(std::string_view text,
                             FontDescriptor const& font) const {
    if (!ft_library_ || text.empty()) {
        return {0, 0};
    }

    std::string const path = resolve_font_path(font);
    if (path.empty()) {
        return {0, 0};
    }

    FT_Face face = create_face(ft_library_, path, font);
    if (face == nullptr) {
        return {0, 0};
    }

    hb_font_t* hb_font = nullptr;
    hb_buffer_t* hb_buffer = nullptr;
    hb_glyph_info_t* glyph_infos = nullptr;
    hb_glyph_position_t* glyph_positions = nullptr;
    unsigned int glyph_count = 0;
    if (!shape_run(
            face,
            text,
            hb_font,
            hb_buffer,
            glyph_infos,
            glyph_positions,
            glyph_count)) {
        if (hb_buffer != nullptr) {
            hb_buffer_destroy(hb_buffer);
        }
        if (hb_font != nullptr) {
            hb_font_destroy(hb_font);
        }
        FT_Done_Face(face);
        return {0, 0};
    }

    auto const bounds =
        compute_text_run_bounds(face, glyph_infos, glyph_positions, glyph_count);
    hb_buffer_destroy(hb_buffer);
    hb_font_destroy(hb_font);
    FT_Done_Face(face);
    return {static_cast<float>(bounds.width), static_cast<float>(bounds.height)};
}

ShapedText FreeTypeShaper::shape(std::string_view text,
                                 FontDescriptor const& font,
                                 Color color) const {
    ShapedText result;

    if (!ft_library_ || text.empty()) {
        return result;
    }

    std::string const path = resolve_font_path(font);
    if (path.empty()) {
        NK_LOG_WARN("FreeType", "Could not resolve font path");
        return result;
    }

    FT_Face face = create_face(ft_library_, path, font);
    if (face == nullptr) {
        NK_LOG_WARN("FreeType", "Failed to load font face");
        return result;
    }

    hb_font_t* hb_font = nullptr;
    hb_buffer_t* hb_buffer = nullptr;
    hb_glyph_info_t* glyph_infos = nullptr;
    hb_glyph_position_t* glyph_positions = nullptr;
    unsigned int glyph_count = 0;
    if (!shape_run(
            face,
            text,
            hb_font,
            hb_buffer,
            glyph_infos,
            glyph_positions,
            glyph_count)) {
        if (hb_buffer != nullptr) {
            hb_buffer_destroy(hb_buffer);
        }
        if (hb_font != nullptr) {
            hb_font_destroy(hb_font);
        }
        FT_Done_Face(face);
        return result;
    }

    auto const bounds =
        compute_text_run_bounds(face, glyph_infos, glyph_positions, glyph_count);
    result.set_text_size(
        {static_cast<float>(bounds.width), static_cast<float>(bounds.height)});
    result.set_baseline(static_cast<float>(bounds.baseline));

    if (bounds.width <= 0 || bounds.height <= 0) {
        hb_buffer_destroy(hb_buffer);
        hb_font_destroy(hb_font);
        FT_Done_Face(face);
        return result;
    }

    std::vector<uint8_t> bitmap(
        static_cast<std::size_t>(bounds.width * bounds.height * 4), 0);

    auto const cr = static_cast<uint8_t>(
        std::clamp(color.r, 0.0F, 1.0F) * 255.0F + 0.5F);
    auto const cg = static_cast<uint8_t>(
        std::clamp(color.g, 0.0F, 1.0F) * 255.0F + 0.5F);
    auto const cb = static_cast<uint8_t>(
        std::clamp(color.b, 0.0F, 1.0F) * 255.0F + 0.5F);

    hb_position_t pen_x = 0;
    for (unsigned int i = 0; i < glyph_count; ++i) {
        if (FT_Load_Glyph(
                face,
                glyph_infos[i].codepoint,
                FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            pen_x += glyph_positions[i].x_advance;
            continue;
        }

        auto const* glyph = face->glyph;
        int const gx =
            to_pixel_floor(pen_x + glyph_positions[i].x_offset)
            + glyph->bitmap_left - bounds.left;
        int const gy =
            bounds.baseline
            - to_pixel_floor(glyph_positions[i].y_offset)
            - glyph->bitmap_top;

        blend_alpha_mask(
            bitmap,
            bounds.width,
            bounds.height,
            gx,
            gy,
            glyph->bitmap,
            cr,
            cg,
            cb);

        pen_x += glyph_positions[i].x_advance;
    }

    hb_buffer_destroy(hb_buffer);
    hb_font_destroy(hb_font);
    FT_Done_Face(face);
    result.set_bitmap(std::move(bitmap), bounds.width, bounds.height);
    return result;
}

} // namespace nk
