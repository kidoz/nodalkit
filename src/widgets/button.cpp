#include <nk/widgets/button.h>

#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>

#include <algorithm>

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

struct Button::Impl {
    std::string label;
    Signal<> clicked;
    bool armed = false;
};

std::shared_ptr<Button> Button::create(std::string label) {
    return std::shared_ptr<Button>(new Button(std::move(label)));
}

Button::Button(std::string label)
    : impl_(std::make_unique<Impl>()) {
    impl_->label = std::move(label);
    set_focusable(true);
    add_style_class("button");
}

Button::~Button() = default;

std::string_view Button::label() const { return impl_->label; }

void Button::set_label(std::string label) {
    if (impl_->label != label) {
        impl_->label = std::move(label);
        queue_layout();
        queue_redraw();
    }
}

Signal<>& Button::on_clicked() { return impl_->clicked; }

SizeRequest Button::measure(Constraints const& /*constraints*/) const {
    auto const measured = measure_text(impl_->label, button_font());
    float const padding_x = theme_number("padding-x", 16.0F);
    float const min_width = theme_number("min-width", 82.0F);
    float const min_height = theme_number("min-height", 36.0F);
    float const w = std::max(min_width, measured.width + (padding_x * 2.0F));
    float const h = min_height;
    return {w, h, w, h};
}

bool Button::handle_mouse_event(MouseEvent const& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed = allocation().contains({event.x, event.y});
        return impl_->armed;
    case MouseEvent::Type::Release: {
        bool const activate =
            impl_->armed && allocation().contains({event.x, event.y});
        impl_->armed = false;
        if (activate) {
            impl_->clicked.emit();
        }
        return activate;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool Button::handle_key_event(KeyEvent const& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        impl_->clicked.emit();
        return true;
    }

    return false;
}

void Button::snapshot(SnapshotContext& ctx) const {
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
        theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}),
        corner_radius);
    ctx.add_border(
        body,
        theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}),
        1.0F,
        corner_radius);

    auto const font = button_font();
    auto const measured = measure_text(impl_->label, font);
    float const text_x =
        body.x + std::max(0.0F, (body.width - measured.width) * 0.5F);
    float const text_y =
        body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
    ctx.add_text(
        {text_x, text_y},
        std::string(impl_->label),
        theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
        font);
}

} // namespace nk
