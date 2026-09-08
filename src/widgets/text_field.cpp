#include "../text/text_boundaries.h"
#include "../text/text_clipboard.h"
#include "../text/text_edit_buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nk/actions/shortcut.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/spell_checker.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/text_field.h>
#include <optional>
#include <vector>

namespace nk {

namespace {

using detail::decode_utf8_units;
using detail::grapheme_boundaries;
using detail::nearest_grapheme_boundary;
using detail::next_grapheme_boundary;
using detail::next_word_boundary;
using detail::previous_grapheme_boundary;
using detail::previous_word_boundary;
using detail::word_selection_range;

FontDescriptor text_field_font() {
    const float scale = Application::instance() != nullptr
                            ? Application::instance()->system_preferences().text_scale_factor
                            : 1.0F;
    return FontDescriptor{
        .family = {},
        .size = 13.5F * scale,
        .weight = FontWeight::Regular,
    };
}

bool has_shift(Modifiers modifiers) {
    return (modifiers & Modifiers::Shift) == Modifiers::Shift;
}

bool has_primary_shortcut(Modifiers modifiers) {
    return (modifiers & Modifiers::Super) == Modifiers::Super ||
           (modifiers & Modifiers::Ctrl) == Modifiers::Ctrl;
}

std::string default_text_field_accessible_name(std::string_view placeholder) {
    return placeholder.empty() ? "Text field" : std::string(placeholder);
}

std::optional<char> key_to_ascii(const KeyEvent& event) {
    const bool shift = has_shift(event.modifiers);
    auto key = static_cast<uint16_t>(event.key);

    if (key >= static_cast<uint16_t>(KeyCode::A) && key <= static_cast<uint16_t>(KeyCode::Z)) {
        const char base = shift ? 'A' : 'a';
        return static_cast<char>(base + (key - static_cast<uint16_t>(KeyCode::A)));
    }

    switch (event.key) {
    case KeyCode::Num1:
        return shift ? '!' : '1';
    case KeyCode::Num2:
        return shift ? '@' : '2';
    case KeyCode::Num3:
        return shift ? '#' : '3';
    case KeyCode::Num4:
        return shift ? '$' : '4';
    case KeyCode::Num5:
        return shift ? '%' : '5';
    case KeyCode::Num6:
        return shift ? '^' : '6';
    case KeyCode::Num7:
        return shift ? '&' : '7';
    case KeyCode::Num8:
        return shift ? '*' : '8';
    case KeyCode::Num9:
        return shift ? '(' : '9';
    case KeyCode::Num0:
        return shift ? ')' : '0';
    case KeyCode::Space:
        return ' ';
    case KeyCode::Minus:
        return shift ? '_' : '-';
    case KeyCode::Equals:
        return shift ? '+' : '=';
    case KeyCode::LeftBracket:
        return shift ? '{' : '[';
    case KeyCode::RightBracket:
        return shift ? '}' : ']';
    case KeyCode::Backslash:
        return shift ? '|' : '\\';
    case KeyCode::Semicolon:
        return shift ? ':' : ';';
    case KeyCode::Apostrophe:
        return shift ? '"' : '\'';
    case KeyCode::Grave:
        return shift ? '~' : '`';
    case KeyCode::Comma:
        return shift ? '<' : ',';
    case KeyCode::Period:
        return shift ? '>' : '.';
    case KeyCode::Slash:
        return shift ? '?' : '/';
    default:
        return std::nullopt;
    }
}

} // namespace

struct TextField::Impl {
    using HistoryGroup = detail::EditGroup;
    detail::TextEditBuffer edit;
    std::string placeholder;
    bool editable = true;
    bool selecting_with_mouse = false;
    bool selecting_word_with_mouse = false;
    bool selecting_line_with_mouse = false;
    std::size_t mouse_selection_base_start = 0;
    std::size_t mouse_selection_base_end = 0;
    float scroll_x = 0.0F;
    HistoryGroup history_group = HistoryGroup::None;
    bool spell_check_enabled = false;
    bool secure_text_entry = false;
    mutable std::string spell_check_cache_text;
    mutable bool spell_check_cache_valid = false;
    mutable std::vector<SpellCheckRange> spell_check_ranges;
    Signal<std::string_view> text_changed;
    Signal<> activate;
};

std::shared_ptr<TextField> TextField::create(std::string initial_text) {
    return std::shared_ptr<TextField>(new TextField(std::move(initial_text)));
}

TextField::TextField(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->edit.text = std::move(text);
    impl_->edit.cursor = impl_->edit.text.size();
    impl_->edit.selection_anchor = impl_->edit.cursor;
    set_focusable(true);
    add_style_class("text-field");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TextInput);
    accessible.set_name(default_text_field_accessible_name(impl_->placeholder));
    accessible.set_value(impl_->edit.text);
    reset_history();
}

TextField::~TextField() = default;

std::string_view TextField::text() const {
    return impl_->edit.text;
}

void TextField::set_text(std::string text) {
    if (impl_->edit.text != text) {
        reset_mouse_selection_state();
        impl_->edit.text = std::move(text);
        ensure_accessible().set_value(impl_->edit.text);
        impl_->edit.cursor = impl_->edit.text.size();
        impl_->edit.selection_anchor = impl_->edit.cursor;
        clear_preedit();
        impl_->scroll_x = 0.0F;
        ensure_caret_visible();
        sync_primary_selection_ownership();
        reset_history();
        impl_->text_changed.emit(impl_->edit.text);
        queue_text_redraw();
    }
}

std::string_view TextField::placeholder() const {
    return impl_->placeholder;
}

void TextField::set_placeholder(std::string placeholder) {
    impl_->placeholder = std::move(placeholder);
    ensure_accessible().set_description(impl_->placeholder);
    ensure_accessible().set_name(default_text_field_accessible_name(impl_->placeholder));
    queue_text_redraw();
}

bool TextField::is_editable() const {
    return impl_->editable;
}

void TextField::set_editable(bool editable) {
    if (impl_->editable == editable) {
        return;
    }
    impl_->editable = editable;
    if (!editable) {
        clear_preedit();
    }
    ensure_caret_visible();
    queue_redraw();
}

std::size_t TextField::cursor_position() const {
    return impl_->edit.cursor;
}

std::size_t TextField::selection_start() const {
    return impl_->edit.selection_start();
}

std::size_t TextField::selection_end() const {
    return impl_->edit.selection_end();
}

bool TextField::has_selection() const {
    return impl_->edit.has_selection();
}

void TextField::select_all() {
    clear_preedit();
    reset_history_grouping();
    impl_->edit.select_all();
    sync_primary_selection_ownership();
    ensure_caret_visible();
    queue_text_redraw();
}

void TextField::set_spell_check_enabled(bool enabled) {
    if (impl_->spell_check_enabled == enabled) {
        return;
    }
    impl_->spell_check_enabled = enabled;
    impl_->spell_check_cache_valid = false;
    impl_->spell_check_ranges.clear();
    queue_redraw();
}

bool TextField::is_spell_check_enabled() const {
    return impl_->spell_check_enabled;
}

void TextField::set_secure_text_entry(bool secure) {
    if (impl_->secure_text_entry == secure) {
        return;
    }
    impl_->secure_text_entry = secure;
    queue_text_redraw();
}

bool TextField::is_secure_text_entry() const {
    return impl_->secure_text_entry;
}

Signal<std::string_view>& TextField::on_text_changed() {
    return impl_->text_changed;
}

Signal<>& TextField::on_activate() {
    return impl_->activate;
}

SizeRequest TextField::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("min-width", 240.0F);
    const float scale = Application::instance() != nullptr
                            ? Application::instance()->system_preferences().text_scale_factor
                            : 1.0F;
    const float h = theme_number("min-height", 36.0F) * scale;
    return {120.0F, h, w, h};
}

void TextField::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    ensure_caret_visible();
}

bool TextField::handle_mouse_event(const MouseEvent& event) {
    const auto point = Point{event.x, event.y};

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button != 1 || !allocation().contains(point)) {
            if (event.button == 3 && allocation().contains(point)) {
                clear_preedit();
                reset_history_grouping();
                return impl_->editable && paste_from_primary_selection(hit_test_cursor(point));
            }
            return false;
        }
        clear_preedit();
        reset_history_grouping();
        impl_->selecting_word_with_mouse = false;
        impl_->selecting_line_with_mouse = false;
        if (event.click_count >= 3) {
            impl_->selecting_with_mouse = true;
            impl_->selecting_line_with_mouse = true;
            impl_->edit.selection_anchor = 0;
            impl_->edit.cursor = impl_->edit.text.size();
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        if (event.click_count >= 2) {
            const auto [start, end] =
                word_selection_range(impl_->edit.text, hit_test_cursor(point));
            impl_->selecting_with_mouse = true;
            impl_->selecting_word_with_mouse = true;
            impl_->mouse_selection_base_start = start;
            impl_->mouse_selection_base_end = end;
            impl_->edit.selection_anchor = start;
            impl_->edit.cursor = end;
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        impl_->selecting_with_mouse = true;
        move_cursor(hit_test_cursor(point), has_shift(event.modifiers));
        return true;
    case MouseEvent::Type::Move:
        if (!impl_->selecting_with_mouse) {
            return false;
        }
        if (impl_->selecting_line_with_mouse) {
            impl_->edit.selection_anchor = 0;
            impl_->edit.cursor = impl_->edit.text.size();
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        if (impl_->selecting_word_with_mouse) {
            const auto [start, end] =
                word_selection_range(impl_->edit.text, hit_test_cursor(point));
            if (end <= impl_->mouse_selection_base_start) {
                impl_->edit.selection_anchor = impl_->mouse_selection_base_end;
                impl_->edit.cursor = start;
            } else {
                impl_->edit.selection_anchor = impl_->mouse_selection_base_start;
                impl_->edit.cursor = end;
            }
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        move_cursor(hit_test_cursor(point), true);
        return true;
    case MouseEvent::Type::Release:
        if (event.button != 1 || !impl_->selecting_with_mouse) {
            return false;
        }
        if (impl_->selecting_line_with_mouse) {
            impl_->edit.selection_anchor = 0;
            impl_->edit.cursor = impl_->edit.text.size();
            impl_->selecting_with_mouse = false;
            impl_->selecting_line_with_mouse = false;
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        if (impl_->selecting_word_with_mouse) {
            const auto [start, end] =
                word_selection_range(impl_->edit.text, hit_test_cursor(point));
            if (end <= impl_->mouse_selection_base_start) {
                impl_->edit.selection_anchor = impl_->mouse_selection_base_end;
                impl_->edit.cursor = start;
            } else {
                impl_->edit.selection_anchor = impl_->mouse_selection_base_start;
                impl_->edit.cursor = end;
            }
            impl_->selecting_with_mouse = false;
            impl_->selecting_word_with_mouse = false;
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        impl_->selecting_with_mouse = false;
        move_cursor(hit_test_cursor(point), true);
        return true;
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
    case MouseEvent::Type::DragStart:
    case MouseEvent::Type::DragUpdate:
    case MouseEvent::Type::DragEnd:
        return false;
    }

    return false;
}

bool TextField::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Escape && has_preedit()) {
        clear_preedit();
        return true;
    }
    if (event.key == KeyCode::Return) {
        if (has_preedit()) {
            return true;
        }
        reset_history_grouping();
        impl_->activate.emit();
        return true;
    }

    if (has_primary_shortcut(event.modifiers) &&
        (event.modifiers & Modifiers::Alt) != Modifiers::Alt) {
        switch (event.key) {
        case KeyCode::A:
            select_all();
            return true;
        case KeyCode::C:
            copy_selection_to_clipboard();
            return has_selection();
        case KeyCode::X:
            if (!impl_->editable || !has_selection()) {
                return false;
            }
            reset_history_grouping();
            copy_selection_to_clipboard();
            replace_selection({});
            return true;
        case KeyCode::V:
            return impl_->editable && paste_from_clipboard();
        case KeyCode::Z:
            if ((event.modifiers & Modifiers::Shift) == Modifiers::Shift) {
                return redo();
            }
            return undo();
        case KeyCode::Y:
            return redo();
        default:
            break;
        }
    }

    switch (event.key) {
    case KeyCode::Left:
        clear_preedit();
        reset_history_grouping();
        if (!has_shift(event.modifiers) && has_selection()) {
            move_cursor(selection_start(), false);
        } else if ((event.modifiers & Modifiers::Alt) == Modifiers::Alt && impl_->edit.cursor > 0) {
            move_cursor(previous_word_boundary(impl_->edit.text, impl_->edit.cursor),
                        has_shift(event.modifiers));
        } else if (impl_->edit.cursor > 0) {
            move_cursor(previous_grapheme_boundary(impl_->edit.text, impl_->edit.cursor),
                        has_shift(event.modifiers));
        }
        return true;
    case KeyCode::Right:
        clear_preedit();
        reset_history_grouping();
        if (!has_shift(event.modifiers) && has_selection()) {
            move_cursor(selection_end(), false);
        } else if ((event.modifiers & Modifiers::Alt) == Modifiers::Alt &&
                   impl_->edit.cursor < impl_->edit.text.size()) {
            move_cursor(next_word_boundary(impl_->edit.text, impl_->edit.cursor),
                        has_shift(event.modifiers));
        } else if (impl_->edit.cursor < impl_->edit.text.size()) {
            move_cursor(next_grapheme_boundary(impl_->edit.text, impl_->edit.cursor),
                        has_shift(event.modifiers));
        }
        return true;
    case KeyCode::Home:
        clear_preedit();
        reset_history_grouping();
        move_cursor(0, has_shift(event.modifiers));
        return true;
    case KeyCode::End:
        clear_preedit();
        reset_history_grouping();
        move_cursor(impl_->edit.text.size(), has_shift(event.modifiers));
        return true;
    case KeyCode::Backspace:
        clear_preedit();
        return impl_->editable &&
               (((event.modifiers & Modifiers::Alt) == Modifiers::Alt) ? delete_backward_word()
                                                                       : delete_backward());
    case KeyCode::Delete:
        clear_preedit();
        return impl_->editable &&
               (((event.modifiers & Modifiers::Alt) == Modifiers::Alt) ? delete_forward_word()
                                                                       : delete_forward());
    default:
        break;
    }

    if (!impl_->editable || has_primary_shortcut(event.modifiers) ||
        (event.modifiers & Modifiers::Alt) == Modifiers::Alt) {
        return false;
    }

    auto character = key_to_ascii(event);
    if (!character.has_value()) {
        reset_history_grouping();
        return false;
    }

    std::string inserted(1, *character);
    impl_->history_group = Impl::HistoryGroup::Insert;
    replace_selection(inserted, true);
    return true;
}

bool TextField::handle_text_input_event(const TextInputEvent& event) {
    switch (event.type) {
    case TextInputEvent::Type::ClearPreedit:
        clear_preedit();
        return true;
    case TextInputEvent::Type::Preedit:
        if (!impl_->editable) {
            return false;
        }
        reset_history_grouping();
        impl_->edit.set_preedit(event.text, event.selection_start, event.selection_end);
        ensure_caret_visible();
        queue_text_redraw();
        return true;
    case TextInputEvent::Type::Commit:
        if (!impl_->editable || event.text.empty()) {
            const bool canceled = has_preedit();
            clear_preedit();
            return canceled;
        }
        impl_->history_group = event.text.size() == 1 && !has_selection()
                                   ? Impl::HistoryGroup::Insert
                                   : Impl::HistoryGroup::None;
        replace_selection(event.text, event.text.size() == 1 && !has_selection());
        return true;
    case TextInputEvent::Type::DeleteSurrounding:
        if (!impl_->editable) {
            return false;
        }
        return delete_surrounding_text(event.delete_before_length, event.delete_after_length);
    }

    return false;
}

std::optional<WidgetTextInputState> TextField::text_input_state() const {
    if (!is_editable()) {
        return std::nullopt;
    }

    const auto caret_rect = text_input_caret_rect();
    if (!caret_rect.has_value()) {
        return std::nullopt;
    }

    WidgetTextInputState state{};
    state.text = std::string(text());
    state.cursor = cursor_position();
    if (has_selection()) {
        if (state.cursor == selection_start()) {
            state.anchor = selection_end();
        } else {
            state.anchor = selection_start();
        }
    } else {
        state.anchor = state.cursor;
    }
    state.caret_rect = *caret_rect;
    return state;
}

std::optional<Rect> TextField::text_input_caret_rect() const {
    const auto text_bounds = text_rect();
    const auto font = text_field_font();
    const auto display_text = composed_display_text();
    const float caret_x =
        text_bounds.x + measure_text(display_text.substr(0, display_caret_position()), font).width -
        impl_->scroll_x;
    return Rect{
        caret_x,
        text_bounds.y + 4.0F,
        1.5F,
        std::max(0.0F, text_bounds.height - 8.0F),
    };
}

CursorShape TextField::cursor_shape() const {
    return impl_->editable ? CursorShape::IBeam : CursorShape::Default;
}

void TextField::on_focus_changed(bool focused) {
    if (!focused) {
        impl_->selecting_with_mouse = false;
        impl_->selecting_line_with_mouse = false;
        clear_preedit();
        reset_history_grouping();
    }
    ensure_caret_visible();
}

void TextField::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto body = inner_body_rect();
    const float corner_radius = theme_number("corner-radius", 10.0F);

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a, theme_color("focus-ring-color"), corner_radius + 2.0F);
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    snapshot_text(ctx);
}

void TextField::snapshot_text(SnapshotContext& ctx) const {
    const auto text_bounds = text_rect();
    if (text_bounds.width <= 0.0F || text_bounds.height <= 0.0F) {
        return;
    }
    ctx.push_rounded_clip(text_bounds, 0.0F);
    const auto display_text = composed_display_text();
    Color text_color = impl_->edit.text.empty() && !has_preedit() ? theme_color("placeholder-color")
                                                                  : theme_color("text-color");
    if (!display_text.empty()) {
        const auto font = text_field_font();
        const auto measured = measure_text(display_text, font);
        const float text_y =
            text_bounds.y + std::max(0.0F, (text_bounds.height - measured.height) * 0.5F);

        const auto paint_offset = [&](std::size_t position) {
            return impl_->secure_text_entry
                       ? decode_utf8_units(impl_->edit.display_text().substr(0, position)).size() *
                             3
                       : position;
        };
        const auto selection_base = has_preedit() ? selection_start() : 0;
        const auto selection_start_pos =
            paint_offset(has_preedit() ? selection_base + impl_->edit.preedit_selection_start
                                       : selection_start());
        const auto selection_end_pos = paint_offset(
            has_preedit() ? selection_base + impl_->edit.preedit_selection_end : selection_end());
        const auto left_pos = std::min(selection_start_pos, selection_end_pos);
        const auto right_pos = std::max(selection_start_pos, selection_end_pos);
        if (left_pos != right_pos) {
            const auto left = measure_text(display_text.substr(0, left_pos), font).width;
            const auto right = measure_text(display_text.substr(0, right_pos), font).width;
            ctx.add_rounded_rect(
                {text_bounds.x + left - impl_->scroll_x,
                 text_bounds.y + 2.0F,
                 std::max(0.0F, right - left),
                 std::max(0.0F, text_bounds.height - 4.0F)},
                theme_color("selection-background-color", Color{0.3F, 0.56F, 0.9F, 0.24F}),
                6.0F);
        }

        ctx.add_text({text_bounds.x - impl_->scroll_x, text_y}, display_text, text_color, font);

        if (impl_->spell_check_enabled && impl_->edit.preedit_text.empty() &&
            !impl_->edit.text.empty()) {
            if (!impl_->spell_check_cache_valid ||
                impl_->spell_check_cache_text != impl_->edit.text) {
                impl_->spell_check_ranges.clear();
                if (auto* app = Application::instance(); app != nullptr) {
                    if (auto* checker = app->platform_backend().spell_checker();
                        checker != nullptr) {
                        impl_->spell_check_ranges = checker->check(impl_->edit.text);
                    }
                }
                impl_->spell_check_cache_text = impl_->edit.text;
                impl_->spell_check_cache_valid = true;
            }

            const Color misspelling_color =
                theme_color("misspelling-underline-color", Color{0.92F, 0.25F, 0.25F, 1.0F});
            const float underline_y = text_bounds.bottom() - 2.0F;
            for (const auto& range : impl_->spell_check_ranges) {
                if (range.length == 0 || range.start + range.length > impl_->edit.text.size()) {
                    continue;
                }
                const float left =
                    measure_text(impl_->edit.text.substr(0, range.start), font).width;
                const float width =
                    measure_text(impl_->edit.text.substr(range.start, range.length), font).width;
                ctx.add_color_rect(
                    {text_bounds.x + left - impl_->scroll_x, underline_y, width, 1.5F},
                    misspelling_color);
            }
        }

        if (has_preedit()) {
            const auto start = paint_offset(selection_start());
            const auto end = paint_offset(selection_start() + impl_->edit.preedit_text.size());
            const auto left = measure_text(display_text.substr(0, start), font).width;
            const auto right = measure_text(display_text.substr(0, end), font).width;
            ctx.add_color_rect({text_bounds.x + left - impl_->scroll_x,
                                text_bounds.bottom() - 3.0F,
                                std::max(0.0F, right - left),
                                1.5F},
                               theme_color("caret-color", text_color));
        }
        if (has_flag(state_flags(), StateFlags::Focused)) {
            const auto caret_x =
                text_bounds.x +
                measure_text(display_text.substr(0, display_caret_position()), font).width -
                impl_->scroll_x;
            ctx.add_color_rect(
                {caret_x, text_bounds.y + 4.0F, 1.5F, std::max(0.0F, text_bounds.height - 8.0F)},
                theme_color("caret-color", text_color));
        }
    } else if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_color_rect(
            {text_bounds.x, text_bounds.y + 4.0F, 1.5F, std::max(0.0F, text_bounds.height - 8.0F)},
            theme_color("caret-color", text_color));
    }
    ctx.pop_container();
}

Rect TextField::inner_body_rect() const {
    auto body = allocation();
    if (has_flag(state_flags(), StateFlags::Focused)) {
        body = {body.x + 2.0F,
                body.y + 2.0F,
                std::max(0.0F, body.width - 4.0F),
                std::max(0.0F, body.height - 4.0F)};
    }
    return body;
}

Rect TextField::text_rect() const {
    const auto body = allocation();
    return {body.x + 12.0F,
            body.y + 4.0F,
            std::max(0.0F, body.width - 24.0F),
            std::max(0.0F, body.height - 8.0F)};
}

Rect TextField::local_text_damage_rect() const {
    const auto a = allocation();
    const auto text_bounds = text_rect();
    if (a.width <= 0.0F || a.height <= 0.0F || text_bounds.width <= 0.0F ||
        text_bounds.height <= 0.0F) {
        return {};
    }

    const float margin = 2.0F;
    const float x = std::max(0.0F, (text_bounds.x - a.x) - margin);
    const float y = std::max(0.0F, (text_bounds.y - a.y) - margin);
    const float right = std::min(a.width, (text_bounds.right() - a.x) + margin);
    const float bottom = std::min(a.height, (text_bounds.bottom() - a.y) + margin);
    return {x, y, std::max(0.0F, right - x), std::max(0.0F, bottom - y)};
}

void TextField::queue_text_redraw() {
    const auto damage = local_text_damage_rect();
    if (damage.width <= 0.0F || damage.height <= 0.0F) {
        queue_redraw();
    } else {
        queue_redraw(damage);
    }
}

std::size_t TextField::hit_test_cursor(Point point) const {
    const auto bounds = text_rect();
    const auto font = text_field_font();
    const float local_x = std::max(0.0F, point.x - bounds.x + impl_->scroll_x);
    const auto boundaries = grapheme_boundaries(impl_->edit.text);

    std::size_t best_index = 0;
    float best_distance = std::numeric_limits<float>::infinity();
    for (const auto index : boundaries) {
        const float caret_x = measure_text(impl_->edit.text.substr(0, index), font).width;
        const float distance = std::fabs(caret_x - local_x);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }
    return best_index;
}

void TextField::move_cursor(std::size_t position, bool extend_selection) {
    position =
        nearest_grapheme_boundary(impl_->edit.text, std::min(position, impl_->edit.text.size()));
    impl_->edit.move_cursor(position, extend_selection);
    sync_primary_selection_ownership();
    ensure_caret_visible();
    queue_text_redraw();
}

void TextField::replace_selection(std::string_view text, bool coalesce_history) {
    replace_range(selection_start(), selection_end(), text, coalesce_history);
}

void TextField::replace_range(std::size_t start,
                              std::size_t end,
                              std::string_view text,
                              bool coalesce_history) {
    reset_mouse_selection_state();
    const auto group =
        coalesce_history && !has_preedit() ? impl_->history_group : Impl::HistoryGroup::None;
    clear_preedit();
    if (!impl_->edit.replace(start, end, text, group)) {
        return;
    }
    ensure_caret_visible();
    sync_primary_selection_ownership();
    ensure_accessible().set_value(impl_->edit.text);
    impl_->text_changed.emit(impl_->edit.text);
    queue_text_redraw();
}

void TextField::ensure_caret_visible() {
    const auto bounds = text_rect();
    const auto font = text_field_font();
    const float content_width = std::max(0.0F, bounds.width);
    const auto display_text = composed_display_text();
    const float caret_x =
        measure_text(display_text.substr(0, display_caret_position()), font).width;
    const float total_width = measure_text(display_text, font).width;
    // Keep the full caret inside the clip even at the end of a long query.
    const float max_scroll = std::max(0.0F, total_width + 2.0F - content_width);

    if (caret_x < impl_->scroll_x) {
        impl_->scroll_x = caret_x;
    } else if (caret_x > impl_->scroll_x + content_width - 2.0F) {
        impl_->scroll_x = caret_x - std::max(0.0F, content_width - 2.0F);
    }

    impl_->scroll_x = std::clamp(impl_->scroll_x, 0.0F, max_scroll);
}

void TextField::reset_history_grouping() {
    impl_->history_group = Impl::HistoryGroup::None;
    impl_->edit.break_undo_group();
}

void TextField::reset_history() {
    impl_->edit.reset_history();
    reset_history_grouping();
}

bool TextField::undo() {
    if (!impl_->editable) {
        return false;
    }
    const bool canceled = has_preedit();
    clear_preedit();
    if (!impl_->edit.undo()) {
        return canceled;
    }
    reset_mouse_selection_state();
    reset_history_grouping();
    sync_primary_selection_ownership();
    ensure_caret_visible();
    ensure_accessible().set_value(impl_->edit.text);
    impl_->text_changed.emit(impl_->edit.text);
    queue_text_redraw();
    return true;
}

bool TextField::redo() {
    if (!impl_->editable) {
        return false;
    }
    const bool canceled = has_preedit();
    clear_preedit();
    if (!impl_->edit.redo()) {
        return canceled;
    }
    reset_mouse_selection_state();
    reset_history_grouping();
    sync_primary_selection_ownership();
    ensure_caret_visible();
    ensure_accessible().set_value(impl_->edit.text);
    impl_->text_changed.emit(impl_->edit.text);
    queue_text_redraw();
    return true;
}

void TextField::copy_selection_to_clipboard() const {
    if (!has_selection()) {
        return;
    }

    const auto selected =
        impl_->edit.text.substr(selection_start(), selection_end() - selection_start());
    detail::write_text_clipboard(selected);
}

void TextField::sync_primary_selection_ownership() const {
    std::string selected;
    if (has_selection()) {
        selected = impl_->edit.text.substr(selection_start(), selection_end() - selection_start());
    }

    detail::write_text_clipboard(std::move(selected), true);
}

bool TextField::delete_backward() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({});
        return true;
    }
    if (impl_->edit.cursor == 0) {
        return false;
    }

    impl_->history_group = Impl::HistoryGroup::DeleteBackward;
    replace_range(previous_grapheme_boundary(impl_->edit.text, impl_->edit.cursor),
                  impl_->edit.cursor,
                  {},
                  true);
    return true;
}

bool TextField::delete_forward() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({});
        return true;
    }
    if (impl_->edit.cursor >= impl_->edit.text.size()) {
        return false;
    }

    impl_->history_group = Impl::HistoryGroup::DeleteForward;
    replace_range(
        impl_->edit.cursor, next_grapheme_boundary(impl_->edit.text, impl_->edit.cursor), {}, true);
    return true;
}

bool TextField::delete_backward_word() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({});
        return true;
    }
    if (impl_->edit.cursor == 0) {
        return false;
    }

    reset_history_grouping();
    replace_range(
        previous_word_boundary(impl_->edit.text, impl_->edit.cursor), impl_->edit.cursor, {});
    return true;
}

bool TextField::delete_forward_word() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({});
        return true;
    }
    if (impl_->edit.cursor >= impl_->edit.text.size()) {
        return false;
    }

    reset_history_grouping();
    replace_range(impl_->edit.cursor, next_word_boundary(impl_->edit.text, impl_->edit.cursor), {});
    return true;
}

bool TextField::paste_from_clipboard() {
    const auto text = detail::read_text_clipboard();

    if (text.empty()) {
        return false;
    }

    reset_history_grouping();
    replace_selection(text);
    return true;
}

bool TextField::paste_from_primary_selection(std::optional<std::size_t> cursor_position) {
    const auto text = detail::read_text_clipboard(true);

    if (text.empty()) {
        return false;
    }

    reset_history_grouping();
    if (cursor_position.has_value()) {
        reset_mouse_selection_state();
        const auto bounded = nearest_grapheme_boundary(
            impl_->edit.text, std::min(*cursor_position, impl_->edit.text.size()));
        impl_->edit.cursor = bounded;
        impl_->edit.selection_anchor = bounded;
    }
    replace_selection(text);
    return true;
}

bool TextField::has_preedit() const {
    return impl_->edit.has_preedit();
}

void TextField::clear_preedit() {
    if (impl_->edit.clear_preedit()) {
        ensure_caret_visible();
        queue_text_redraw();
    }
}

void TextField::reset_mouse_selection_state() {
    impl_->selecting_with_mouse = false;
    impl_->selecting_word_with_mouse = false;
    impl_->selecting_line_with_mouse = false;
}

bool TextField::delete_surrounding_text(std::size_t before_length, std::size_t after_length) {
    if (!impl_->edit.delete_surrounding(before_length, after_length)) {
        return false;
    }
    reset_mouse_selection_state();
    reset_history_grouping();
    sync_primary_selection_ownership();
    ensure_caret_visible();
    ensure_accessible().set_value(impl_->edit.text);
    impl_->text_changed.emit(impl_->edit.text);
    queue_text_redraw();
    return true;
}

std::string TextField::composed_display_text() const {
    auto display = impl_->edit.display_text();
    if (impl_->secure_text_entry && !display.empty()) {
        const auto count = decode_utf8_units(display).size();
        display.clear();
        for (std::size_t i = 0; i < count; ++i) {
            display.append("\xE2\x80\xA2");
        }
    }
    return display.empty() ? impl_->placeholder : display;
}

std::size_t TextField::display_caret_position() const {
    const auto position = impl_->edit.display_caret_position();
    if (impl_->secure_text_entry) {
        return decode_utf8_units(impl_->edit.display_text().substr(0, position)).size() * 3;
    }
    return position;
}

} // namespace nk
