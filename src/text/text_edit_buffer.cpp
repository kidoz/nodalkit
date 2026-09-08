#include "text_edit_buffer.h"

#include <algorithm>

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
    cursor = std::min(position, text.size());
    if (!extend) {
        selection_anchor = cursor;
    }
    break_undo_group();
}

void TextEditBuffer::select_all() {
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
    if (has_selection()) {
        group = EditGroup::None;
    }
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
    break_undo_group();
    if (history_index_ == 0) {
        return false;
    }
    restore(history_[--history_index_]);
    return true;
}

bool TextEditBuffer::redo() {
    break_undo_group();
    if (history_index_ + 1 == history_.size()) {
        return false;
    }
    restore(history_[++history_index_]);
    return true;
}

void TextEditBuffer::reset_history() {
    history_ = {state()};
    history_index_ = 0;
    break_undo_group();
}

void TextEditBuffer::break_undo_group() {
    last_group_ = EditGroup::None;
}

} // namespace nk::detail
