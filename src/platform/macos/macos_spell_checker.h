#pragma once

/// @file macos_spell_checker.h
/// @brief macOS NSSpellChecker-backed spell-checking service (private header).

#include <nk/platform/spell_checker.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class MacosSpellChecker : public SpellChecker {
public:
    MacosSpellChecker() = default;
    ~MacosSpellChecker() override = default;

    [[nodiscard]] std::vector<SpellCheckRange> check(std::string_view text) override;
    [[nodiscard]] std::vector<std::string> suggestions(std::string_view misspelled_word) override;
};

} // namespace nk
