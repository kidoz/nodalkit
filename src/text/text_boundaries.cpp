#include "text_boundaries.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace nk::detail {

std::optional<Utf8Unit> decode_utf8_unit(std::string_view text, std::size_t index) {
    if (index >= text.size()) {
        return std::nullopt;
    }

    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        return Utf8Unit{index, index + 1, static_cast<char32_t>(lead)};
    }

    auto continuation = [&](std::size_t offset) -> std::optional<unsigned char> {
        const auto next_index = index + offset;
        if (next_index >= text.size()) {
            return std::nullopt;
        }
        const auto byte = static_cast<unsigned char>(text[next_index]);
        if ((byte & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        return byte;
    };

    if ((lead & 0xE0U) == 0xC0U) {
        const auto b1 = continuation(1);
        if (!b1.has_value()) {
            return Utf8Unit{index, index + 1, 0xFFFD};
        }
        const char32_t cp = static_cast<char32_t>(((lead & 0x1FU) << 6) | (*b1 & 0x3FU));
        return Utf8Unit{index, index + 2, cp};
    }
    if ((lead & 0xF0U) == 0xE0U) {
        const auto b1 = continuation(1);
        const auto b2 = continuation(2);
        if (!b1.has_value() || !b2.has_value()) {
            return Utf8Unit{index, index + 1, 0xFFFD};
        }
        const char32_t cp =
            static_cast<char32_t>(((lead & 0x0FU) << 12) | ((*b1 & 0x3FU) << 6) | (*b2 & 0x3FU));
        return Utf8Unit{index, index + 3, cp};
    }
    if ((lead & 0xF8U) == 0xF0U) {
        const auto b1 = continuation(1);
        const auto b2 = continuation(2);
        const auto b3 = continuation(3);
        if (!b1.has_value() || !b2.has_value() || !b3.has_value()) {
            return Utf8Unit{index, index + 1, 0xFFFD};
        }
        const char32_t cp = static_cast<char32_t>(((lead & 0x07U) << 18) | ((*b1 & 0x3FU) << 12) |
                                                  ((*b2 & 0x3FU) << 6) | (*b3 & 0x3FU));
        return Utf8Unit{index, index + 4, cp};
    }

    return Utf8Unit{index, index + 1, 0xFFFD};
}

std::vector<Utf8Unit> decode_utf8_units(std::string_view text) {
    std::vector<Utf8Unit> units;
    for (std::size_t index = 0; index < text.size();) {
        const auto unit =
            decode_utf8_unit(text, index).value_or(Utf8Unit{index, index + 1, 0xFFFD});
        units.push_back(unit);
        index = std::max(unit.next_index, index + 1);
    }
    return units;
}

namespace {

bool is_combining_mark(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

bool is_variation_selector(char32_t cp) {
    return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool is_emoji_modifier(char32_t cp) {
    return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

bool is_regional_indicator(char32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool is_grapheme_extend(char32_t cp) {
    return is_combining_mark(cp) || is_variation_selector(cp) || is_emoji_modifier(cp);
}

} // namespace

std::vector<std::size_t> grapheme_boundaries(std::string_view text) {
    std::vector<std::size_t> boundaries;
    boundaries.push_back(0);

    const auto units = decode_utf8_units(text);
    if (units.empty()) {
        return boundaries;
    }

    std::size_t index = 0;
    while (index < units.size()) {
        std::size_t next = index + 1;

        if (is_regional_indicator(units[index].code_point)) {
            if (next < units.size() && is_regional_indicator(units[next].code_point)) {
                ++next;
            }
        } else {
            while (next < units.size()) {
                const auto cp = units[next].code_point;
                if (is_grapheme_extend(cp)) {
                    ++next;
                    continue;
                }
                if (cp == 0x200D) {
                    ++next;
                    if (next < units.size()) {
                        ++next;
                        while (next < units.size() && is_grapheme_extend(units[next].code_point)) {
                            ++next;
                        }
                    }
                    continue;
                }
                break;
            }
        }

        const std::size_t boundary = next < units.size() ? units[next].byte_index : text.size();
        boundaries.push_back(boundary);
        index = next;
    }

    return boundaries;
}

std::size_t previous_grapheme_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    if (it == boundaries.begin()) {
        return 0;
    }
    if (it != boundaries.end() && *it == position) {
        return *std::prev(it);
    }
    return *std::prev(it);
}

std::size_t next_grapheme_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    const auto it = std::upper_bound(boundaries.begin(), boundaries.end(), position);
    return it != boundaries.end() ? *it : text.size();
}

std::size_t nearest_grapheme_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    if (it == boundaries.end()) {
        return text.size();
    }
    if (*it == position || it == boundaries.begin()) {
        return *it;
    }
    const auto prev = *std::prev(it);
    return (position - prev) <= (*it - position) ? prev : *it;
}

namespace {

bool is_unicode_whitespace(char32_t cp) {
    switch (cp) {
    case 0x0009:
    case 0x000A:
    case 0x000B:
    case 0x000C:
    case 0x000D:
    case 0x0020:
    case 0x0085:
    case 0x00A0:
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return true;
    default:
        return false;
    }
}

enum class GraphemeKind : uint8_t {
    Word,
    Space,
    Punctuation,
};

GraphemeKind classify_grapheme(char32_t cp) {
    if (is_unicode_whitespace(cp)) {
        return GraphemeKind::Space;
    }
    if (cp > 0x7FU) {
        return GraphemeKind::Word;
    }
    if (std::isalnum(static_cast<unsigned char>(cp)) != 0 || cp == '_') {
        return GraphemeKind::Word;
    }
    return GraphemeKind::Punctuation;
}

std::vector<GraphemeKind> grapheme_kinds(std::string_view text,
                                         const std::vector<std::size_t>& boundaries) {
    std::vector<GraphemeKind> kinds;
    kinds.reserve(boundaries.size() > 0 ? boundaries.size() - 1 : 0);
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const auto unit = decode_utf8_unit(text, boundaries[i])
                              .value_or(Utf8Unit{boundaries[i], boundaries[i] + 1, 0xFFFD});
        kinds.push_back(classify_grapheme(unit.code_point));
    }
    return kinds;
}

} // namespace

std::size_t previous_word_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    if (boundaries.size() <= 1 || position == 0) {
        return 0;
    }

    const auto kinds = grapheme_kinds(text, boundaries);
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    std::size_t cluster = 0;
    if (it == boundaries.end()) {
        cluster = kinds.size();
    } else if (*it == position) {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    } else {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    }

    while (cluster > 0 && kinds[cluster - 1] == GraphemeKind::Space) {
        --cluster;
    }
    if (cluster == 0) {
        return 0;
    }

    while (cluster > 0 && kinds[cluster - 1] != GraphemeKind::Word) {
        --cluster;
    }
    while (cluster > 0 && kinds[cluster - 1] == GraphemeKind::Word) {
        --cluster;
    }
    return boundaries[cluster];
}

std::size_t next_word_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    if (boundaries.size() <= 1 || position >= text.size()) {
        return text.size();
    }

    const auto kinds = grapheme_kinds(text, boundaries);
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    std::size_t cluster = 0;
    if (it == boundaries.end()) {
        return text.size();
    }
    if (*it == position) {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    } else {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    }
    if (cluster >= kinds.size()) {
        return text.size();
    }

    if (kinds[cluster] != GraphemeKind::Word) {
        while (cluster < kinds.size() && kinds[cluster] != GraphemeKind::Word) {
            ++cluster;
        }
    }
    while (cluster < kinds.size() && kinds[cluster] == GraphemeKind::Word) {
        ++cluster;
    }
    return boundaries[cluster];
}

std::pair<std::size_t, std::size_t> word_selection_range(std::string_view text,
                                                         std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    if (boundaries.size() <= 1) {
        return {0, text.size()};
    }

    const auto kinds = grapheme_kinds(text, boundaries);
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    std::size_t cluster = 0;
    if (it == boundaries.end()) {
        cluster = kinds.size() - 1;
    } else if (*it == position) {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
        if (cluster == kinds.size()) {
            cluster = kinds.size() - 1;
        } else if (cluster > 0) {
            --cluster;
        }
    } else {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
        if (cluster > 0) {
            --cluster;
        }
    }

    if (kinds[cluster] != GraphemeKind::Word) {
        std::size_t forward = cluster;
        while (forward < kinds.size() && kinds[forward] != GraphemeKind::Word) {
            ++forward;
        }
        if (forward < kinds.size()) {
            cluster = forward;
        } else {
            std::size_t backward = cluster;
            while (backward > 0 && kinds[backward] != GraphemeKind::Word) {
                --backward;
            }
            if (kinds[backward] == GraphemeKind::Word) {
                cluster = backward;
            } else {
                return {boundaries[cluster], boundaries[cluster + 1]};
            }
        }
    }

    std::size_t start_cluster = cluster;
    while (start_cluster > 0 && kinds[start_cluster - 1] == GraphemeKind::Word) {
        --start_cluster;
    }
    std::size_t end_cluster = cluster + 1;
    while (end_cluster < kinds.size() && kinds[end_cluster] == GraphemeKind::Word) {
        ++end_cluster;
    }
    return {boundaries[start_cluster], boundaries[end_cluster]};
}

} // namespace nk::detail
