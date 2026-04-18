#pragma once

/// @file spell_checker.h
/// @brief Abstract platform spell-checking interface.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A misspelled range within a UTF-8 string, expressed as byte offsets.
struct SpellCheckRange {
    std::size_t start = 0;
    std::size_t length = 0;
};

/// Platform spell-checking service. Backends provide implementations (macOS
/// via NSSpellChecker today; other platforms return nullptr from
/// PlatformBackend::spell_checker() until their own wiring lands).
class SpellChecker {
public:
    virtual ~SpellChecker() = default;

    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    /// Locate misspelled ranges in `text`. Ranges are byte offsets into the
    /// UTF-8 buffer. Returning an empty vector means "no misspellings found"
    /// or "spell-checking unavailable for this locale".
    [[nodiscard]] virtual std::vector<SpellCheckRange> check(std::string_view text) = 0;

    /// Return replacement suggestions for a misspelled word.
    [[nodiscard]] virtual std::vector<std::string> suggestions(std::string_view misspelled_word) = 0;

protected:
    SpellChecker() = default;
};

} // namespace nk
