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
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

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
// Unicode helpers
// ---------------------------------------------------------------------------

// Minimal UTF-8 decoder. Advances `cursor` past one codepoint and returns it (or U+FFFD on bad
// input). Suitable for iterating a UTF-8 string without pulling in a dedicated ICU/libunibreak
// dependency; the real shaping pass still goes through HarfBuzz which handles malformed runs.
uint32_t decode_utf8_codepoint(std::string_view text, std::size_t& cursor) {
    if (cursor >= text.size()) {
        return 0U;
    }
    const auto b0 = static_cast<unsigned char>(text[cursor]);
    if (b0 < 0x80U) {
        ++cursor;
        return b0;
    }
    unsigned int extra = 0;
    uint32_t cp = 0;
    if ((b0 & 0xE0U) == 0xC0U) { cp = b0 & 0x1FU; extra = 1; }
    else if ((b0 & 0xF0U) == 0xE0U) { cp = b0 & 0x0FU; extra = 2; }
    else if ((b0 & 0xF8U) == 0xF0U) { cp = b0 & 0x07U; extra = 3; }
    else { ++cursor; return 0xFFFDU; }

    if (cursor + 1U + extra > text.size()) {
        cursor = text.size();
        return 0xFFFDU;
    }
    for (unsigned int i = 1U; i <= extra; ++i) {
        const auto bi = static_cast<unsigned char>(text[cursor + i]);
        if ((bi & 0xC0U) != 0x80U) {
            ++cursor;
            return 0xFFFDU;
        }
        cp = (cp << 6U) | (bi & 0x3FU);
    }
    cursor += 1U + extra;
    return cp;
}

// Returns true when `face` has a glyph for every non-whitespace codepoint in `text`. Whitespace
// is excluded because any sensible font covers it, and renderers fall back to pen advance on
// missing spaces — so including spaces in the coverage check would force fallbacks for trivia.
bool face_covers_codepoints(FT_Face face, std::string_view text) {
    if (face == nullptr) {
        return false;
    }
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const uint32_t cp = decode_utf8_codepoint(text, cursor);
        if (cp == 0U || cp == 0xFFFDU) {
            continue;
        }
        if (cp == 0x20U || cp == 0x09U || cp == 0x0AU || cp == 0x0DU) {
            continue;
        }
        if (FT_Get_Char_Index(face, cp) == 0U) {
            return false;
        }
    }
    return true;
}

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

    // Resolves a face that can render `text` given `desc` as the preferred style. If the primary
    // family's face covers every non-whitespace codepoint in the text, returns that face.
    // Otherwise queries fontconfig for a face matching the text's charset with the same
    // weight/slant as `desc`, so non-Latin strings render with real glyphs instead of .notdef
    // boxes. Runs through the face cache for both candidates.
    FaceCacheEntry* resolve_face_for_text(FontDescriptor const& desc, std::string_view text) {
        auto const& primary_path = resolve_font_path(desc);
        if (primary_path.empty()) {
            return nullptr;
        }
        auto* primary = get_face(primary_path, desc);
        if (primary == nullptr || text.empty() || face_covers_codepoints(primary->face, text)) {
            return primary;
        }

        std::string const& fallback_path = resolve_fallback_font_path(desc, text);
        if (fallback_path.empty() || fallback_path == primary_path) {
            return primary;
        }
        auto* fallback = get_face(fallback_path, desc);
        if (fallback == nullptr || !face_covers_codepoints(fallback->face, text)) {
            return primary;
        }
        return fallback;
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

    // Look up a font that covers a specific codepoint set. Keyed by the (weight, style) pair and
    // the sorted set of covered codepoints so repeated calls for the same text don't hammer
    // fontconfig. Falls back to the primary font path when fontconfig can't find a cover.
    std::string const& resolve_fallback_font_path(FontDescriptor const& desc,
                                                   std::string_view text) {
        FallbackKey key{desc.weight, desc.style, codepoint_fingerprint(text)};
        auto it = fallback_path_cache.find(key);
        if (it != fallback_path_cache.end()) {
            return it->second;
        }
        std::string path = lookup_fallback_font_path(desc, text);
        auto [inserted, _] = fallback_path_cache.emplace(std::move(key), std::move(path));
        return inserted->second;
    }

private:
    // Compact, order-independent fingerprint of a text's codepoint coverage: sorted unique list
    // of codepoints joined with commas. Good enough for caching; collisions across texts with
    // the same coverage share the same fallback which is what we want anyway.
    static std::string codepoint_fingerprint(std::string_view text) {
        std::vector<uint32_t> codepoints;
        codepoints.reserve(text.size());
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            const uint32_t cp = decode_utf8_codepoint(text, cursor);
            if (cp == 0U || cp == 0xFFFDU || cp < 0x80U) {
                continue;  // ASCII is always covered; ignore to collapse common cases
            }
            codepoints.push_back(cp);
        }
        std::sort(codepoints.begin(), codepoints.end());
        codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
        std::string out;
        out.reserve(codepoints.size() * 4);
        for (const auto cp : codepoints) {
            out.append(std::to_string(cp));
            out.push_back(',');
        }
        return out;
    }

    struct FallbackKey {
        FontWeight weight = FontWeight::Regular;
        FontStyle style = FontStyle::Normal;
        std::string fingerprint;
        bool operator==(const FallbackKey&) const = default;
    };

    struct FallbackKeyHash {
        std::size_t operator()(const FallbackKey& key) const noexcept {
            std::size_t h = std::hash<std::string>{}(key.fingerprint);
            h ^= std::hash<uint16_t>{}(static_cast<uint16_t>(key.weight)) + 0x9e3779b9 + (h << 6) +
                 (h >> 2);
            h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(key.style)) + 0x9e3779b9 + (h << 6) +
                 (h >> 2);
            return h;
        }
    };

    std::unordered_map<FallbackKey, std::string, FallbackKeyHash> fallback_path_cache;

    static std::string lookup_fallback_font_path(FontDescriptor const& desc,
                                                  std::string_view text) {
        FcCharSet* charset = FcCharSetCreate();
        if (charset == nullptr) {
            return {};
        }
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            const uint32_t cp = decode_utf8_codepoint(text, cursor);
            if (cp == 0U || cp == 0xFFFDU || cp == 0x20U || cp == 0x09U || cp == 0x0AU ||
                cp == 0x0DU) {
                continue;
            }
            FcCharSetAddChar(charset, cp);
        }

        FcPattern* pattern = FcPatternCreate();
        if (pattern == nullptr) {
            FcCharSetDestroy(charset);
            return {};
        }
        FcPatternAddCharSet(pattern, FC_CHARSET, charset);
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
        if (match != nullptr) {
            FcChar8* file = nullptr;
            if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
                path = reinterpret_cast<char const*>(file);
            }
            FcPatternDestroy(match);
        }

        FcPatternDestroy(pattern);
        FcCharSetDestroy(charset);
        return path;
    }

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

    auto* entry = impl_->resolve_face_for_text(font, text);
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

    auto* entry = impl_->resolve_face_for_text(font, text);
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

// ---------------------------------------------------------------------------
// Word wrapping
// ---------------------------------------------------------------------------

namespace {

// Splits `text` into tokens at ASCII whitespace boundaries while preserving the whitespace with
// the preceding token. Each token therefore carries its trailing space(s), which keeps inter-word
// measurement faithful when we reassemble lines. Newlines force hard breaks: the token preceding
// a '\n' ends the current line, and the newline itself is consumed (not included).
std::vector<std::string> tokenize_for_wrap(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_whitespace = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\n') {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            tokens.emplace_back("\n");
            in_whitespace = false;
            continue;
        }
        const bool is_ws = ch == ' ' || ch == '\t';
        if (is_ws) {
            if (!in_whitespace && !current.empty()) {
                // Whitespace attaches to the preceding token, not a new one.
            }
            current.push_back(ch);
            in_whitespace = true;
        } else {
            if (in_whitespace) {
                tokens.push_back(std::move(current));
                current.clear();
                in_whitespace = false;
            }
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

} // namespace

Size FreeTypeShaper::measure_wrapped(std::string_view text,
                                     FontDescriptor const& font,
                                     float max_width) const {
    if (text.empty() || max_width <= 0.0F) {
        return measure(text, font);
    }

    const auto tokens = tokenize_for_wrap(text);
    std::string line;
    float max_line_width = 0.0F;
    float total_height = 0.0F;
    bool has_line_content = false;
    auto flush_line = [&] {
        if (line.empty() && !has_line_content) {
            const auto empty_line_size = measure(" ", font);
            total_height += empty_line_size.height;
            return;
        }
        // Trim the trailing whitespace so the line width reflects visible content rather than the
        // soft-break that let us flush here.
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
        const auto line_size = measure(trimmed.empty() ? std::string(" ") : trimmed, font);
        max_line_width = std::max(max_line_width, line_size.width);
        total_height += line_size.height;
        line.clear();
        has_line_content = false;
    };

    for (const auto& token : tokens) {
        if (token == "\n") {
            flush_line();
            continue;
        }
        const std::string candidate = line + token;
        const auto candidate_size = measure(candidate, font);
        if (candidate_size.width <= max_width || !has_line_content) {
            line = candidate;
            has_line_content = true;
        } else {
            flush_line();
            line = token;
            has_line_content = true;
        }
    }
    flush_line();

    return {max_line_width, total_height};
}

ShapedText FreeTypeShaper::shape_wrapped(std::string_view text,
                                         FontDescriptor const& font,
                                         Color color,
                                         float max_width) const {
    if (text.empty() || max_width <= 0.0F) {
        return shape(text, font, color);
    }

    // Build the same line decomposition as measure_wrapped so layout and rasterization agree.
    const auto tokens = tokenize_for_wrap(text);
    std::vector<std::string> lines;
    std::string line;
    bool has_line_content = false;
    auto flush_line = [&] {
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
        lines.push_back(std::move(trimmed));
        line.clear();
        has_line_content = false;
    };
    for (const auto& token : tokens) {
        if (token == "\n") {
            flush_line();
            continue;
        }
        const std::string candidate = line + token;
        const auto candidate_size = measure(candidate, font);
        if (candidate_size.width <= max_width || !has_line_content) {
            line = candidate;
            has_line_content = true;
        } else {
            flush_line();
            line = token;
            has_line_content = true;
        }
    }
    if (!line.empty() || has_line_content) {
        flush_line();
    }

    // Shape each line individually, then composite into one stacked bitmap. The per-line height
    // and baseline come from the primary face's metrics via the first shape() result; subsequent
    // lines are stacked at full ascent+descent offsets. This is a greedy "all lines share the same
    // metrics" approach — acceptable for our label widget but not general-purpose typesetting.
    std::vector<ShapedText> shaped_lines;
    shaped_lines.reserve(lines.size());
    float max_width_seen = 0.0F;
    float total_height = 0.0F;
    float first_baseline = 0.0F;
    for (const auto& line_text : lines) {
        ShapedText shaped = shape(line_text.empty() ? std::string(" ") : line_text, font, color);
        const auto size = shaped.text_size();
        max_width_seen = std::max(max_width_seen, size.width);
        if (shaped_lines.empty()) {
            first_baseline = shaped.baseline();
        }
        total_height += size.height;
        shaped_lines.push_back(std::move(shaped));
    }

    ShapedText result;
    result.set_text_size({max_width_seen, total_height});
    result.set_baseline(first_baseline);

    const auto total_width_px = static_cast<int>(std::ceil(max_width_seen));
    const auto total_height_px = static_cast<int>(std::ceil(total_height));
    if (total_width_px <= 0 || total_height_px <= 0) {
        return result;
    }

    std::vector<uint8_t> bitmap(
        static_cast<std::size_t>(total_width_px) * static_cast<std::size_t>(total_height_px) * 4U,
        0U);
    int cursor_y = 0;
    for (const auto& shaped : shaped_lines) {
        const auto* src = shaped.bitmap_data();
        const int src_w = shaped.bitmap_width();
        const int src_h = shaped.bitmap_height();
        const int line_advance = static_cast<int>(std::ceil(shaped.text_size().height));
        if (src != nullptr && src_w > 0 && src_h > 0) {
            const int copy_w = std::min(src_w, total_width_px);
            const int copy_h = std::min(src_h, total_height_px - cursor_y);
            for (int y = 0; y < copy_h; ++y) {
                const auto src_row = static_cast<std::size_t>(y * src_w) * 4U;
                const auto dst_row =
                    (static_cast<std::size_t>((cursor_y + y) * total_width_px)) * 4U;
                std::memcpy(bitmap.data() + dst_row, src + src_row,
                            static_cast<std::size_t>(copy_w) * 4U);
            }
        }
        cursor_y += line_advance;
    }

    result.set_bitmap(std::move(bitmap), total_width_px, total_height_px);
    return result;
}

} // namespace nk
