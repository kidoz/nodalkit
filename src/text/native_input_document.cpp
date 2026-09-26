#include "native_input_document.h"

#include "text_boundaries.h"

#include <algorithm>

namespace nk::detail {

NativeInputDocument::NativeInputDocument(std::string_view committed,
                                         std::size_t cursor,
                                         std::size_t anchor,
                                         std::string_view composition,
                                         std::size_t composition_selection_start,
                                         std::size_t composition_selection_end)
    : text_(committed) {
    cursor = std::min(cursor, committed.size());
    anchor = std::min(anchor, committed.size());
    target_start_ = std::min(cursor, anchor);
    target_end_ = std::max(cursor, anchor);
    selection_ = {target_start_, target_end_};
    if (composition.empty()) {
        return;
    }
    text_.replace(target_start_, target_end_ - target_start_, composition);
    composition_size_ = composition.size();
    const auto start = std::min(composition_selection_start, composition.size());
    const auto end = std::min(composition_selection_end, composition.size());
    selection_ = {target_start_ + std::min(start, end), target_start_ + std::max(start, end)};
}

std::size_t NativeInputDocument::length() const {
    return utf16_offset_from_utf8(text_, text_.size());
}

std::optional<Utf16Range> NativeInputDocument::marked_range() const {
    if (composition_size_ == 0) {
        return std::nullopt;
    }
    return utf16_range({target_start_, target_start_ + composition_size_});
}

Utf16Range NativeInputDocument::selected_range() const {
    return utf16_range(selection_);
}

Utf16Range NativeInputDocument::clamp(Utf16Range range) const {
    range.location = std::min(range.location, length());
    return utf16_range(byte_range(range).value_or(ByteRange{text_.size(), text_.size()}));
}

std::optional<NativeInputSubstring> NativeInputDocument::substring(Utf16Range range) const {
    const auto bytes = byte_range(range);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    return NativeInputSubstring{
        .text = text_.substr(bytes->start, bytes->end - bytes->start),
        .range = utf16_range(*bytes),
    };
}

std::optional<TextInputRange> NativeInputDocument::committed_replacement(Utf16Range range) const {
    const auto bytes = byte_range(range);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    const TextInputRange committed{committed_offset(bytes->start, false),
                                   committed_offset(bytes->end, true)};
    if (committed == TextInputRange{target_start_, target_end_}) {
        return std::nullopt;
    }
    return committed;
}

std::optional<NativeInputDocument::ByteRange>
NativeInputDocument::byte_range(Utf16Range range) const {
    const auto units = length();
    if (range.location > units) {
        return std::nullopt;
    }
    const auto end = range.location + std::min(range.length, units - range.location);
    return ByteRange{utf8_offset_from_utf16(text_, range.location),
                     utf8_offset_from_utf16(text_, end, true)};
}

Utf16Range NativeInputDocument::utf16_range(ByteRange range) const {
    const auto start = utf16_offset_from_utf8(text_, range.start);
    return {start, utf16_offset_from_utf8(text_, range.end) - start};
}

std::size_t NativeInputDocument::committed_offset(std::size_t offset, bool range_end) const {
    if (composition_size_ == 0 || offset <= target_start_) {
        return offset;
    }
    // A range that reaches into the composition covers the whole composition target.
    if (offset < target_start_ + composition_size_) {
        return range_end ? target_end_ : target_start_;
    }
    return offset - composition_size_ + (target_end_ - target_start_);
}

} // namespace nk::detail
