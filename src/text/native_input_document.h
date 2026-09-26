#pragma once

#include <cstddef>
#include <nk/platform/events.h>
#include <optional>
#include <string>
#include <string_view>

namespace nk::detail {

// Native text-input clients (Cocoa NSTextInputClient) address text in UTF-16 units.
struct Utf16Range {
    std::size_t location = 0;
    std::size_t length = 0;

    bool operator==(const Utf16Range&) const = default;
};

struct NativeInputSubstring {
    std::string text;
    Utf16Range range;
};

// The document an input method sees: committed editor text with any active
// composition shown in place of the selection it will replace. Ranges that
// split a surrogate pair widen to whole code points; offsets past the end are
// rejected rather than silently redirected to the end of the text.
class NativeInputDocument {
public:
    NativeInputDocument(std::string_view committed,
                        std::size_t cursor,
                        std::size_t anchor,
                        std::string_view composition = {},
                        std::size_t composition_selection_start = 0,
                        std::size_t composition_selection_end = 0);

    [[nodiscard]] std::size_t length() const;
    [[nodiscard]] std::optional<Utf16Range> marked_range() const;
    [[nodiscard]] Utf16Range selected_range() const;
    [[nodiscard]] Utf16Range clamp(Utf16Range range) const;
    [[nodiscard]] std::optional<NativeInputSubstring> substring(Utf16Range range) const;
    // Committed bytes that a native replacement range addresses, or nullopt when
    // it is invalid or matches what Commit and Preedit replace by default.
    [[nodiscard]] std::optional<TextInputRange> committed_replacement(Utf16Range range) const;

private:
    struct ByteRange {
        std::size_t start = 0;
        std::size_t end = 0;
    };

    [[nodiscard]] std::optional<ByteRange> byte_range(Utf16Range range) const;
    [[nodiscard]] Utf16Range utf16_range(ByteRange range) const;
    [[nodiscard]] std::size_t committed_offset(std::size_t offset, bool range_end) const;

    std::string text_;
    std::size_t target_start_ = 0;
    std::size_t target_end_ = 0;
    std::size_t composition_size_ = 0;
    ByteRange selection_;
};

} // namespace nk::detail
