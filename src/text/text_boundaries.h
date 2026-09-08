#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace nk::detail {

struct Utf8Unit {
    std::size_t byte_index = 0;
    std::size_t next_index = 0;
    char32_t code_point = 0;
};

// Private editing helpers shared by the built-in text controls. This retains
// TextField's lightweight cluster handling; it is not a complete Unicode
// grapheme-break implementation. Multiline callers segment each line separately.
std::optional<Utf8Unit> decode_utf8_unit(std::string_view text, std::size_t index);
std::vector<Utf8Unit> decode_utf8_units(std::string_view text);
std::vector<std::size_t> grapheme_boundaries(std::string_view text);
std::size_t previous_grapheme_boundary(std::string_view text, std::size_t position);
std::size_t next_grapheme_boundary(std::string_view text, std::size_t position);
std::size_t nearest_grapheme_boundary(std::string_view text, std::size_t position);

std::size_t previous_word_boundary(std::string_view text, std::size_t position);
std::size_t next_word_boundary(std::string_view text, std::size_t position);
std::pair<std::size_t, std::size_t> word_selection_range(std::string_view text,
                                                         std::size_t position);

} // namespace nk::detail
