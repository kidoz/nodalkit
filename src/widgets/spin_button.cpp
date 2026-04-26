#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/spin_button.h>
#include <sstream>

namespace nk {

namespace {

FontDescriptor spin_button_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

std::string format_value(double value, int precision) {
    std::ostringstream oss;
    oss.precision(precision);
    oss << std::fixed << value;
    return oss.str();
}

} // namespace

struct SpinButton::Impl {
    double value = 0.0;
    double min_val = 0.0;
    double max_val = 100.0;
    double step = 1.0;
    int precision = 0;
    Signal<double> value_changed;
    int armed_button = 0; // -1 = dec, 0 = none, 1 = inc
};

std::shared_ptr<SpinButton> SpinButton::create() {
    return std::shared_ptr<SpinButton>(new SpinButton());
}

SpinButton::SpinButton() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("spin-button");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::SpinButton);
    accessible.set_value(format_value(impl_->value, impl_->precision));
    accessible.add_action(AccessibleAction::Focus, [this]() {
        grab_focus();
        return true;
    });
}

SpinButton::~SpinButton() = default;

double SpinButton::value() const {
    return impl_->value;
}

void SpinButton::set_value(double val) {
    val = std::clamp(val, impl_->min_val, impl_->max_val);
    if (impl_->value != val) {
        impl_->value = val;
        ensure_accessible().set_value(format_value(impl_->value, impl_->precision));
        queue_redraw();
        impl_->value_changed.emit(impl_->value);
    }
}

double SpinButton::minimum() const {
    return impl_->min_val;
}

double SpinButton::maximum() const {
    return impl_->max_val;
}

void SpinButton::set_range(double min, double max) {
    impl_->min_val = min;
    impl_->max_val = max;
    set_value(impl_->value);
}

double SpinButton::step() const {
    return impl_->step;
}

void SpinButton::set_step(double s) {
    impl_->step = s;
}

int SpinButton::precision() const {
    return impl_->precision;
}

void SpinButton::set_precision(int digits) {
    impl_->precision = digits;
    ensure_accessible().set_value(format_value(impl_->value, impl_->precision));
    queue_redraw();
}

Signal<double>& SpinButton::on_value_changed() {
    return impl_->value_changed;
}

SizeRequest SpinButton::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("min-width", 120.0F);
    const float h = theme_number("min-height", 36.0F);
    return {w, h, w, h};
}

bool SpinButton::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    const auto a = allocation();
    const float button_width = theme_number("button-width", a.width / 3.0F);
    const auto point = Point{event.x, event.y};

    auto zone_at = [&](Point pt) -> int {
        if (!a.contains(pt)) {
            return 0;
        }
        if (pt.x < a.x + button_width) {
            return -1; // decrement
        }
        if (pt.x > a.right() - button_width) {
            return 1; // increment
        }
        return 0; // middle
    };

    switch (event.type) {
    case MouseEvent::Type::Press: {
        const int zone = zone_at(point);
        if (!a.contains(point)) {
            return false;
        }
        impl_->armed_button = zone;
        grab_focus();
        queue_redraw();
        return true;
    }
    case MouseEvent::Type::Release: {
        const int zone = zone_at(point);
        const bool activate = impl_->armed_button != 0 && zone == impl_->armed_button;
        impl_->armed_button = 0;
        queue_redraw();
        if (activate) {
            if (zone == -1) {
                set_value(impl_->value - impl_->step);
            } else if (zone == 1) {
                set_value(impl_->value + impl_->step);
            }
            return true;
        }
        return a.contains(point);
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool SpinButton::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Up:
    case KeyCode::Right:
        set_value(impl_->value + impl_->step);
        return true;
    case KeyCode::Down:
    case KeyCode::Left:
        set_value(impl_->value - impl_->step);
        return true;
    case KeyCode::Home:
        set_value(impl_->min_val);
        return true;
    case KeyCode::End:
        set_value(impl_->max_val);
        return true;
    default:
        return false;
    }
}

CursorShape SpinButton::cursor_shape() const {
    return CursorShape::PointingHand;
}

void SpinButton::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    const float button_width = theme_number("button-width", a.width / 3.0F);
    auto body = a;

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
        body, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, corner_radius);

    const auto font = spin_button_font();
    const auto border_color = theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F});
    const auto button_bg = theme_color("button-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto armed_bg = theme_color("armed-background", Color{0.88F, 0.9F, 0.93F, 1.0F});
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto button_text_color =
        theme_color("button-text-color", Color{0.25F, 0.27F, 0.3F, 1.0F});

    // Left divider
    ctx.add_color_rect({body.x + button_width, body.y + 1.0F, 1.0F, body.height - 2.0F},
                       border_color);
    // Right divider
    ctx.add_color_rect({body.right() - button_width, body.y + 1.0F, 1.0F, body.height - 2.0F},
                       border_color);

    // Decrement button background
    {
        const auto bg = impl_->armed_button == -1 ? armed_bg : button_bg;
        ctx.add_color_rect({body.x + 1.0F, body.y + 1.0F, button_width - 1.0F, body.height - 2.0F},
                           bg);
    }

    // Increment button background
    {
        const auto bg = impl_->armed_button == 1 ? armed_bg : button_bg;
        ctx.add_color_rect({body.right() - button_width + 1.0F,
                            body.y + 1.0F,
                            button_width - 2.0F,
                            body.height - 2.0F},
                           bg);
    }

    // Decrement "-" text
    {
        const auto measured = measure_text("-", font);
        const float text_x = body.x + (button_width - measured.width) * 0.5F;
        const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y}, "-", button_text_color, font);
    }

    // Increment "+" text
    {
        const auto measured = measure_text("+", font);
        const float text_x = body.right() - button_width + (button_width - measured.width) * 0.5F;
        const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y}, "+", button_text_color, font);
    }

    // Value text centered
    {
        const auto value_str = format_value(impl_->value, impl_->precision);
        const auto measured = measure_text(value_str, font);
        const float center_start = body.x + button_width;
        const float center_width = std::max(0.0F, body.width - button_width * 2.0F);
        const float text_x = center_start + std::max(0.0F, (center_width - measured.width) * 0.5F);
        const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y}, value_str, text_color, font);
    }
}

} // namespace nk
