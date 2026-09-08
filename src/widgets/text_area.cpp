#include "../text/text_boundaries.h"
#include "../text/text_clipboard.h"
#include "../text/text_edit_buffer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/text_area.h>
#include <optional>
#include <string>
#include <vector>

namespace nk {

namespace {

FontDescriptor text_area_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto pos = text.find('\n', start);
        if (pos == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    if (lines.empty()) {
        lines.emplace_back();
    }
    return lines;
}

struct TextLine {
    std::size_t start;
    std::string_view text;
};

TextLine line_at(std::string_view text, std::size_t position) {
    const auto previous_newline =
        position > 0 ? text.rfind('\n', position - 1) : std::string_view::npos;
    const auto start = previous_newline == std::string_view::npos ? 0 : previous_newline + 1;
    const auto next_newline = text.find('\n', position);
    const auto end = next_newline == std::string_view::npos ? text.size() : next_newline;
    return {.start = start, .text = text.substr(start, end - start)};
}

std::size_t previous_text_boundary(std::string_view text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    if (text[position - 1] == '\n') {
        return position - 1;
    }
    const auto line = line_at(text, position);
    return line.start + detail::previous_grapheme_boundary(line.text, position - line.start);
}

std::size_t next_text_boundary(std::string_view text, std::size_t position) {
    if (position == text.size()) {
        return position;
    }
    if (text[position] == '\n') {
        return position + 1;
    }
    const auto line = line_at(text, position);
    return line.start + detail::next_grapheme_boundary(line.text, position - line.start);
}

} // namespace

struct TextArea::Impl {
    detail::TextEditBuffer edit;
    std::string placeholder;
    bool editable = true;
    int visible_rows = 4;
    Signal<> text_changed;
    enum class MouseSelection { Character, Word, Line };
    bool selecting = false;
    MouseSelection selection_mode = MouseSelection::Character;
    std::size_t mouse_base_start = 0;
    std::size_t mouse_base_end = 0;
    float scroll_x = 0.0F;
    float scroll_y = 0.0F;
    float content_width = 0.0F;
    std::vector<std::size_t> line_starts{0};
    std::vector<std::size_t> display_line_starts{0};
    std::string display_text;
    std::optional<float> preferred_x;
    bool focused_state = false;
};

std::shared_ptr<TextArea> TextArea::create() {
    return std::shared_ptr<TextArea>(new TextArea());
}

TextArea::TextArea() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("text-area");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TextInput);
    accessible.set_value(impl_->edit.text);
}

TextArea::~TextArea() = default;

std::string_view TextArea::text() const {
    return impl_->edit.text;
}

void TextArea::set_text(std::string text) {
    if (impl_->edit.text != text) {
        impl_->edit.text = std::move(text);
        impl_->edit.cursor = impl_->edit.text.size();
        impl_->edit.selection_anchor = impl_->edit.cursor;
        impl_->edit.reset_history();
        did_edit();
    }
}

std::string_view TextArea::placeholder() const {
    return impl_->placeholder;
}

void TextArea::set_placeholder(std::string placeholder) {
    if (impl_->placeholder != placeholder) {
        impl_->placeholder = std::move(placeholder);
        queue_redraw();
    }
}

bool TextArea::is_editable() const {
    return impl_->editable;
}

void TextArea::set_editable(bool editable) {
    if (impl_->editable != editable) {
        impl_->editable = editable;
        if (!editable) {
            clear_preedit();
        }
        impl_->edit.break_undo_group();
        impl_->selecting = false;
        queue_redraw();
    }
}

int TextArea::visible_rows() const {
    return impl_->visible_rows;
}

void TextArea::set_visible_rows(int rows) {
    rows = std::max(1, rows);
    if (impl_->visible_rows != rows) {
        impl_->visible_rows = rows;
        queue_layout();
        queue_redraw();
    }
}

Signal<>& TextArea::on_text_changed() {
    return impl_->text_changed;
}

SizeRequest TextArea::measure(const Constraints& /*constraints*/) const {
    const float row_height = line_height();
    const float min_width = theme_number("min-width", 300.0F);
    const float padding = theme_number("padding", 8.0F);
    const float h = static_cast<float>(impl_->visible_rows) * row_height + (padding * 2.0F);
    return {min_width, h, min_width, h};
}

void TextArea::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    // Attachment can replace estimated text widths with the window's shaper.
    refresh_content_metrics();
    ensure_caret_visible();
}

std::size_t TextArea::cursor_position() const {
    return impl_->edit.cursor;
}

std::size_t TextArea::selection_start() const {
    return impl_->edit.selection_start();
}

std::size_t TextArea::selection_end() const {
    return impl_->edit.selection_end();
}

bool TextArea::has_selection() const {
    return impl_->edit.has_selection();
}

void TextArea::select_all() {
    clear_preedit();
    impl_->edit.select_all();
    impl_->selecting = false;
    impl_->preferred_x.reset();
    did_select();
}

void TextArea::sync_primary_selection() const {
    detail::write_text_clipboard(
        impl_->edit.text.substr(selection_start(), selection_end() - selection_start()), true);
}

void TextArea::did_select() {
    sync_primary_selection();
    ensure_caret_visible();
    queue_redraw();
}

bool TextArea::replace_selection(std::string_view text, bool typing) {
    const auto group =
        typing && !has_selection() ? detail::EditGroup::Insert : detail::EditGroup::None;
    if (!impl_->edit.replace(selection_start(), selection_end(), text, group)) {
        return false;
    }
    did_edit();
    return true;
}

Rect TextArea::text_rect() const {
    const auto a = allocation();
    const float padding = std::max(0.0F, theme_number("padding", 8.0F));
    // Focus decorations must not move hit-testing or the scroll origin.
    return {a.x + padding,
            a.y + padding,
            std::max(0.0F, a.width - 2.0F * padding),
            std::max(0.0F, a.height - 2.0F * padding)};
}

float TextArea::line_height() const {
    return std::max(1.0F, theme_number("line-height", 20.0F));
}

std::size_t TextArea::cursor_line() const {
    return static_cast<std::size_t>(
        std::upper_bound(impl_->line_starts.begin(), impl_->line_starts.end(), impl_->edit.cursor) -
        impl_->line_starts.begin() - 1);
}

Rect TextArea::text_input_caret_rect() const {
    const auto viewport = text_rect();
    const auto position = impl_->edit.display_caret_position();
    const auto line = line_at(impl_->display_text, position);
    const auto row = static_cast<std::size_t>(std::upper_bound(impl_->display_line_starts.begin(),
                                                               impl_->display_line_starts.end(),
                                                               position) -
                                              impl_->display_line_starts.begin() - 1);
    const float x =
        measure_text(line.text.substr(0, position - line.start), text_area_font()).width;
    return {viewport.x + x - impl_->scroll_x,
            viewport.y + static_cast<float>(row) * line_height() - impl_->scroll_y,
            1.5F,
            line_height()};
}

std::optional<WidgetTextInputState> TextArea::text_input_state() const {
    if (!impl_->editable) {
        return std::nullopt;
    }
    return WidgetTextInputState{.text = impl_->edit.text,
                                .cursor = impl_->edit.cursor,
                                .anchor = impl_->edit.selection_anchor,
                                .caret_rect = text_input_caret_rect()};
}

void TextArea::refresh_content_metrics() {
    impl_->line_starts.clear();
    std::size_t start = 0;
    for (const auto line : split_lines(impl_->edit.text)) {
        impl_->line_starts.push_back(start);
        start += line.size() + 1;
    }
    impl_->display_text = impl_->edit.display_text();
    impl_->display_line_starts.clear();
    impl_->content_width = 0.0F;
    start = 0;
    for (const auto line : split_lines(impl_->display_text)) {
        impl_->display_line_starts.push_back(start);
        impl_->content_width =
            std::max(impl_->content_width, measure_text(line, text_area_font()).width);
        start += line.size() + 1;
    }
}

bool TextArea::clear_preedit() {
    if (!impl_->edit.clear_preedit()) {
        return false;
    }
    refresh_content_metrics();
    ensure_caret_visible();
    queue_redraw();
    return true;
}

void TextArea::clamp_scroll() {
    const auto viewport = text_rect();
    const float width = impl_->content_width + 2.0F;
    const float height = static_cast<float>(impl_->display_line_starts.size()) * line_height();
    impl_->scroll_x = std::clamp(impl_->scroll_x, 0.0F, std::max(0.0F, width - viewport.width));
    impl_->scroll_y = std::clamp(impl_->scroll_y, 0.0F, std::max(0.0F, height - viewport.height));
}

void TextArea::ensure_caret_visible() {
    const auto viewport = text_rect();
    clamp_scroll();
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return;
    }
    const auto caret = text_input_caret_rect();
    if (caret.x < viewport.x) {
        impl_->scroll_x -= viewport.x - caret.x;
    } else if (caret.right() > viewport.right()) {
        impl_->scroll_x += caret.right() - viewport.right();
    }
    if (caret.y < viewport.y) {
        impl_->scroll_y -= viewport.y - caret.y;
    } else if (caret.bottom() > viewport.bottom()) {
        impl_->scroll_y += caret.bottom() - viewport.bottom();
    }
    clamp_scroll();
}

void TextArea::did_edit() {
    impl_->selecting = false;
    impl_->preferred_x.reset();
    refresh_content_metrics();
    ensure_caret_visible();
    ensure_accessible().set_value(impl_->edit.text);
    sync_primary_selection();
    impl_->text_changed.emit();
    queue_redraw();
}

std::size_t TextArea::position_at_x(std::string_view line, float x) const {
    const auto font = text_area_font();
    std::size_t best = 0;
    float distance = std::numeric_limits<float>::infinity();
    for (const auto boundary : detail::grapheme_boundaries(line)) {
        const float candidate = std::fabs(measure_text(line.substr(0, boundary), font).width - x);
        if (candidate < distance) {
            best = boundary;
            distance = candidate;
        }
    }
    return best;
}

std::size_t TextArea::hit_test_cursor(Point point) const {
    const auto viewport = text_rect();
    const float row =
        std::clamp(std::floor((point.y - viewport.y + impl_->scroll_y) / line_height()),
                   0.0F,
                   static_cast<float>(impl_->display_line_starts.size() - 1));
    const auto line =
        line_at(impl_->display_text, impl_->display_line_starts[static_cast<std::size_t>(row)]);
    const auto position =
        line.start + position_at_x(line.text, point.x - viewport.x + impl_->scroll_x);
    return impl_->edit.text_position_from_display(position);
}

void TextArea::extend_mouse_selection(Point point) {
    const auto position = hit_test_cursor(point);
    if (impl_->selection_mode == Impl::MouseSelection::Character) {
        impl_->edit.move_cursor(position, true);
    } else {
        const auto line = line_at(impl_->edit.text, position);
        auto start = line.start;
        auto end = line.start + line.text.size();
        if (impl_->selection_mode == Impl::MouseSelection::Word) {
            const auto range = detail::word_selection_range(line.text, position - line.start);
            start += range.first;
            end = line.start + range.second;
        } else if (end < impl_->edit.text.size()) {
            ++end;
        }
        if (end <= impl_->mouse_base_start) {
            impl_->edit.selection_anchor = impl_->mouse_base_end;
            impl_->edit.cursor = start;
        } else {
            impl_->edit.selection_anchor = impl_->mouse_base_start;
            impl_->edit.cursor = end;
        }
        impl_->edit.break_undo_group();
    }
    impl_->preferred_x.reset();
    did_select();
}

bool TextArea::handle_mouse_event(const MouseEvent& event) {
    if (impl_->selecting && (event.type == MouseEvent::Type::Move ||
                             (event.type == MouseEvent::Type::Release && event.button == 1))) {
        extend_mouse_selection({event.x, event.y});
        if (event.type == MouseEvent::Type::Release) {
            impl_->selecting = false;
        }
        return true;
    }
    if (!allocation().contains({event.x, event.y})) {
        return false;
    }
    if (event.type == MouseEvent::Type::Scroll) {
        if (!std::isfinite(event.scroll_dx) || !std::isfinite(event.scroll_dy)) {
            return false;
        }
        const auto viewport = text_rect();
        if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
            return false;
        }
        const float old_x = impl_->scroll_x;
        const float old_y = impl_->scroll_y;
        const float step = event.precise_scrolling ? 1.0F : 40.0F;
        float dx = event.scroll_dx;
        float dy = event.scroll_dy;
        if (((event.modifiers & Modifiers::Shift) != Modifiers::None) && dx == 0.0F) {
            dx = dy;
            dy = 0.0F;
        }
        impl_->scroll_x -= dx * step;
        impl_->scroll_y -= dy * step;
        clamp_scroll();
        if (old_x == impl_->scroll_x && old_y == impl_->scroll_y) {
            return false;
        }
        queue_redraw();
        return true;
    }
    if (event.type == MouseEvent::Type::Press && event.button == 3 && impl_->editable) {
        // Read before moving the caret changes primary-selection ownership.
        const auto pasted = detail::read_text_clipboard(true);
        if (pasted.empty()) {
            return false;
        }
        const auto position = hit_test_cursor({event.x, event.y});
        clear_preedit();
        impl_->edit.move_cursor(position, false);
        return replace_selection(pasted);
    }
    if (event.type == MouseEvent::Type::Press && event.button == 1) {
        const auto position = hit_test_cursor({event.x, event.y});
        clear_preedit();
        impl_->edit.break_undo_group();
        impl_->selecting = true;
        impl_->selection_mode = Impl::MouseSelection::Character;
        if (event.click_count >= 2) {
            impl_->selection_mode =
                event.click_count >= 3 ? Impl::MouseSelection::Line : Impl::MouseSelection::Word;
            const auto line = line_at(impl_->edit.text, position);
            auto start = line.start;
            auto end = line.start + line.text.size();
            if (impl_->selection_mode == Impl::MouseSelection::Word) {
                const auto range = detail::word_selection_range(line.text, position - line.start);
                start += range.first;
                end = line.start + range.second;
            } else if (end < impl_->edit.text.size()) {
                ++end;
            }
            impl_->mouse_base_start = impl_->edit.selection_anchor = start;
            impl_->mouse_base_end = impl_->edit.cursor = end;
        } else {
            impl_->edit.move_cursor(position,
                                    (event.modifiers & Modifiers::Shift) != Modifiers::None);
        }
        impl_->preferred_x.reset();
        grab_focus();
        did_select();
        return true;
    }
    return false;
}

bool TextArea::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (event.key == KeyCode::Escape) {
        return clear_preedit();
    }
    if (event.key == KeyCode::Return && impl_->edit.has_preedit()) {
        // The input context must commit or cancel composition before a newline.
        return true;
    }
    const bool document = ((event.modifiers & Modifiers::Ctrl) != Modifiers::None) ||
                          ((event.modifiers & Modifiers::Super) != Modifiers::None);
    const bool extend = (event.modifiers & Modifiers::Shift) != Modifiers::None;
    const bool word = (event.modifiers & (Modifiers::Alt | Modifiers::Ctrl)) != Modifiers::None;
    const bool shortcut =
        document && (event.modifiers & Modifiers::Alt) == Modifiers::None &&
        (event.key == KeyCode::A || event.key == KeyCode::C || event.key == KeyCode::X ||
         event.key == KeyCode::V || event.key == KeyCode::Z || event.key == KeyCode::Y);
    const bool navigation = event.key == KeyCode::Left || event.key == KeyCode::Right ||
                            event.key == KeyCode::Up || event.key == KeyCode::Down ||
                            event.key == KeyCode::Home || event.key == KeyCode::End ||
                            event.key == KeyCode::PageUp || event.key == KeyCode::PageDown ||
                            event.key == KeyCode::Backspace || event.key == KeyCode::Delete;
    const bool canceled_composition = (shortcut || navigation) && clear_preedit();
    impl_->selecting = false;
    if (document && (event.modifiers & Modifiers::Alt) == Modifiers::None) {
        switch (event.key) {
        case KeyCode::A:
            select_all();
            return true;
        case KeyCode::C:
        case KeyCode::X:
            impl_->edit.break_undo_group();
            if (!has_selection() || (event.key == KeyCode::X && !impl_->editable)) {
                return false;
            }
            detail::write_text_clipboard(
                impl_->edit.text.substr(selection_start(), selection_end() - selection_start()));
            return event.key == KeyCode::C || replace_selection({});
        case KeyCode::V:
            if (!impl_->editable) {
                return false;
            }
            if (const auto pasted = detail::read_text_clipboard(); !pasted.empty()) {
                return replace_selection(pasted);
            }
            return false;
        case KeyCode::Z:
        case KeyCode::Y:
            if (!impl_->editable) {
                return false;
            }
            if ((event.key == KeyCode::Y || extend) ? impl_->edit.redo() : impl_->edit.undo()) {
                did_edit();
                return true;
            }
            return canceled_composition;
        default:
            break;
        }
    }
    const auto current_line = line_at(impl_->edit.text, impl_->edit.cursor);
    const auto previous = [&] {
        if (word && impl_->edit.cursor > current_line.start) {
            return current_line.start +
                   detail::previous_word_boundary(current_line.text,
                                                  impl_->edit.cursor - current_line.start);
        }
        return previous_text_boundary(impl_->edit.text, impl_->edit.cursor);
    };
    const auto next = [&] {
        if (word && impl_->edit.cursor < current_line.start + current_line.text.size()) {
            return current_line.start +
                   detail::next_word_boundary(current_line.text,
                                              impl_->edit.cursor - current_line.start);
        }
        return next_text_boundary(impl_->edit.text, impl_->edit.cursor);
    };
    switch (event.key) {
    case KeyCode::Return:
        return impl_->editable && replace_selection("\n");
    case KeyCode::Backspace:
    case KeyCode::Delete: {
        if (!impl_->editable) {
            return false;
        }
        if (has_selection()) {
            return replace_selection({});
        }
        const bool backward = event.key == KeyCode::Backspace;
        const auto group = word       ? detail::EditGroup::None
                           : backward ? detail::EditGroup::DeleteBackward
                                      : detail::EditGroup::DeleteForward;
        if (impl_->edit.replace(backward ? previous() : impl_->edit.cursor,
                                backward ? impl_->edit.cursor : next(),
                                {},
                                group)) {
            did_edit();
        }
        return true;
    }
    case KeyCode::Left:
        impl_->edit.cursor = !extend && has_selection() ? selection_start() : previous();
        break;
    case KeyCode::Right:
        impl_->edit.cursor = !extend && has_selection() ? selection_end() : next();
        break;
    case KeyCode::Up:
    case KeyCode::Down:
    case KeyCode::PageUp:
    case KeyCode::PageDown: {
        const auto current = line_at(impl_->edit.text, impl_->edit.cursor);
        if (!impl_->preferred_x) {
            impl_->preferred_x =
                measure_text(current.text.substr(0, impl_->edit.cursor - current.start),
                             text_area_font())
                    .width;
        }
        const bool up = event.key == KeyCode::Up || event.key == KeyCode::PageUp;
        const bool page = event.key == KeyCode::PageUp || event.key == KeyCode::PageDown;
        const auto count = page ? static_cast<std::size_t>(
                                      std::clamp(std::floor(text_rect().height / line_height()),
                                                 1.0F,
                                                 static_cast<float>(impl_->line_starts.size())))
                                : 1;
        const auto row = cursor_line();
        const auto target_row = up ? row - std::min(count, row)
                                   : row + std::min(count, impl_->line_starts.size() - 1 - row);
        const auto target = line_at(impl_->edit.text, impl_->line_starts[target_row]);
        impl_->edit.move_cursor(target.start + position_at_x(target.text, *impl_->preferred_x),
                                extend);
        did_select();
        return true;
    }
    case KeyCode::Home:
        impl_->edit.cursor = document ? 0 : line_at(impl_->edit.text, impl_->edit.cursor).start;
        break;
    case KeyCode::End: {
        const auto line = line_at(impl_->edit.text, impl_->edit.cursor);
        impl_->edit.cursor = document ? impl_->edit.text.size() : line.start + line.text.size();
        break;
    }
    default:
        return false;
    }
    impl_->edit.move_cursor(impl_->edit.cursor, extend);
    impl_->preferred_x.reset();
    did_select();
    return true;
}

bool TextArea::handle_text_input_event(const TextInputEvent& event) {
    if (event.type == TextInputEvent::Type::ClearPreedit) {
        clear_preedit();
        return true;
    }
    if (!impl_->editable) {
        return false;
    }
    switch (event.type) {
    case TextInputEvent::Type::Preedit:
        impl_->selecting = false;
        impl_->edit.set_preedit(event.text, event.selection_start, event.selection_end);
        refresh_content_metrics();
        ensure_caret_visible();
        queue_redraw();
        return true;
    case TextInputEvent::Type::Commit:
        if (event.text.empty()) {
            return clear_preedit();
        }
        return replace_selection(
            event.text, !impl_->edit.has_preedit() && event.text.find('\n') == std::string::npos);
    case TextInputEvent::Type::DeleteSurrounding:
        if (impl_->edit.delete_surrounding(event.delete_before_length, event.delete_after_length)) {
            did_edit();
            return true;
        }
        return false;
    case TextInputEvent::Type::ClearPreedit:
        return true;
    }
    return false;
}

CursorShape TextArea::cursor_shape() const {
    return impl_->editable ? CursorShape::IBeam : CursorShape::Default;
}

void TextArea::on_focus_changed(bool focused) {
    impl_->focused_state = focused;
    // Window sets Pressed before assigning pointer focus. Keep that viewport
    // intact until the ensuing press places the caret in the clicked line.
    if (focused && !has_flag(state_flags(), StateFlags::Pressed)) {
        ensure_caret_visible();
    } else if (!focused) {
        clear_preedit();
        impl_->selecting = false;
        impl_->edit.break_undo_group();
        impl_->preferred_x.reset();
    }
    queue_redraw();
}

void TextArea::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 8.0F);
    const auto font = text_area_font();
    auto body = a;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a, theme_color("focus-ring-color"), corner_radius + 2.0F);
        body = {a.x + 2.0F,
                a.y + 2.0F,
                std::max(0.0F, a.width - 4.0F),
                std::max(0.0F, a.height - 4.0F)};
    }
    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    const auto viewport = text_rect();
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return;
    }
    ctx.push_rounded_clip(viewport, 0.0F);
    if (impl_->display_text.empty() && !impl_->placeholder.empty()) {
        ctx.add_text(
            {viewport.x, viewport.y}, impl_->placeholder, theme_color("placeholder-color"), font);
    } else {
        const float height = line_height();
        const auto first =
            static_cast<std::size_t>(std::max(0.0F, std::floor(impl_->scroll_y / height)));
        const auto selection_base = impl_->edit.has_preedit() ? selection_start() : 0;
        const auto selected_start =
            impl_->edit.has_preedit()
                ? selection_base + std::min(impl_->edit.preedit_selection_start,
                                            impl_->edit.preedit_selection_end)
                : selection_start();
        const auto selected_end =
            impl_->edit.has_preedit()
                ? selection_base + std::max(impl_->edit.preedit_selection_start,
                                            impl_->edit.preedit_selection_end)
                : selection_end();
        for (std::size_t row = first; row < impl_->display_line_starts.size(); ++row) {
            const float y = viewport.y + static_cast<float>(row) * height - impl_->scroll_y;
            if (y >= viewport.bottom()) {
                break;
            }
            const auto line = line_at(impl_->display_text, impl_->display_line_starts[row]);
            const auto line_end = line.start + line.text.size();
            if (selected_start != selected_end && selected_start <= line_end &&
                selected_end > line.start) {
                const auto start = std::clamp(selected_start, line.start, line_end) - line.start;
                const auto end = std::clamp(selected_end, line.start, line_end) - line.start;
                const float left = measure_text(line.text.substr(0, start), font).width;
                float right = measure_text(line.text.substr(0, end), font).width;
                if (selected_end > line_end && line_end < impl_->display_text.size()) {
                    right += measure_text(" ", font).width;
                }
                ctx.add_color_rect(
                    {viewport.x + left - impl_->scroll_x, y, std::max(0.0F, right - left), height},
                    theme_color("selection-background-color", Color{0.3F, 0.56F, 0.9F, 0.24F}));
            }
            if (impl_->edit.has_preedit()) {
                const auto preedit_start = selection_start();
                const auto preedit_end = preedit_start + impl_->edit.preedit_text.size();
                const auto start = std::clamp(preedit_start, line.start, line_end) - line.start;
                const auto end = std::clamp(preedit_end, line.start, line_end) - line.start;
                const auto left = measure_text(line.text.substr(0, start), font).width;
                const auto right = measure_text(line.text.substr(0, end), font).width;
                if (end > start) {
                    ctx.add_color_rect({viewport.x + left - impl_->scroll_x,
                                        y + height - 2.0F,
                                        std::max(0.0F, right - left),
                                        1.5F},
                                       theme_color("caret-color"));
                }
            }
            ctx.add_text({viewport.x - impl_->scroll_x, y},
                         std::string(line.text),
                         theme_color("text-color"),
                         font);
        }
    }
    // Empty editors, including those with placeholders, still show the caret.
    if (impl_->focused_state) {
        auto caret = text_input_caret_rect();
        caret.y += std::min(2.0F, caret.height * 0.25F);
        caret.height = std::max(1.0F, caret.height - 4.0F);
        ctx.add_color_rect(caret, theme_color("caret-color"));
    }
    ctx.pop_container();
}

} // namespace nk
