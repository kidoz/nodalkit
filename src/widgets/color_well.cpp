#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/color_well.h>

namespace nk {

struct ColorWell::Impl {
    Color color;
    Signal<> activated;
    bool armed = false;
};

std::shared_ptr<ColorWell> ColorWell::create(Color color) {
    return std::shared_ptr<ColorWell>(new ColorWell(color));
}

ColorWell::ColorWell(Color color) : impl_(std::make_unique<Impl>()) {
    impl_->color = color;
    set_focusable(true);
    add_style_class("color-well");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Button);
    accessible.set_name("Color selector");
    accessible.add_action(AccessibleAction::Activate, [this]() {
        impl_->activated.emit();
        return true;
    });
}

ColorWell::~ColorWell() = default;

Color ColorWell::color() const {
    return impl_->color;
}

void ColorWell::set_color(Color color) {
    if (!(impl_->color == color)) {
        impl_->color = color;
        queue_redraw();
    }
}

Signal<>& ColorWell::on_activated() {
    return impl_->activated;
}

SizeRequest ColorWell::measure(const Constraints& /*constraints*/) const {
    const float size = theme_number("size", 32.0F);
    return {size, size, size, size};
}

bool ColorWell::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed = allocation().contains({event.x, event.y});
        return impl_->armed;
    case MouseEvent::Type::Release: {
        const bool activate = impl_->armed && allocation().contains({event.x, event.y});
        impl_->armed = false;
        if (activate) {
            impl_->activated.emit();
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

bool ColorWell::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        impl_->activated.emit();
        return true;
    }

    return false;
}

CursorShape ColorWell::cursor_shape() const {
    return CursorShape::PointingHand;
}

void ColorWell::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 6.0F);
    auto body = a;

    // Focus ring.
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a, theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    // Checkerboard hint behind the color (visible when alpha < 1).
    ctx.add_rounded_rect(body, Color{0.85F, 0.85F, 0.85F, 1.0F}, corner_radius);

    // Color swatch.
    ctx.add_rounded_rect(body, impl_->color, corner_radius);

    // Border.
    ctx.add_border(body, theme_color("border-color", Color{0.6F, 0.62F, 0.66F, 1.0F}), 1.0F,
                   corner_radius);
}

} // namespace nk
