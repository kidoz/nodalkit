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
#include <list>
#include <unordered_map>

namespace nk {

namespace {

// ---------------------------------------------------------------------------
// Font path cache — avoids repeated fontconfig matching.
// ---------------------------------------------------------------------------

struct FontPathKey {
    std::string family;
    FontWeight weight = FontWeight::Regular;
    FontStyle style = FontStyle::Normal;

    bool operator==(const FontPathKey&) const = default;
};

struct FontPathKeyHash {
    std::size_t operator()(const FontPathKey& key) const noexcept {
        std::size_t h = std::hash<std::string>{}(key.family);
        h ^= std::hash<uint16_t>{}(static_cast<uint16_t>(key.weight)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(key.style)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// ---------------------------------------------------------------------------
// Face cache — keeps FT_Face objects alive across calls.
// ---------------------------------------------------------------------------

struct FaceKey {
    std::string path;
    unsigned int pixel_size = 0;

    bool operator==(const FaceKey&) const = default;
};

struct FaceKeyHash {
    std::size_t operator()(const FaceKey& key) const noexcept {
        std::size_t h = std::hash<std::string>{}(key.path);
        h ^= std::hash<unsigned int>{}(key.pixel_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct FaceCacheEntry {
    FT_Face face = nullptr;
    hb_font_t* hb_font = nullptr;
};

constexpr std::size_t kFaceCacheMaxEntries = 8;

// ---------------------------------------------------------------------------
// Measurement cache — avoids re-shaping text that hasn't changed.
// ---------------------------------------------------------------------------

struct MeasureCacheKey {
    std::string text;
    std::string family;
    float size = 0.0F;
    FontWeight weight = FontWeight::Regular;
    FontStyle style = FontStyle::Normal;

    bool operator==(const MeasureCacheKey&) const = default;
};

struct MeasureCacheKeyHash {
    std::size_t operator()(const MeasureCacheKey& key) const noexcept {
        std::size_t h = std::hash<std::string>{}(key.text);
        h ^= std::hash<std::string>{}(key.family) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(key.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint16_t>{}(static_cast<uint16_t>(key.weight)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(key.style)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

constexpr std::size_t kMeasureCacheMaxEntries = 512;

// ---------------------------------------------------------------------------
// Text run helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// FreeTypeShaper::Impl — holds all caches.
// ---------------------------------------------------------------------------

struct FreeTypeShaper::Impl {
    FT_Library ft_library = nullptr;

    // Font path cache: (family, weight, style) → file path.
    std::unordered_map<FontPathKey, std::string, FontPathKeyHash> font_path_cache;

    // Face cache with LRU eviction: (path, pixel_size) → (FT_Face, hb_font).
    std::list<std::pair<FaceKey, FaceCacheEntry>> face_lru;
    std::unordered_map<FaceKey, decltype(face_lru)::iterator, FaceKeyHash> face_map;

    // Measurement cache with LRU eviction.
    std::list<std::pair<MeasureCacheKey, Size>> measure_lru;
    std::unordered_map<MeasureCacheKey, decltype(measure_lru)::iterator, MeasureCacheKeyHash> measure_map;

    ~Impl() {
        for (auto& entry : face_lru) {
            if (entry.second.hb_font != nullptr) {
                hb_font_destroy(entry.second.hb_font);
            }
            if (entry.second.face != nullptr) {
                FT_Done_Face(entry.second.face);
            }
        }
        if (ft_library != nullptr) {
            FT_Done_FreeType(ft_library);
        }
    }

    std::string const& resolve_font_path(FontDescriptor const& desc) {
        FontPathKey key{
            desc.family.empty() || desc.family == "System" ? "sans-serif" : desc.family,
            desc.weight,
            desc.style,
        };

        auto it = font_path_cache.find(key);
        if (it != font_path_cache.end()) {
            return it->second;
        }

        std::string path = lookup_font_path(key);
        auto [inserted, _] = font_path_cache.emplace(std::move(key), std::move(path));
        return inserted->second;
    }

    FaceCacheEntry* get_face(std::string const& path, FontDescriptor const& font) {
        auto const px = static_cast<unsigned int>(
            std::round(static_cast<double>(font.size) * 96.0 / 72.0));
        FaceKey key{path, px};

        auto it = face_map.find(key);
        if (it != face_map.end()) {
            // Move to front of LRU.
            face_lru.splice(face_lru.begin(), face_lru, it->second);
            return &it->second->second;
        }

        // Create new face.
        FT_Face face = nullptr;
        if (FT_New_Face(ft_library, path.c_str(), 0, &face) != 0) {
            return nullptr;
        }
        FT_Set_Pixel_Sizes(face, 0, px);

        hb_font_t* hb_font = hb_ft_font_create_referenced(face);
        if (hb_font == nullptr) {
            FT_Done_Face(face);
            return nullptr;
        }

        // Evict oldest if at capacity.
        if (face_lru.size() >= kFaceCacheMaxEntries) {
            auto& back = face_lru.back();
            if (back.second.hb_font != nullptr) {
                hb_font_destroy(back.second.hb_font);
            }
            FT_Done_Face(back.second.face);
            face_map.erase(back.first);
            face_lru.pop_back();
        }

        face_lru.emplace_front(std::move(key), FaceCacheEntry{face, hb_font});
        face_map[face_lru.front().first] = face_lru.begin();
        return &face_lru.front().second;
    }

    Size* find_cached_measure(std::string_view text, FontDescriptor const& font) {
        MeasureCacheKey key{
            std::string(text),
            font.family,
            font.size,
            font.weight,
            font.style,
        };
        auto it = measure_map.find(key);
        if (it != measure_map.end()) {
            measure_lru.splice(measure_lru.begin(), measure_lru, it->second);
            return &it->second->second;
        }
        return nullptr;
    }

    void insert_measure(std::string_view text, FontDescriptor const& font, Size size) {
        MeasureCacheKey key{
            std::string(text),
            font.family,
            font.size,
            font.weight,
            font.style,
        };
        if (measure_lru.size() >= kMeasureCacheMaxEntries) {
            measure_map.erase(measure_lru.back().first);
            measure_lru.pop_back();
        }
        measure_lru.emplace_front(std::move(key), size);
        measure_map[measure_lru.front().first] = measure_lru.begin();
    }

private:
    static std::string lookup_font_path(FontPathKey const& key) {
        FcPattern* pattern = FcPatternCreate();
        if (pattern == nullptr) {
            return {};
        }

        FcPatternAddString(pattern, FC_FAMILY,
                           reinterpret_cast<FcChar8 const*>(key.family.c_str()));

        int fc_weight = FC_WEIGHT_REGULAR;
        switch (key.weight) {
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
        switch (key.style) {
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
        if (match != nullptr) {
            FcChar8* file = nullptr;
            if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch &&
                file != nullptr) {
                path = reinterpret_cast<char const*>(file);
            }
            FcPatternDestroy(match);
        }

        FcPatternDestroy(pattern);
        return path;
    }
};

// ---------------------------------------------------------------------------
// FreeTypeShaper
// ---------------------------------------------------------------------------

FreeTypeShaper::FreeTypeShaper() : impl_(std::make_unique<Impl>()) {
    if (FT_Init_FreeType(&impl_->ft_library) != 0) {
        NK_LOG_ERROR("FreeType", "Failed to initialize FreeType library");
        impl_->ft_library = nullptr;
    }
}

FreeTypeShaper::~FreeTypeShaper() = default;

Size FreeTypeShaper::measure(std::string_view text,
                             FontDescriptor const& font) const {
    if (impl_->ft_library == nullptr || text.empty()) {
        return {0, 0};
    }

    // Check measurement cache first.
    if (auto* cached = impl_->find_cached_measure(text, font)) {
        return *cached;
    }

    auto const& path = impl_->resolve_font_path(font);
    if (path.empty()) {
        return {0, 0};
    }

    auto* entry = impl_->get_face(path, font);
    if (entry == nullptr) {
        return {0, 0};
    }

    hb_buffer_t* hb_buffer = hb_buffer_create();
    if (hb_buffer == nullptr) {
        return {0, 0};
    }

    hb_buffer_add_utf8(hb_buffer, text.data(),
                       static_cast<int>(text.size()),
                       0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(hb_buffer);
    hb_shape(entry->hb_font, hb_buffer, nullptr, 0);

    unsigned int glyph_count = 0;
    auto* glyph_infos = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
    auto* glyph_positions = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);

    Size result{0, 0};
    if (glyph_infos != nullptr && glyph_positions != nullptr) {
        auto const bounds =
            compute_text_run_bounds(entry->face, glyph_infos, glyph_positions, glyph_count);
        result = {static_cast<float>(bounds.width), static_cast<float>(bounds.height)};
    }

    hb_buffer_destroy(hb_buffer);
    impl_->insert_measure(text, font, result);
    return result;
}

ShapedText FreeTypeShaper::shape(std::string_view text,
                                 FontDescriptor const& font,
                                 Color color) const {
    ShapedText result;

    if (impl_->ft_library == nullptr || text.empty()) {
        return result;
    }

    auto const& path = impl_->resolve_font_path(font);
    if (path.empty()) {
        NK_LOG_WARN("FreeType", "Could not resolve font path");
        return result;
    }

    auto* entry = impl_->get_face(path, font);
    if (entry == nullptr) {
        NK_LOG_WARN("FreeType", "Failed to load font face");
        return result;
    }

    hb_buffer_t* hb_buffer = hb_buffer_create();
    if (hb_buffer == nullptr) {
        return result;
    }

    hb_buffer_add_utf8(hb_buffer, text.data(),
                       static_cast<int>(text.size()),
                       0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(hb_buffer);
    hb_shape(entry->hb_font, hb_buffer, nullptr, 0);

    unsigned int glyph_count = 0;
    auto* glyph_infos = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
    auto* glyph_positions = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);

    if (glyph_infos == nullptr || glyph_positions == nullptr) {
        hb_buffer_destroy(hb_buffer);
        return result;
    }

    auto const bounds =
        compute_text_run_bounds(entry->face, glyph_infos, glyph_positions, glyph_count);
    result.set_text_size(
        {static_cast<float>(bounds.width), static_cast<float>(bounds.height)});
    result.set_baseline(static_cast<float>(bounds.baseline));

    if (bounds.width <= 0 || bounds.height <= 0) {
        hb_buffer_destroy(hb_buffer);
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
                entry->face,
                glyph_infos[i].codepoint,
                FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            pen_x += glyph_positions[i].x_advance;
            continue;
        }

        auto const* glyph = entry->face->glyph;
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
    result.set_bitmap(std::move(bitmap), bounds.width, bounds.height);
    return result;
}

} // namespace nk
