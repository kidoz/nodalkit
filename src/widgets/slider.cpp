#include <algorithm>
#include <cmath>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/slider.h>
#include <sstream>

namespace nk {

struct Slider::Impl {
    Orientation orientation = Orientation::Horizontal;
    double value = 0.0;
    double min_val = 0.0;
    double max_val = 1.0;
    double step = 0.0;
    Signal<double> value_changed;
    bool dragging = false;
};

std::shared_ptr<Slider> Slider::create(Orientation orientation) {
    return std::shared_ptr<Slider>(new Slider(orientation));
}

Slider::Slider(Orientation orientation) : impl_(std::make_unique<Impl>()) {
    impl_->orientation = orientation;
    set_focusable(true);
    add_style_class("slider");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Slider);
    accessible.set_value("0");
    accessible.add_action(AccessibleAction::Focus, [this]() {
        grab_focus();
        return true;
    });
}

Slider::~Slider() = default;

Orientation Slider::orientation() const {
    return impl_->orientation;
}

double Slider::value() const {
    return impl_->value;
}

void Slider::set_value(double val) {
    val = std::clamp(val, impl_->min_val, impl_->max_val);
    if (impl_->step > 0.0 && impl_->max_val > impl_->min_val) {
        const double steps = std::round((val - impl_->min_val) / impl_->step);
        val = std::clamp(impl_->min_val + steps * impl_->step, impl_->min_val, impl_->max_val);
    }
    if (impl_->value != val) {
        impl_->value = val;
        std::ostringstream oss;
        oss << impl_->value;
        ensure_accessible().set_value(oss.str());
        queue_redraw();
        impl_->value_changed.emit(impl_->value);
    }
}

double Slider::minimum() const {
    return impl_->min_val;
}

double Slider::maximum() const {
    return impl_->max_val;
}

void Slider::set_range(double min, double max) {
    impl_->min_val = min;
    impl_->max_val = max;
    set_value(impl_->value);
}

double Slider::step() const {
    return impl_->step;
}

void Slider::set_step(double s) {
    impl_->step = s;
}

Signal<double>& Slider::on_value_changed() {
    return impl_->value_changed;
}

SizeRequest Slider::measure(const Constraints& /*constraints*/) const {
    if (impl_->orientation == Orientation::Horizontal) {
        const float w = theme_number("min-width", 200.0F);
        const float h = theme_number("min-height", 24.0F);
        return {w, h, w, h};
    }
    const float w = theme_number("min-width", 24.0F);
    const float h = theme_number("min-height", 200.0F);
    return {w, h, w, h};
}

bool Slider::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1 && event.type != MouseEvent::Type::Move) {
        return false;
    }

    const auto a = allocation();
    const float thumb_radius = theme_number("thumb-radius", 8.0F);
    const double range = impl_->max_val - impl_->min_val;

    auto value_from_point = [&](float px, float py) -> double {
        if (range <= 0.0) {
            return impl_->min_val;
        }
        if (impl_->orientation == Orientation::Horizontal) {
            const float usable = std::max(1.0F, a.width - thumb_radius * 2.0F);
            const float local_x = std::clamp(px - a.x - thumb_radius, 0.0F, usable);
            return impl_->min_val + static_cast<double>(local_x / usable) * range;
        }
        const float usable = std::max(1.0F, a.height - thumb_radius * 2.0F);
        const float local_y = std::clamp(py - a.y - thumb_radius, 0.0F, usable);
        // Vertical slider: top = max, bottom = min
        return impl_->max_val - static_cast<double>(local_y / usable) * range;
    };

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (a.contains({event.x, event.y})) {
            impl_->dragging = true;
            grab_focus();
            set_value(value_from_point(event.x, event.y));
            return true;
        }
        return false;
    case MouseEvent::Type::Move:
        if (impl_->dragging) {
            set_value(value_from_point(event.x, event.y));
            return true;
        }
        return false;
    case MouseEvent::Type::Release:
        if (impl_->dragging) {
            impl_->dragging = false;
            set_value(value_from_point(event.x, event.y));
            return true;
        }
        return false;
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool Slider::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    const double increment = impl_->step > 0.0 ? impl_->step : 0.05 * (impl_->max_val - impl_->min_val);

    switch (event.key) {
    case KeyCode::Left:
    case KeyCode::Down:
        set_value(impl_->value - increment);
        return true;
    case KeyCode::Right:
    case KeyCode::Up:
        set_value(impl_->value + increment);
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

CursorShape Slider::cursor_shape() const {
    return CursorShape::PointingHand;
}

void Slider::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float thumb_diameter = theme_number("thumb-diameter", 16.0F);
    const float thumb_radius = thumb_diameter * 0.5F;
    const float track_thickness = theme_number("track-thickness", 4.0F);
    const float track_radius = track_thickness * 0.5F;

    const auto track_bg = theme_color("track-background", Color{0.82F, 0.84F, 0.88F, 1.0F});
    const auto fill_color = theme_color("fill-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
    const auto thumb_color = theme_color("thumb-color", Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto thumb_border_color = theme_color("thumb-border-color", Color{0.78F, 0.8F, 0.84F, 1.0F});

    const double range = impl_->max_val - impl_->min_val;
    const float fraction = range > 0.0
                               ? static_cast<float>((impl_->value - impl_->min_val) / range)
                               : 0.0F;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             thumb_radius + 2.0F);
    }

    if (impl_->orientation == Orientation::Horizontal) {
        const float track_y = a.y + (a.height - track_thickness) * 0.5F;
        const float usable = std::max(0.0F, a.width - thumb_diameter);

        // Track background
        ctx.add_rounded_rect(
            {a.x + thumb_radius, track_y, std::max(0.0F, a.width - thumb_diameter), track_thickness},
            track_bg,
            track_radius);

        // Filled portion
        const float fill_width = usable * fraction;
        if (fill_width > 0.0F) {
            ctx.add_rounded_rect(
                {a.x + thumb_radius, track_y, fill_width, track_thickness},
                fill_color,
                track_radius);
        }

        // Thumb
        const float thumb_cx = a.x + thumb_radius + usable * fraction;
        const float thumb_cy = a.y + a.height * 0.5F;
        ctx.add_rounded_rect(
            {thumb_cx - thumb_radius, thumb_cy - thumb_radius, thumb_diameter, thumb_diameter},
            thumb_color,
            thumb_radius);
        ctx.add_border(
            {thumb_cx - thumb_radius, thumb_cy - thumb_radius, thumb_diameter, thumb_diameter},
            thumb_border_color,
            1.0F,
            thumb_radius);
    } else {
        const float track_x = a.x + (a.width - track_thickness) * 0.5F;
        const float usable = std::max(0.0F, a.height - thumb_diameter);

        // Track background
        ctx.add_rounded_rect(
            {track_x, a.y + thumb_radius, track_thickness, std::max(0.0F, a.height - thumb_diameter)},
            track_bg,
            track_radius);

        // Filled portion (from bottom up)
        const float fill_height = usable * fraction;
        if (fill_height > 0.0F) {
            ctx.add_rounded_rect(
                {track_x, a.y + thumb_radius + usable - fill_height, track_thickness, fill_height},
                fill_color,
                track_radius);
        }

        // Thumb (top = max, bottom = min)
        const float thumb_cy = a.y + thumb_radius + usable * (1.0F - fraction);
        const float thumb_cx = a.x + a.width * 0.5F;
        ctx.add_rounded_rect(
            {thumb_cx - thumb_radius, thumb_cy - thumb_radius, thumb_diameter, thumb_diameter},
            thumb_color,
            thumb_radius);
        ctx.add_border(
            {thumb_cx - thumb_radius, thumb_cy - thumb_radius, thumb_diameter, thumb_diameter},
            thumb_border_color,
            1.0F,
            thumb_radius);
    }
}

} // namespace nk
