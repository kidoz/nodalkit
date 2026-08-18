#include "win32_spell_checker.h"

#include <nk/foundation/logging.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <objbase.h>
#include <spellcheck.h>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace nk {
namespace {

/// RAII COM apartment scoped to a single spell-check call. The spell checker is
/// apartment-threaded, so it is created and used on the calling (UI) thread.
class ComApartment {
public:
    ComApartment()
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
        , should_uninitialize_(SUCCEEDED(result_)) {}

    ~ComApartment() {
        if (should_uninitialize_) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool usable() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
    bool should_uninitialize_ = false;
};

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int wide_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wide_length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        text.data(),
                        static_cast<int>(text.size()),
                        wide.data(),
                        wide_length);
    return wide;
}

[[nodiscard]] std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int utf8_length = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_length <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(utf8_length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        utf8.data(),
                        utf8_length,
                        nullptr,
                        nullptr);
    return utf8;
}

/// Byte length of the UTF-8 encoding of the first `utf16_code_units` UTF-16
/// code units of `wide`. Used to convert the UTF-16/wchar_t spans returned by
/// the Windows Spell Checking API into the UTF-8 byte offsets the SpellChecker
/// contract requires (mirrors macos_spell_checker.mm's prefix re-measurement).
[[nodiscard]] std::size_t utf8_bytes_for_prefix(std::wstring_view wide,
                                                std::size_t utf16_code_units) {
    const std::size_t clamped = std::min(utf16_code_units, wide.size());
    return wide_to_utf8(wide.substr(0, clamped)).size();
}

} // namespace

Win32SpellChecker::~Win32SpellChecker() {
    // ISpellChecker and ISpellCheckerFactory are released via the vtable the
    // header can't name; do the explicit Release() through the raw pointer.
    if (checker_ != nullptr) {
        static_cast<IUnknown*>(checker_)->Release();
        checker_ = nullptr;
    }
    if (factory_ != nullptr) {
        static_cast<IUnknown*>(factory_)->Release();
        factory_ = nullptr;
    }
}

namespace {

/// Lazily create the ISpellChecker on first use. Returns null and logs once
/// when no supported language is installed (graceful no-op per contract).
[[nodiscard]] ISpellChecker* ensure_checker(void*& factory_slot, void*& checker_slot) {
    if (checker_slot != nullptr) {
        return static_cast<ISpellChecker*>(checker_slot);
    }
    if (factory_slot == nullptr) {
        ComApartment apartment;
        if (!apartment.usable()) {
            return nullptr;
        }
        ComPtr<ISpellCheckerFactory> factory;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
        const HRESULT create_hr = CoCreateInstance(
            __uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        if (FAILED(create_hr)) {
            return nullptr;
        }
        factory_slot = factory.Detach(); // owned by self; released in dtor
    }

    auto* factory = static_cast<ISpellCheckerFactory*>(factory_slot);
    // Pick the first supported language tag. If none are installed the contract
    // says to behave as if no errors were found.
    ComPtr<IEnumString> languages;
    if (FAILED(factory->get_SupportedLanguages(&languages)) || languages == nullptr) {
        return nullptr;
    }
    std::wstring language_tag;
    ULONG fetched = 0;
    LPOLESTR item = nullptr;
    while (languages->Next(1, &item, &fetched) == S_OK && fetched > 0) {
        if (item != nullptr) {
            language_tag.assign(item);
            CoTaskMemFree(item);
            break;
        }
    }
    if (language_tag.empty()) {
        NK_LOG_DEBUG("Win32SpellChecker", "No supported spell-check language installed");
        return nullptr;
    }

    ComApartment apartment;
    if (!apartment.usable()) {
        return nullptr;
    }
    ComPtr<ISpellChecker> checker;
    if (FAILED(factory->CreateSpellChecker(language_tag.c_str(), &checker))) {
        return nullptr;
    }
    checker_slot = checker.Detach();
    return static_cast<ISpellChecker*>(checker_slot);
}

} // namespace

std::vector<SpellCheckRange> Win32SpellChecker::check(std::string_view text) {
    std::vector<SpellCheckRange> result;
    if (text.empty()) {
        return result;
    }

    ComApartment apartment;
    if (!apartment.usable()) {
        return result;
    }

    auto* checker = ensure_checker(factory_, checker_);
    if (checker == nullptr) {
        return result;
    }

    const std::wstring wide = utf8_to_wide(text);
    if (wide.empty()) {
        return result;
    }

    ComPtr<IEnumSpellingError> errors;
    // ComprehensiveCheck catches grammar and spelling issues, matching the
    // NSSpellChecker `checkSpellingOfString:` breadth used by macos_spell_checker.
    if (FAILED(checker->ComprehensiveCheck(wide.c_str(), &errors)) || errors == nullptr) {
        return result;
    }

    while (true) {
        ComPtr<ISpellingError> error;
        // IEnumSpellingError::Next advances the enumeration itself; we just walk
        // it until there are no more genuine misspellings.
        if (errors->Next(&error) != S_OK || error == nullptr) {
            break;
        }
        ULONG start_index = 0;
        ULONG length = 0;
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
        if (FAILED(error->get_StartIndex(&start_index)) || FAILED(error->get_Length(&length)) ||
            FAILED(error->get_CorrectiveAction(&action))) {
            break;
        }
        // get_CorrectiveAction returns CORRECTIVE_ACTION_NONE for benign cases;
        // skip those so we only flag genuine misspellings the way NSSpellChecker does.
        if (length == 0 || action == CORRECTIVE_ACTION_NONE) {
            continue;
        }
        const std::size_t byte_start = utf8_bytes_for_prefix(wide, start_index);
        const std::size_t byte_length =
            utf8_bytes_for_prefix(wide, start_index + length) - byte_start;
        result.push_back({byte_start, byte_length});
    }

    return result;
}

std::vector<std::string> Win32SpellChecker::suggestions(std::string_view misspelled_word) {
    std::vector<std::string> result;
    if (misspelled_word.empty()) {
        return result;
    }

    ComApartment apartment;
    if (!apartment.usable()) {
        return result;
    }

    auto* checker = ensure_checker(factory_, checker_);
    if (checker == nullptr) {
        return result;
    }

    const std::wstring wide = utf8_to_wide(misspelled_word);
    if (wide.empty()) {
        return result;
    }

    ComPtr<IEnumString> suggestions;
    if (FAILED(checker->Suggest(wide.c_str(), &suggestions)) || suggestions == nullptr) {
        return result;
    }

    ULONG fetched = 0;
    LPOLESTR item = nullptr;
    while (suggestions->Next(1, &item, &fetched) == S_OK && fetched > 0) {
        if (item != nullptr) {
            auto utf8 = wide_to_utf8(item);
            if (!utf8.empty()) {
                result.emplace_back(std::move(utf8));
            }
            CoTaskMemFree(item);
        }
    }
    return result;
}

} // namespace nk
