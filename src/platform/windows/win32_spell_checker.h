#pragma once

/// @file win32_spell_checker.h
/// @brief Windows Spell Checking API-backed spell-checking service (private header).

#include <nk/platform/spell_checker.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Spell checker backed by the Windows Spell Checking API
/// (`ISpellCheckerFactory` / `ISpellChecker`). Lazily picks a supported
/// language on first use; when no spell-check language is installed (e.g. some
/// Windows Server SKUs) every call is a graceful no-op, matching the
/// "locale unavailable -> empty" contract of SpellChecker.
class Win32SpellChecker : public SpellChecker {
public:
    Win32SpellChecker() = default;
    ~Win32SpellChecker() override;

    [[nodiscard]] std::vector<SpellCheckRange> check(std::string_view text) override;
    [[nodiscard]] std::vector<std::string> suggestions(std::string_view misspelled_word) override;

private:
    // Lazily created on the STA thread. Held via void* so the Windows SDK
    // header (<spellcheck.h>) does not leak into this private header.
    void* checker_ = nullptr; // owns an ISpellChecker*; released in the dtor.
    void* factory_ = nullptr; // owns an ISpellCheckerFactory*; released in the dtor.
};

} // namespace nk
