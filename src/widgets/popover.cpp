#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/popover.h>

namespace nk {

struct Popover::Impl {
    std::shared_ptr<Widget> child;
    bool open = false;
    Point anchor{};
    Signal<> dismissed;
    Rect popup_rect{};
};

std::shared_ptr<Popover> Popover::create() {
    return std::shared_ptr<Popover>(new Popover());
}

Popover::Popover() : impl_(std::make_unique<Impl>()) {
    add_style_class("popover");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Dialog);
}

Popover::~Popover() = default;

void Popover::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child) {
        append_child(impl_->child);
    }
    queue_layout();
    queue_redraw();
}

void Popover::show_at(Point anchor) {
    impl_->anchor = anchor;
    impl_->open = true;
    set_focusable(true);
    preserve_damage_regions_for_next_redraw();
    queue_layout();
    queue_redraw();
}

void Popover::dismiss() {
    if (!impl_->open) {
        return;
    }
    preserve_damage_regions_for_next_redraw();
    impl_->open = false;
    set_focusable(false);
    impl_->dismissed.emit();
    queue_redraw();
}

bool Popover::is_open() const {
    return impl_->open;
}

Signal<>& Popover::on_dismissed() {
    return impl_->dismissed;
}

SizeRequest Popover::measure(const Constraints& /*constraints*/) const {
    return {0.0F, 0.0F, 0.0F, 0.0F};
}

void Popover::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    if (!impl_->open || !impl_->child) {
        impl_->popup_rect = {};
        return;
    }

    const auto child_req = impl_->child->measure_for_diagnostics(Constraints::unbounded());
    const float padding = theme_number("padding", 8.0F);
    const float popup_w = child_req.natural_width + (padding * 2.0F);
    const float popup_h = child_req.natural_height + (padding * 2.0F);

    // Position below anchor, centered horizontally.
    const float popup_x = impl_->anchor.x - (popup_w * 0.5F);
    const float popup_y = impl_->anchor.y + 4.0F;

    impl_->popup_rect = {popup_x, popup_y, popup_w, popup_h};

    impl_->child->allocate({popup_x + padding, popup_y + padding,
                            child_req.natural_width, child_req.natural_height});
}

bool Popover::hit_test(Point point) const {
    if (!impl_->open) {
        return false;
    }
    return impl_->popup_rect.contains(point);
}

std::vector<Rect> Popover::damage_regions() const {
    if (!impl_->open || impl_->popup_rect.width <= 0.0F) {
        return {};
    }

    constexpr float shadow_extend = 4.0F;
    return {{
        impl_->popup_rect.x - shadow_extend,
        impl_->popup_rect.y - shadow_extend,
        impl_->popup_rect.width + (shadow_extend * 2.0F),
        impl_->popup_rect.height + (shadow_extend * 2.0F),
    }};
}

bool Popover::handle_mouse_event(const MouseEvent& event) {
    if (!impl_->open) {
        return false;
    }

    const Point point{event.x, event.y};

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button != 1) {
            return false;
        }
        if (!impl_->popup_rect.contains(point)) {
            dismiss();
            return true;
        }
        return true;
    case MouseEvent::Type::Release:
    case MouseEvent::Type::Move:
        return impl_->popup_rect.contains(point);
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool Popover::handle_key_event(const KeyEvent& event) {
    if (!impl_->open) {
        return false;
    }

    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Escape) {
        dismiss();
        return true;
    }

    return false;
}

void Popover::snapshot(SnapshotContext& ctx) const {
    if (!impl_->open) {
        return;
    }

    const float corner_radius = theme_number("corner-radius", 12.0F);
    const auto& popup = impl_->popup_rect;

    ctx.push_overlay_container(allocation());

    // Shadow.
    ctx.add_rounded_rect({popup.x, popup.y + 2.0F, popup.width, popup.height},
                         Color{0.08F, 0.12F, 0.18F, 0.12F},
                         corner_radius + 1.0F);

    // Background.
    ctx.add_rounded_rect(popup,
                         theme_color("background", Color{0.98F, 0.98F, 0.99F, 1.0F}),
                         corner_radius);

    // Border.
    ctx.add_border(popup,
                   theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}),
                   1.0F,
                   corner_radius);

    // Child content.
    if (impl_->child) {
        Widget::snapshot(ctx);
    }

    ctx.pop_container();
}

} // namespace nk
