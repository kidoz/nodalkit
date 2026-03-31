#include <nk/widgets/text_field.h>

#include <nk/actions/shortcut.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>

#include <algorithm>
#include <optional>

namespace nk {

namespace {

FontDescriptor text_field_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

bool has_shift(Modifiers modifiers) {
    return (modifiers & Modifiers::Shift) == Modifiers::Shift;
}

std::optional<char> key_to_ascii(KeyEvent const& event) {
    bool const shift = has_shift(event.modifiers);
    auto key = static_cast<uint16_t>(event.key);

    if (key >= static_cast<uint16_t>(KeyCode::A)
        && key <= static_cast<uint16_t>(KeyCode::Z)) {
        char const base = shift ? 'A' : 'a';
        return static_cast<char>(
            base + (key - static_cast<uint16_t>(KeyCode::A)));
    }

    switch (event.key) {
    case KeyCode::Num1: return shift ? '!' : '1';
    case KeyCode::Num2: return shift ? '@' : '2';
    case KeyCode::Num3: return shift ? '#' : '3';
    case KeyCode::Num4: return shift ? '$' : '4';
    case KeyCode::Num5: return shift ? '%' : '5';
    case KeyCode::Num6: return shift ? '^' : '6';
    case KeyCode::Num7: return shift ? '&' : '7';
    case KeyCode::Num8: return shift ? '*' : '8';
    case KeyCode::Num9: return shift ? '(' : '9';
    case KeyCode::Num0: return shift ? ')' : '0';
    case KeyCode::Space: return ' ';
    case KeyCode::Minus: return shift ? '_' : '-';
    case KeyCode::Equals: return shift ? '+' : '=';
    case KeyCode::LeftBracket: return shift ? '{' : '[';
    case KeyCode::RightBracket: return shift ? '}' : ']';
    case KeyCode::Backslash: return shift ? '|' : '\\';
    case KeyCode::Semicolon: return shift ? ':' : ';';
    case KeyCode::Apostrophe: return shift ? '"' : '\'';
    case KeyCode::Grave: return shift ? '~' : '`';
    case KeyCode::Comma: return shift ? '<' : ',';
    case KeyCode::Period: return shift ? '>' : '.';
    case KeyCode::Slash: return shift ? '?' : '/';
    default: return std::nullopt;
    }
}

} // namespace

struct TextField::Impl {
    std::string text;
    std::string placeholder;
    bool editable = true;
    Signal<std::string_view> text_changed;
    Signal<> activate;
};

std::shared_ptr<TextField> TextField::create(std::string initial_text) {
    return std::shared_ptr<TextField>(
        new TextField(std::move(initial_text)));
}

TextField::TextField(std::string text)
    : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    set_focusable(true);
    add_style_class("text-field");
}

TextField::~TextField() = default;

std::string_view TextField::text() const { return impl_->text; }

void TextField::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
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

bool TextField::is_editable() const { return impl_->editable; }
void TextField::set_editable(bool editable) { impl_->editable = editable; }

Signal<std::string_view>& TextField::on_text_changed() {
    return impl_->text_changed;
}

Signal<>& TextField::on_activate() { return impl_->activate; }

SizeRequest TextField::measure(Constraints const& /*constraints*/) const {
    float const w = theme_number("min-width", 240.0F);
    float const h = theme_number("min-height", 36.0F);
    return {120.0F, h, w, h};
}

bool TextField::handle_mouse_event(MouseEvent const& event) {
    return event.type == MouseEvent::Type::Press
        && event.button == 1
        && allocation().contains({event.x, event.y});
}

bool TextField::handle_key_event(KeyEvent const& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Return) {
        impl_->activate.emit();
        return true;
    }

    if (!impl_->editable) {
        return false;
    }

    if (event.key == KeyCode::Backspace) {
        if (!impl_->text.empty()) {
            impl_->text.pop_back();
            impl_->text_changed.emit(impl_->text);
            queue_redraw();
        }
        return true;
    }

    auto character = key_to_ascii(event);
    if (!character.has_value()) {
        return false;
    }

    impl_->text.push_back(*character);
    impl_->text_changed.emit(impl_->text);
    queue_redraw();
    return true;
}

void TextField::snapshot(SnapshotContext& ctx) const {
    auto const a = allocation();
    float const corner_radius = theme_number("corner-radius", 10.0F);
    auto body = a;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(
            a,
            theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
            corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    ctx.add_rounded_rect(
        body,
        theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}),
        corner_radius);
    ctx.add_border(
        body,
        theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}),
        1.0F,
        corner_radius);

    auto const& display_text = impl_->text.empty()
        ? impl_->placeholder : impl_->text;
    Color text_color = impl_->text.empty()
        ? theme_color("placeholder-color", Color{0.55F, 0.58F, 0.62F, 1.0F})
        : theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    if (!display_text.empty()) {
        auto const font = text_field_font();
        auto const measured = measure_text(display_text, font);
        float const text_y =
            body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text(
            {body.x + 12.0F, text_y},
            std::string(display_text),
            text_color,
            font);
    }
}

} // namespace nk
