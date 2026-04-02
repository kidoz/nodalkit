#include <algorithm>
#include <cmath>
#include <limits>
#include <nk/actions/shortcut.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/text_field.h>
#include <optional>
#include <vector>

namespace nk {

namespace {

std::string& fallback_clipboard_text() {
    static std::string clipboard;
    return clipboard;
}

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
    struct HistoryState {
        std::string text;
        std::size_t cursor = 0;
        std::size_t selection_anchor = 0;
    };

    std::string text;
    std::string placeholder;
    bool editable = true;
    std::size_t cursor = 0;
    std::size_t selection_anchor = 0;
    bool selecting_with_mouse = false;
    float scroll_x = 0.0F;
    std::vector<HistoryState> history;
    std::size_t history_index = 0;
    Signal<std::string_view> text_changed;
    Signal<> activate;
};

std::shared_ptr<TextField> TextField::create(std::string initial_text) {
    return std::shared_ptr<TextField>(new TextField(std::move(initial_text)));
}

TextField::TextField(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    impl_->cursor = impl_->text.size();
    impl_->selection_anchor = impl_->cursor;
    set_focusable(true);
    add_style_class("text-field");
    reset_history();
}

TextField::~TextField() = default;

std::string_view TextField::text() const {
    return impl_->text;
}

void TextField::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
        impl_->cursor = impl_->text.size();
        impl_->selection_anchor = impl_->cursor;
        impl_->scroll_x = 0.0F;
        reset_history();
        impl_->text_changed.emit(impl_->text);
        queue_redraw();
    }
}

std::string_view TextField::placeholder() const {
    return impl_->placeholder;
}

void TextField::set_placeholder(std::string placeholder) {
    impl_->placeholder = std::move(placeholder);
    queue_redraw();
}

bool TextField::is_editable() const {
    return impl_->editable;
}

void TextField::set_editable(bool editable) {
    if (impl_->editable == editable) {
        return;
    }
    impl_->editable = editable;
    queue_redraw();
}

std::size_t TextField::cursor_position() const {
    return impl_->cursor;
}

std::size_t TextField::selection_start() const {
    return std::min(impl_->cursor, impl_->selection_anchor);
}

std::size_t TextField::selection_end() const {
    return std::max(impl_->cursor, impl_->selection_anchor);
}

bool TextField::has_selection() const {
    return impl_->cursor != impl_->selection_anchor;
}

void TextField::select_all() {
    impl_->selection_anchor = 0;
    impl_->cursor = impl_->text.size();
    ensure_caret_visible();
    queue_redraw();
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
            return false;
        }
        impl_->selecting_with_mouse = true;
        move_cursor(hit_test_cursor(point), has_shift(event.modifiers));
        return true;
    case MouseEvent::Type::Move:
        if (!impl_->selecting_with_mouse) {
            return false;
        }
        move_cursor(hit_test_cursor(point), true);
        return true;
    case MouseEvent::Type::Release:
        if (event.button != 1 || !impl_->selecting_with_mouse) {
            return false;
        }
        impl_->selecting_with_mouse = false;
        move_cursor(hit_test_cursor(point), true);
        return true;
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool TextField::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Return) {
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
            copy_selection_to_clipboard();
            replace_selection({}, true);
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
        if (!has_shift(event.modifiers) && has_selection()) {
            move_cursor(selection_start(), false);
        } else if (impl_->cursor > 0) {
            move_cursor(impl_->cursor - 1, has_shift(event.modifiers));
        }
        return true;
    case KeyCode::Right:
        if (!has_shift(event.modifiers) && has_selection()) {
            move_cursor(selection_end(), false);
        } else if (impl_->cursor < impl_->text.size()) {
            move_cursor(impl_->cursor + 1, has_shift(event.modifiers));
        }
        return true;
    case KeyCode::Home:
        move_cursor(0, has_shift(event.modifiers));
        return true;
    case KeyCode::End:
        move_cursor(impl_->text.size(), has_shift(event.modifiers));
        return true;
    case KeyCode::Backspace:
        return impl_->editable && delete_backward();
    case KeyCode::Delete:
        return impl_->editable && delete_forward();
    default:
        break;
    }

    if (!impl_->editable || has_primary_shortcut(event.modifiers) ||
        (event.modifiers & Modifiers::Alt) == Modifiers::Alt) {
        return false;
    }

    auto character = key_to_ascii(event);
    if (!character.has_value()) {
        return false;
    }

    std::string inserted(1, *character);
    replace_selection(inserted, true);
    return true;
}

CursorShape TextField::cursor_shape() const {
    return impl_->editable ? CursorShape::IBeam : CursorShape::Default;
}

void TextField::on_focus_changed(bool focused) {
    if (!focused) {
        impl_->selecting_with_mouse = false;
    }
    ensure_caret_visible();
}

void TextField::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto body = inner_body_rect();
    const auto text_bounds = text_rect();
    const float corner_radius = theme_number("corner-radius", 10.0F);

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    const auto& display_text = impl_->text.empty() ? impl_->placeholder : impl_->text;
    Color text_color = impl_->text.empty()
                           ? theme_color("placeholder-color", Color{0.55F, 0.58F, 0.62F, 1.0F})
                           : theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    if (!display_text.empty()) {
        const auto font = text_field_font();
        const auto measured = measure_text(display_text, font);
        const float text_y =
            text_bounds.y + std::max(0.0F, (text_bounds.height - measured.height) * 0.5F);

        if (!impl_->text.empty() && has_selection()) {
            const auto selection_bg =
                theme_color("selection-background-color", Color{0.3F, 0.56F, 0.9F, 0.24F});
            const auto selection_left =
                measure_text(impl_->text.substr(0, selection_start()), font).width;
            const auto selection_width =
                measure_text(
                    impl_->text.substr(selection_start(), selection_end() - selection_start()),
                    font)
                    .width;
            ctx.add_rounded_rect({text_bounds.x + selection_left - impl_->scroll_x,
                                  text_bounds.y + 2.0F,
                                  selection_width,
                                  std::max(0.0F, text_bounds.height - 4.0F)},
                                 selection_bg,
                                 6.0F);
        }

        ctx.push_rounded_clip(body, corner_radius);
        ctx.add_text(
            {text_bounds.x - impl_->scroll_x, text_y}, std::string(display_text), text_color, font);
        if (has_flag(state_flags(), StateFlags::Focused)) {
            const auto caret_x = text_bounds.x +
                                 measure_text(impl_->text.substr(0, impl_->cursor), font).width -
                                 impl_->scroll_x;
            ctx.add_color_rect(
                {caret_x, text_bounds.y + 4.0F, 1.5F, std::max(0.0F, text_bounds.height - 8.0F)},
                theme_color("caret-color", text_color));
        }
        ctx.pop_container();
    } else if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_color_rect(
            {text_bounds.x, text_bounds.y + 4.0F, 1.5F, std::max(0.0F, text_bounds.height - 8.0F)},
            theme_color("caret-color", text_color));
    }
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
    const auto body = inner_body_rect();
    return {body.x + 12.0F,
            body.y + 4.0F,
            std::max(0.0F, body.width - 24.0F),
            std::max(0.0F, body.height - 8.0F)};
}

std::size_t TextField::hit_test_cursor(Point point) const {
    const auto bounds = text_rect();
    const auto font = text_field_font();
    const float local_x = std::max(0.0F, point.x - bounds.x + impl_->scroll_x);

    std::size_t best_index = 0;
    float best_distance = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index <= impl_->text.size(); ++index) {
        const float caret_x = measure_text(impl_->text.substr(0, index), font).width;
        const float distance = std::fabs(caret_x - local_x);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }
    return best_index;
}

void TextField::move_cursor(std::size_t position, bool extend_selection) {
    position = std::min(position, impl_->text.size());
    impl_->cursor = position;
    if (!extend_selection) {
        impl_->selection_anchor = position;
    }
    ensure_caret_visible();
    queue_redraw();
}

void TextField::replace_selection(std::string_view text, bool record_history) {
    const auto start = selection_start();
    const auto end = selection_end();
    const auto had_selection = has_selection();
    const auto changing_text = had_selection || !text.empty();
    if (!changing_text) {
        return;
    }

    if (!had_selection) {
        impl_->selection_anchor = impl_->cursor;
    }

    impl_->text.replace(start, end - start, text);
    impl_->cursor = start + text.size();
    impl_->selection_anchor = impl_->cursor;
    ensure_caret_visible();
    if (record_history) {
        push_history_state();
    }
    impl_->text_changed.emit(impl_->text);
    queue_redraw();
}

void TextField::ensure_caret_visible() {
    const auto bounds = text_rect();
    const auto font = text_field_font();
    const float content_width = std::max(0.0F, bounds.width);
    const float caret_x = measure_text(impl_->text.substr(0, impl_->cursor), font).width;
    const float total_width = measure_text(impl_->text, font).width;
    const float max_scroll = std::max(0.0F, total_width - content_width);

    if (caret_x < impl_->scroll_x) {
        impl_->scroll_x = caret_x;
    } else if (caret_x > impl_->scroll_x + content_width - 2.0F) {
        impl_->scroll_x = caret_x - std::max(0.0F, content_width - 2.0F);
    }

    impl_->scroll_x = std::clamp(impl_->scroll_x, 0.0F, max_scroll);
}

void TextField::reset_history() {
    impl_->history.clear();
    impl_->history.push_back({impl_->text, impl_->cursor, impl_->selection_anchor});
    impl_->history_index = 0;
}

void TextField::push_history_state() {
    if (impl_->history_index + 1 < impl_->history.size()) {
        impl_->history.erase(impl_->history.begin() +
                                 static_cast<std::ptrdiff_t>(impl_->history_index + 1),
                             impl_->history.end());
    }

    const auto state = Impl::HistoryState{
        impl_->text,
        impl_->cursor,
        impl_->selection_anchor,
    };
    if (!impl_->history.empty()) {
        const auto& current = impl_->history.back();
        if (current.text == state.text && current.cursor == state.cursor &&
            current.selection_anchor == state.selection_anchor) {
            return;
        }
    }

    impl_->history.push_back(state);
    impl_->history_index = impl_->history.size() - 1;
}

bool TextField::undo() {
    if (impl_->history_index == 0) {
        return false;
    }

    --impl_->history_index;
    const auto& state = impl_->history[impl_->history_index];
    impl_->text = state.text;
    impl_->cursor = state.cursor;
    impl_->selection_anchor = state.selection_anchor;
    ensure_caret_visible();
    impl_->text_changed.emit(impl_->text);
    queue_redraw();
    return true;
}

bool TextField::redo() {
    if (impl_->history_index + 1 >= impl_->history.size()) {
        return false;
    }

    ++impl_->history_index;
    const auto& state = impl_->history[impl_->history_index];
    impl_->text = state.text;
    impl_->cursor = state.cursor;
    impl_->selection_anchor = state.selection_anchor;
    ensure_caret_visible();
    impl_->text_changed.emit(impl_->text);
    queue_redraw();
    return true;
}

void TextField::copy_selection_to_clipboard() const {
    if (!has_selection()) {
        return;
    }

    const auto selected =
        impl_->text.substr(selection_start(), selection_end() - selection_start());
    if (auto* app = Application::instance()) {
        app->set_clipboard_text(selected);
    } else {
        fallback_clipboard_text() = selected;
    }
}

bool TextField::delete_backward() {
    if (has_selection()) {
        replace_selection({}, true);
        return true;
    }
    if (impl_->cursor == 0) {
        return false;
    }

    impl_->selection_anchor = impl_->cursor - 1;
    replace_selection({}, true);
    return true;
}

bool TextField::delete_forward() {
    if (has_selection()) {
        replace_selection({}, true);
        return true;
    }
    if (impl_->cursor >= impl_->text.size()) {
        return false;
    }

    impl_->selection_anchor = impl_->cursor + 1;
    replace_selection({}, true);
    return true;
}

bool TextField::paste_from_clipboard() {
    std::string text;
    if (auto* app = Application::instance()) {
        text = app->clipboard_text();
    } else {
        text = fallback_clipboard_text();
    }

    if (text.empty()) {
        return false;
    }

    replace_selection(text, true);
    return true;
}

} // namespace nk
