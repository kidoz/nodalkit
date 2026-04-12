#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/search_field.h>

namespace nk {

namespace {

FontDescriptor search_field_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

struct SearchField::Impl {
    std::string text;
    std::string placeholder;
    Signal<std::string_view> text_changed;
    Signal<std::string_view> search;
    std::size_t cursor_pos = 0;
    bool focused_state = false;
};

std::shared_ptr<SearchField> SearchField::create(std::string placeholder) {
    return std::shared_ptr<SearchField>(new SearchField(std::move(placeholder)));
}

SearchField::SearchField(std::string placeholder) : impl_(std::make_unique<Impl>()) {
    impl_->placeholder = std::move(placeholder);
    set_focusable(true);
    add_style_class("search-field");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TextInput);
    accessible.set_name(impl_->placeholder.empty() ? "Search" : impl_->placeholder);
    accessible.add_action(AccessibleAction::Focus, [this]() {
        grab_focus();
        return true;
    });
}

SearchField::~SearchField() = default;

std::string_view SearchField::text() const {
    return impl_->text;
}

void SearchField::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
        impl_->cursor_pos = impl_->text.size();
        ensure_accessible().set_value(impl_->text);
        queue_redraw();
        impl_->text_changed.emit(impl_->text);
    }
}

std::string_view SearchField::placeholder() const {
    return impl_->placeholder;
}

void SearchField::set_placeholder(std::string placeholder) {
    impl_->placeholder = std::move(placeholder);
    ensure_accessible().set_name(impl_->placeholder.empty() ? "Search" : impl_->placeholder);
    queue_redraw();
}

Signal<std::string_view>& SearchField::on_text_changed() {
    return impl_->text_changed;
}

Signal<std::string_view>& SearchField::on_search() {
    return impl_->search;
}

SizeRequest SearchField::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("min-width", 200.0F);
    const float h = theme_number("min-height", 36.0F);
    return {120.0F, h, w, h};
}

bool SearchField::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    const auto a = allocation();
    const auto point = Point{event.x, event.y};

    switch (event.type) {
    case MouseEvent::Type::Press: {
        if (!a.contains(point)) {
            return false;
        }

        // Check if click is on clear button area (right side, only when text present)
        if (!impl_->text.empty()) {
            const float clear_button_width = theme_number("clear-button-width", 28.0F);
            if (point.x > a.right() - clear_button_width) {
                impl_->text.clear();
                impl_->cursor_pos = 0;
                ensure_accessible().set_value(impl_->text);
                queue_redraw();
                impl_->text_changed.emit(impl_->text);
                return true;
            }
        }

        grab_focus();
        return true;
    }
    case MouseEvent::Type::Release:
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool SearchField::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Return:
        impl_->search.emit(impl_->text);
        return true;
    case KeyCode::Backspace:
        if (impl_->cursor_pos > 0) {
            // Delete one byte before cursor (simplified, not full grapheme-aware)
            std::size_t delete_pos = impl_->cursor_pos - 1;
            // Walk back over continuation bytes for basic UTF-8 safety
            while (delete_pos > 0 &&
                   (static_cast<unsigned char>(impl_->text[delete_pos]) & 0xC0U) == 0x80U) {
                --delete_pos;
            }
            impl_->text.erase(delete_pos, impl_->cursor_pos - delete_pos);
            impl_->cursor_pos = delete_pos;
            ensure_accessible().set_value(impl_->text);
            queue_redraw();
            impl_->text_changed.emit(impl_->text);
            return true;
        }
        return false;
    case KeyCode::Delete:
        if (impl_->cursor_pos < impl_->text.size()) {
            std::size_t end_pos = impl_->cursor_pos + 1;
            // Walk forward over continuation bytes for basic UTF-8 safety
            while (end_pos < impl_->text.size() &&
                   (static_cast<unsigned char>(impl_->text[end_pos]) & 0xC0U) == 0x80U) {
                ++end_pos;
            }
            impl_->text.erase(impl_->cursor_pos, end_pos - impl_->cursor_pos);
            ensure_accessible().set_value(impl_->text);
            queue_redraw();
            impl_->text_changed.emit(impl_->text);
            return true;
        }
        return false;
    case KeyCode::Left:
        if (impl_->cursor_pos > 0) {
            --impl_->cursor_pos;
            // Walk back over continuation bytes
            while (impl_->cursor_pos > 0 &&
                   (static_cast<unsigned char>(impl_->text[impl_->cursor_pos]) & 0xC0U) == 0x80U) {
                --impl_->cursor_pos;
            }
            queue_redraw();
            return true;
        }
        return false;
    case KeyCode::Right:
        if (impl_->cursor_pos < impl_->text.size()) {
            ++impl_->cursor_pos;
            // Walk forward over continuation bytes
            while (impl_->cursor_pos < impl_->text.size() &&
                   (static_cast<unsigned char>(impl_->text[impl_->cursor_pos]) & 0xC0U) == 0x80U) {
                ++impl_->cursor_pos;
            }
            queue_redraw();
            return true;
        }
        return false;
    case KeyCode::Home:
        if (impl_->cursor_pos != 0) {
            impl_->cursor_pos = 0;
            queue_redraw();
            return true;
        }
        return false;
    case KeyCode::End:
        if (impl_->cursor_pos != impl_->text.size()) {
            impl_->cursor_pos = impl_->text.size();
            queue_redraw();
            return true;
        }
        return false;
    case KeyCode::Escape:
        if (!impl_->text.empty()) {
            impl_->text.clear();
            impl_->cursor_pos = 0;
            ensure_accessible().set_value(impl_->text);
            queue_redraw();
            impl_->text_changed.emit(impl_->text);
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool SearchField::handle_text_input_event(const TextInputEvent& event) {
    switch (event.type) {
    case TextInputEvent::Type::Commit:
        if (event.text.empty()) {
            return false;
        }
        impl_->text.insert(impl_->cursor_pos, event.text);
        impl_->cursor_pos += event.text.size();
        ensure_accessible().set_value(impl_->text);
        queue_redraw();
        impl_->text_changed.emit(impl_->text);
        return true;
    case TextInputEvent::Type::Preedit:
    case TextInputEvent::Type::ClearPreedit:
    case TextInputEvent::Type::DeleteSurrounding:
        return false;
    }
    return false;
}

CursorShape SearchField::cursor_shape() const {
    return CursorShape::IBeam;
}

void SearchField::on_focus_changed(bool focused) {
    impl_->focused_state = focused;
    queue_redraw();
}

void SearchField::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    const float icon_width = theme_number("icon-width", 28.0F);
    const float clear_button_width = theme_number("clear-button-width", 28.0F);
    auto body = a;

    // Focus ring
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    // Background
    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    const auto font = search_field_font();
    const auto icon_color = theme_color("icon-color", Color{0.45F, 0.48F, 0.54F, 1.0F});

    // Search icon (magnifying glass character)
    {
        const auto icon_font = FontDescriptor{
            .family = {},
            .size = 14.0F,
            .weight = FontWeight::Medium,
        };
        const auto icon_text = std::string("Q");
        const auto icon_measured = measure_text(icon_text, icon_font);
        const float icon_x = body.x + (icon_width - icon_measured.width) * 0.5F;
        const float icon_y = body.y + std::max(0.0F, (body.height - icon_measured.height) * 0.5F);
        ctx.add_text({icon_x, icon_y}, icon_text, icon_color, icon_font);
    }

    // Text content area
    const float text_left = body.x + icon_width;
    const float text_right_margin = (!impl_->text.empty()) ? clear_button_width : 12.0F;
    const float text_width = std::max(0.0F, body.width - icon_width - text_right_margin);

    ctx.push_rounded_clip(
        {text_left, body.y, text_width, body.height}, corner_radius);

    if (impl_->text.empty()) {
        // Placeholder text
        if (!impl_->placeholder.empty()) {
            const auto placeholder_color =
                theme_color("placeholder-color", Color{0.55F, 0.58F, 0.62F, 1.0F});
            const auto measured = measure_text(impl_->placeholder, font);
            const float text_y =
                body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
            ctx.add_text({text_left, text_y}, impl_->placeholder, placeholder_color, font);
        }
    } else {
        // Actual text
        const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
        const auto measured = measure_text(impl_->text, font);
        const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text({text_left, text_y}, impl_->text, text_color, font);

        // Caret
        if (has_flag(state_flags(), StateFlags::Focused)) {
            const auto text_before_cursor = impl_->text.substr(0, impl_->cursor_pos);
            const auto cursor_offset = measure_text(text_before_cursor, font);
            const float caret_x = text_left + cursor_offset.width;
            ctx.add_color_rect(
                {caret_x, body.y + 4.0F, 1.5F, std::max(0.0F, body.height - 8.0F)},
                theme_color("caret-color", text_color));
        }
    }

    ctx.pop_container();

    // Clear button "x" when text is present
    if (!impl_->text.empty()) {
        const auto clear_font = FontDescriptor{
            .family = {},
            .size = 13.0F,
            .weight = FontWeight::Medium,
        };
        const auto clear_text = std::string("x");
        const auto clear_measured = measure_text(clear_text, clear_font);
        const float clear_x =
            body.right() - clear_button_width +
            (clear_button_width - clear_measured.width) * 0.5F;
        const float clear_y =
            body.y + std::max(0.0F, (body.height - clear_measured.height) * 0.5F);
        ctx.add_text({clear_x, clear_y}, clear_text, icon_color, clear_font);
    }
}

} // namespace nk
