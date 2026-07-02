#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/button.h>

namespace nk {

namespace {

FontDescriptor button_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

} // namespace

std::shared_ptr<Button> Button::create(std::string label) {
    return std::shared_ptr<Button>(new Button(std::move(label)));
}

Button::Button(std::string label) : label_(std::move(label)) {

    set_focusable(true);
    add_style_class("button");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Button);
    accessible.set_name(label_);
    accessible.add_action(AccessibleAction::Activate, [this]() {
        clicked_.emit();
        return true;
    });
}

Button::~Button() = default;

std::string_view Button::label() const {
    return label_;
}

void Button::set_label(std::string label) {
    if (label_ != label) {
        label_ = std::move(label);
        ensure_accessible().set_name(label_);
        queue_layout();
        queue_redraw();
    }
}

Signal<>& Button::on_clicked() {
    return clicked_;
}

SizeRequest Button::measure(const Constraints& /*constraints*/) const {
    const auto measured = measure_text(label_, button_font());
    const float padding_x = theme_number("padding-x", 16.0F);
    const float min_width = theme_number("min-width", 82.0F);
    const float min_height = theme_number("min-height", 36.0F);
    const float w = std::max(min_width, measured.width + (padding_x * 2.0F));
    const float h = min_height;
    return {w, h, w, h};
}

bool Button::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        armed_ = allocation().contains({event.x, event.y});
        return armed_;
    case MouseEvent::Type::Release: {
        const bool activate = armed_ && allocation().contains({event.x, event.y});
        armed_ = false;
        if (activate) {
            clicked_.emit();
        }
        return activate;
    }
    case MouseEvent::Type::Move:
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

bool Button::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        clicked_.emit();
        return true;
    }

    return false;
}

CursorShape Button::cursor_shape() const {
    return CursorShape::PointingHand;
}

void Button::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    auto body = a;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a, theme_color("focus-ring-color"), corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, corner_radius);

    const auto font = button_font();
    const auto measured = measure_text(label_, font);
    // When layout under-allocates the button, elide the label instead of
    // painting past the body.
    const float label_inset = theme_number("padding-x", 16.0F) * 0.5F;
    const float max_label_width = std::max(0.0F, body.width - (label_inset * 2.0F));
    const float label_width = std::min(measured.width, max_label_width);
    const float text_x = body.x + std::max(label_inset, (body.width - label_width) * 0.5F);
    const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
    add_text_elided(
        ctx, {text_x, text_y}, label_, measured, max_label_width, theme_color("text-color"), font);
}

} // namespace nk
