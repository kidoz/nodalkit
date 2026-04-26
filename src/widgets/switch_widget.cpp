#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/switch_widget.h>

namespace nk {

struct Switch::Impl {
    Signal<bool> toggled;
    bool active = false;
};

std::shared_ptr<Switch> Switch::create() {
    return std::shared_ptr<Switch>(new Switch());
}

Switch::Switch() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("switch");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::ToggleButton);
    accessible.set_name("Switch");
    accessible.add_action(AccessibleAction::Toggle, [this]() {
        set_active(!impl_->active);
        return true;
    });
}

Switch::~Switch() = default;

bool Switch::is_active() const {
    return impl_->active;
}

void Switch::set_active(bool active) {
    if (impl_->active != active) {
        impl_->active = active;
        set_state_flag(StateFlags::Checked, active);
        ensure_accessible().set_value(active ? "on" : "off");
        impl_->toggled.emit(active);
        queue_redraw();
    }
}

Signal<bool>& Switch::on_toggled() {
    return impl_->toggled;
}

SizeRequest Switch::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("width", 44.0F);
    const float h = theme_number("height", 24.0F);
    return {w, h, w, h};
}

bool Switch::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        return allocation().contains({event.x, event.y});
    case MouseEvent::Type::Release:
        if (allocation().contains({event.x, event.y})) {
            set_active(!impl_->active);
            return true;
        }
        return false;
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool Switch::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        set_active(!impl_->active);
        return true;
    }

    return false;
}

CursorShape Switch::cursor_shape() const {
    return CursorShape::PointingHand;
}

void Switch::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float track_width = theme_number("width", 44.0F);
    const float track_height = theme_number("height", 24.0F);
    const float track_radius = track_height * 0.5F;
    const float thumb_size = theme_number("thumb-size", 18.0F);
    const float thumb_radius = thumb_size * 0.5F;
    const float thumb_margin = (track_height - thumb_size) * 0.5F;

    // Center the track within the allocation.
    const float track_x = a.x + std::max(0.0F, (a.width - track_width) * 0.5F);
    const float track_y = a.y + std::max(0.0F, (a.height - track_height) * 0.5F);
    const Rect track_rect{track_x, track_y, track_width, track_height};

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect({track_rect.x - 2.0F,
                              track_rect.y - 2.0F,
                              track_rect.width + 4.0F,
                              track_rect.height + 4.0F},
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             track_radius + 2.0F);
    }

    // Track background.
    const auto track_color =
        impl_->active ? theme_color("active-track-color", Color{0.2F, 0.45F, 0.85F, 1.0F})
                      : theme_color("inactive-track-color", Color{0.78F, 0.8F, 0.84F, 1.0F});
    ctx.add_rounded_rect(track_rect, track_color, track_radius);

    // Thumb position: left when off, right when on.
    const float thumb_x_off = track_rect.x + thumb_margin;
    const float thumb_x_on = track_rect.right() - thumb_margin - thumb_size;
    const float thumb_x = impl_->active ? thumb_x_on : thumb_x_off;
    const float thumb_y = track_rect.y + thumb_margin;
    const Rect thumb_rect{thumb_x, thumb_y, thumb_size, thumb_size};

    ctx.add_rounded_rect(
        thumb_rect, theme_color("thumb-color", Color{1.0F, 1.0F, 1.0F, 1.0F}), thumb_radius);
}

} // namespace nk
