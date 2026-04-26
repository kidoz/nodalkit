#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/split_view.h>

namespace nk {

struct SplitView::Impl {
    Orientation orientation = Orientation::Horizontal;
    std::shared_ptr<Widget> start_child;
    std::shared_ptr<Widget> end_child;
    float position = 0.5F;
    float min_start = 50.0F;
    float min_end = 50.0F;
    Signal<float> position_changed;
    bool dragging = false;
    float drag_origin = 0.0F;
    float drag_start_position = 0.0F;
};

std::shared_ptr<SplitView> SplitView::create(Orientation orientation) {
    return std::shared_ptr<SplitView>(new SplitView(orientation));
}

SplitView::SplitView(Orientation orientation) : impl_(std::make_unique<Impl>()) {
    impl_->orientation = orientation;
    add_style_class("split-view");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::None);
}

SplitView::~SplitView() = default;

Orientation SplitView::orientation() const {
    return impl_->orientation;
}

void SplitView::set_start_child(std::shared_ptr<Widget> child) {
    if (impl_->start_child) {
        remove_child(*impl_->start_child);
    }
    impl_->start_child = std::move(child);
    if (impl_->start_child) {
        append_child(impl_->start_child);
    }
    queue_layout();
    queue_redraw();
}

void SplitView::set_end_child(std::shared_ptr<Widget> child) {
    if (impl_->end_child) {
        remove_child(*impl_->end_child);
    }
    impl_->end_child = std::move(child);
    if (impl_->end_child) {
        append_child(impl_->end_child);
    }
    queue_layout();
    queue_redraw();
}

float SplitView::position() const {
    return impl_->position;
}

void SplitView::set_position(float fraction) {
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    if (impl_->position != fraction) {
        impl_->position = fraction;
        impl_->position_changed.emit(fraction);
        queue_layout();
        queue_redraw();
    }
}

void SplitView::set_min_start_size(float size) {
    impl_->min_start = std::max(0.0F, size);
}

void SplitView::set_min_end_size(float size) {
    impl_->min_end = std::max(0.0F, size);
}

Signal<float>& SplitView::on_position_changed() {
    return impl_->position_changed;
}

SizeRequest SplitView::measure(const Constraints& /*constraints*/) const {
    const float divider_width = theme_number("divider-width", 8.0F);

    SizeRequest start_req{};
    SizeRequest end_req{};
    if (impl_->start_child) {
        start_req = impl_->start_child->measure_for_diagnostics(Constraints::unbounded());
    }
    if (impl_->end_child) {
        end_req = impl_->end_child->measure_for_diagnostics(Constraints::unbounded());
    }

    if (impl_->orientation == Orientation::Horizontal) {
        const float min_w = start_req.minimum_width + end_req.minimum_width + divider_width;
        const float nat_w = start_req.natural_width + end_req.natural_width + divider_width;
        const float min_h = std::max(start_req.minimum_height, end_req.minimum_height);
        const float nat_h = std::max(start_req.natural_height, end_req.natural_height);
        return {min_w, min_h, nat_w, nat_h};
    }

    const float min_w = std::max(start_req.minimum_width, end_req.minimum_width);
    const float nat_w = std::max(start_req.natural_width, end_req.natural_width);
    const float min_h = start_req.minimum_height + end_req.minimum_height + divider_width;
    const float nat_h = start_req.natural_height + end_req.natural_height + divider_width;
    return {min_w, min_h, nat_w, nat_h};
}

void SplitView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    const float divider_width = theme_number("divider-width", 8.0F);
    const bool horizontal = impl_->orientation == Orientation::Horizontal;
    const float total = horizontal ? allocation.width : allocation.height;
    const float usable = std::max(0.0F, total - divider_width);

    float start_size = usable * impl_->position;
    float end_size = usable - start_size;

    // Enforce minimum sizes.
    if (start_size < impl_->min_start && usable >= impl_->min_start + impl_->min_end) {
        start_size = impl_->min_start;
        end_size = usable - start_size;
    }
    if (end_size < impl_->min_end && usable >= impl_->min_start + impl_->min_end) {
        end_size = impl_->min_end;
        start_size = usable - end_size;
    }

    if (horizontal) {
        if (impl_->start_child) {
            impl_->start_child->allocate(
                {allocation.x, allocation.y, start_size, allocation.height});
        }
        if (impl_->end_child) {
            impl_->end_child->allocate({allocation.x + start_size + divider_width,
                                        allocation.y,
                                        end_size,
                                        allocation.height});
        }
    } else {
        if (impl_->start_child) {
            impl_->start_child->allocate(
                {allocation.x, allocation.y, allocation.width, start_size});
        }
        if (impl_->end_child) {
            impl_->end_child->allocate({allocation.x,
                                        allocation.y + start_size + divider_width,
                                        allocation.width,
                                        end_size});
        }
    }
}

bool SplitView::handle_mouse_event(const MouseEvent& event) {
    const auto a = allocation();
    const float divider_width = theme_number("divider-width", 8.0F);
    const bool horizontal = impl_->orientation == Orientation::Horizontal;
    const float total = horizontal ? a.width : a.height;
    const float usable = std::max(0.0F, total - divider_width);
    const float start_size = usable * impl_->position;

    auto divider_rect = [&]() -> Rect {
        if (horizontal) {
            return {a.x + start_size, a.y, divider_width, a.height};
        }
        return {a.x, a.y + start_size, a.width, divider_width};
    };

    const Point point{event.x, event.y};

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button != 1) {
            return false;
        }
        if (divider_rect().contains(point)) {
            impl_->dragging = true;
            impl_->drag_origin = horizontal ? point.x : point.y;
            impl_->drag_start_position = impl_->position;
            return true;
        }
        return false;
    case MouseEvent::Type::Move:
        if (!impl_->dragging) {
            return false;
        }
        {
            const float current = horizontal ? point.x : point.y;
            const float delta = current - impl_->drag_origin;
            const float new_start = usable * impl_->drag_start_position + delta;
            float fraction = usable > 0.0F ? new_start / usable : 0.5F;

            // Clamp to respect minimum sizes.
            if (usable >= impl_->min_start + impl_->min_end) {
                const float min_frac = impl_->min_start / usable;
                const float max_frac = (usable - impl_->min_end) / usable;
                fraction = std::clamp(fraction, min_frac, max_frac);
            }
            fraction = std::clamp(fraction, 0.0F, 1.0F);

            if (impl_->position != fraction) {
                impl_->position = fraction;
                impl_->position_changed.emit(fraction);
                queue_layout();
                queue_redraw();
            }
        }
        return true;
    case MouseEvent::Type::Release:
        if (event.button == 1 && impl_->dragging) {
            impl_->dragging = false;
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

CursorShape SplitView::cursor_shape() const {
    if (impl_->dragging) {
        return impl_->orientation == Orientation::Horizontal ? CursorShape::ResizeLeftRight
                                                             : CursorShape::ResizeUpDown;
    }
    return CursorShape::Default;
}

void SplitView::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float divider_width = theme_number("divider-width", 8.0F);
    const bool horizontal = impl_->orientation == Orientation::Horizontal;
    const float total = horizontal ? a.width : a.height;
    const float usable = std::max(0.0F, total - divider_width);
    const float start_size = usable * impl_->position;

    // Draw divider.
    const auto divider_color = theme_color("divider-color", Color{0.82F, 0.84F, 0.88F, 1.0F});
    if (horizontal) {
        const float line_x = a.x + start_size + (divider_width * 0.5F) - 0.5F;
        ctx.add_color_rect({line_x, a.y, 1.0F, a.height}, divider_color);
    } else {
        const float line_y = a.y + start_size + (divider_width * 0.5F) - 0.5F;
        ctx.add_color_rect({a.x, line_y, a.width, 1.0F}, divider_color);
    }

    Widget::snapshot(ctx);
}

} // namespace nk
