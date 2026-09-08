#include "text_edit_buffer.h"

#include "text_boundaries.h"

#include <algorithm>
#include <utility>

namespace nk::detail {

std::size_t TextEditBuffer::selection_start() const {
    return std::min(cursor, selection_anchor);
}

std::size_t TextEditBuffer::selection_end() const {
    return std::max(cursor, selection_anchor);
}

bool TextEditBuffer::has_selection() const {
    return cursor != selection_anchor;
}

void TextEditBuffer::move_cursor(std::size_t position, bool extend) {
    clear_preedit();
    cursor = std::min(position, text.size());
    if (!extend) {
        selection_anchor = cursor;
    }
    break_undo_group();
}

void TextEditBuffer::select_all() {
    clear_preedit();
    selection_anchor = 0;
    cursor = text.size();
    break_undo_group();
}

TextEditBuffer::State TextEditBuffer::state() const {
    return {text, cursor, selection_anchor};
}

void TextEditBuffer::restore(const State& value) {
    text = value.text;
    cursor = value.cursor;
    selection_anchor = value.anchor;
}

bool TextEditBuffer::replace(std::size_t start,
                             std::size_t end,
                             std::string_view inserted,
                             EditGroup group) {
    start = std::min(start, text.size());
    end = std::clamp(end, start, text.size());
    if (start == end && inserted.empty()) {
        return false;
    }
    if (has_selection() || has_preedit()) {
        group = EditGroup::None;
    }
    clear_preedit();
    const bool coalesce = group != EditGroup::None && group == last_group_ &&
                          history_index_ + 1 == history_.size() && history_.size() > 1 &&
                          !has_selection() && cursor == history_.back().cursor &&
                          selection_anchor == history_.back().anchor;
    if (!coalesce) {
        history_.resize(history_index_ + 1);
        // Navigation is not an undo step, but undo must restore the selection
        // and caret that existed immediately before this edit.
        history_.back() = state();
    }
    text.replace(start, end - start, inserted);
    cursor = start + inserted.size();
    selection_anchor = cursor;
    if (coalesce) {
        history_.back() = state();
    } else {
        history_.push_back(state());
        ++history_index_;
    }
    last_group_ = group;
    return true;
}

bool TextEditBuffer::undo() {
    clear_preedit();
    break_undo_group();
    if (history_index_ == 0) {
        return false;
    }
    restore(history_[--history_index_]);
    return true;
}

bool TextEditBuffer::redo() {
    clear_preedit();
    break_undo_group();
    if (history_index_ + 1 == history_.size()) {
        return false;
    }
    restore(history_[++history_index_]);
    return true;
}

void TextEditBuffer::reset_history() {
    clear_preedit();
    history_ = {state()};
    history_index_ = 0;
    break_undo_group();
}

void TextEditBuffer::break_undo_group() {
    last_group_ = EditGroup::None;
}

void TextEditBuffer::set_preedit(std::string value, std::size_t start, std::size_t end) {
    preedit_text = std::move(value);
    start = std::min(start, preedit_text.size());
    end = std::min(end, preedit_text.size());
    // Platform offsets are bytes. Do not slice a UTF-8 code point if an input
    // context supplies a partial or out-of-range offset.
    while (start > 0 && start < preedit_text.size() &&
           (static_cast<unsigned char>(preedit_text[start]) & 0xC0U) == 0x80U) {
        --start;
    }
    while (end < preedit_text.size() &&
           (static_cast<unsigned char>(preedit_text[end]) & 0xC0U) == 0x80U) {
        ++end;
    }
    preedit_selection_start = start;
    preedit_selection_end = end;
    break_undo_group();
}

bool TextEditBuffer::clear_preedit() {
    const bool changed = has_preedit();
    preedit_text.clear();
    preedit_selection_start = 0;
    preedit_selection_end = 0;
    if (changed) {
        break_undo_group();
    }
    return changed;
}

bool TextEditBuffer::has_preedit() const {
    return !preedit_text.empty();
}

std::string TextEditBuffer::display_text() const {
    if (!has_preedit()) {
        return text;
    }
    auto display = text;
    display.replace(selection_start(), selection_end() - selection_start(), preedit_text);
    return display;
}

std::size_t TextEditBuffer::display_caret_position() const {
    return has_preedit() ? selection_start() + preedit_selection_end : cursor;
}

std::size_t TextEditBuffer::text_position_from_display(std::size_t position) const {
    if (!has_preedit()) {
        return std::min(position, text.size());
    }
    const auto start = selection_start();
    if (position <= start) {
        return position;
    }
    if (position <= start + preedit_text.size()) {
        return start;
    }
    return std::min(text.size(), position - preedit_text.size() + selection_end() - start);
}

bool TextEditBuffer::delete_surrounding(std::size_t before, std::size_t after) {
    before = std::min(before, cursor);
    after = std::min(after, text.size() - cursor);
    if (before == 0 && after == 0) {
        return false;
    }
    auto start = cursor - before;
    auto end = cursor + after;
    // Segment each hard line separately: a combining mark following a newline
    // must not cause the newline to be swallowed by outward range rounding.
    if (start < text.size() && text[start] != '\n') {
        const auto newline = start > 0 ? text.rfind('\n', start - 1) : std::string::npos;
        const auto line_start = newline == std::string::npos ? 0 : newline + 1;
        const auto line_end = text.find('\n', start);
        const auto line = std::string_view(text).substr(
            line_start, (line_end == std::string::npos ? text.size() : line_end) - line_start);
        start = line_start + previous_grapheme_boundary(line, start - line_start + 1);
    }
    if (end > 0 && text[end - 1] != '\n') {
        const auto newline = text.rfind('\n', end - 1);
        const auto line_start = newline == std::string::npos ? 0 : newline + 1;
        const auto line_end = text.find('\n', end);
        const auto line = std::string_view(text).substr(
            line_start, (line_end == std::string::npos ? text.size() : line_end) - line_start);
        end = line_start + next_grapheme_boundary(line, end - line_start - 1);
    }
    return replace(start, end, {});
}

} // namespace nk::detail
