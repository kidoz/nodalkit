#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/popover.h>

namespace nk {

namespace {

constexpr float kGap = 8.0F;
constexpr float kArrow = 10.0F;

struct Placement {
    Rect popup;
    Rect arrow;
    bool above = false;
};

Placement place_popover(Rect anchor, float popup_w, float popup_h, Size window) {
    float x = anchor.x + ((anchor.width - popup_w) * 0.5F);
    float y = anchor.bottom() + kGap;
    bool above = false;
    if (window.height > 0.0F && y + popup_h > window.height && anchor.y >= popup_h + kGap) {
        y = anchor.y - popup_h - kGap;
        above = true;
    }
    if (window.width > 0.0F) {
        x = std::clamp(x, 4.0F, std::max(4.0F, window.width - popup_w - 4.0F));
    }
    if (window.height > 0.0F) {
        y = std::clamp(y, 4.0F, std::max(4.0F, window.height - popup_h - 4.0F));
    }

    const float anchor_center = anchor.x + (anchor.width * 0.5F);
    const float min_arrow_x = x + 8.0F;
    const float max_arrow_x = std::max(min_arrow_x, x + popup_w - kArrow - 8.0F);
    const float arrow_x = std::clamp(anchor_center - (kArrow * 0.5F), min_arrow_x, max_arrow_x);
    const float arrow_y = above ? y + popup_h - (kArrow * 0.5F) : y - (kArrow * 0.5F);
    return {
        .popup = {x, y, popup_w, popup_h},
        .arrow = {arrow_x, arrow_y, kArrow, kArrow},
        .above = above,
    };
}

Widget* deepest_hit(Widget* widget, Point point) {
    if (widget == nullptr || !widget->is_visible() || !widget->hit_test(point)) {
        return nullptr;
    }
    const auto children = widget->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (auto* hit = deepest_hit(it->get(), point)) {
            return hit;
        }
    }
    return widget;
}

Widget* first_focusable(Widget* widget) {
    if (widget == nullptr || !widget->is_visible() || !widget->is_sensitive()) {
        return nullptr;
    }
    if (widget->is_focusable()) {
        return widget;
    }
    for (const auto& child : widget->children()) {
        if (auto* found = first_focusable(child.get())) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

struct Popover::Impl {
    std::shared_ptr<Widget> child;
    std::weak_ptr<Widget> anchor_widget;
    std::weak_ptr<Widget> focus_restore;
    bool open = false;
    bool dismissing = false;
    bool above = false;
    Point anchor{};
    Signal<> dismissed;
    Rect popup_rect{};
    Rect arrow_rect{};
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
    if (impl_->open) {
        update_popup_geometry();
    }
    queue_layout();
    queue_redraw();
}

void Popover::set_anchor(Widget& anchor) {
    impl_->anchor_widget = anchor.weak_from_this();
    if (impl_->open) {
        open_beside(anchor.allocation());
    }
}

void Popover::clear_anchor() {
    impl_->anchor_widget.reset();
}

void Popover::show() {
    if (auto anchor = impl_->anchor_widget.lock()) {
        open_beside(anchor->allocation());
    }
}

void Popover::show_at(Point anchor) {
    impl_->anchor_widget.reset();
    impl_->anchor = anchor;
    open_beside({anchor.x, anchor.y, 0.0F, 0.0F});
}

void Popover::open_beside(Rect anchor_rect) {
    if (!impl_->open) {
        if (auto* focused = host_focused_widget();
            focused != nullptr && !contains_widget(focused)) {
            impl_->focus_restore = focused->weak_from_this();
        }
    }
    impl_->anchor = {anchor_rect.x + (anchor_rect.width * 0.5F), anchor_rect.bottom()};
    impl_->open = true;
    set_visible(true);
    update_popup_geometry();
    if (auto* target = first_focusable(impl_->child.get())) {
        target->grab_focus();
    } else {
        set_focusable(true);
        grab_focus();
    }
    preserve_damage_regions_for_next_redraw();
    queue_redraw();
}

void Popover::update_popup_geometry() {
    if (!impl_->open) {
        impl_->popup_rect = {};
        impl_->arrow_rect = {};
        return;
    }

    const float padding = theme_number("padding", 8.0F);
    float content_w = 24.0F;
    float content_h = 24.0F;
    if (impl_->child) {
        const auto child_req = impl_->child->measure_for_diagnostics(Constraints::unbounded());
        content_w = std::max(content_w, child_req.natural_width);
        content_h = std::max(content_h, child_req.natural_height);
    }
    const float popup_w = content_w + (padding * 2.0F);
    const float popup_h = content_h + (padding * 2.0F);

    Rect anchor_rect{impl_->anchor.x, impl_->anchor.y, 0.0F, 0.0F};
    if (auto anchor = impl_->anchor_widget.lock()) {
        anchor_rect = anchor->allocation();
    }
    const Size window_size = host_window() != nullptr ? host_window()->size() : Size{};
    const auto placed = place_popover(anchor_rect, popup_w, popup_h, window_size);
    impl_->popup_rect = placed.popup;
    impl_->arrow_rect = placed.arrow;
    impl_->above = placed.above;
    if (impl_->child) {
        impl_->child->allocate(
            {placed.popup.x + padding, placed.popup.y + padding, content_w, content_h});
    }
}

void Popover::dismiss() {
    if (!impl_->open || impl_->dismissing) {
        return;
    }
    impl_->dismissing = true;
    preserve_damage_regions_for_next_redraw();
    impl_->open = false;
    impl_->popup_rect = {};
    impl_->arrow_rect = {};
    set_focusable(false);
    auto restore = std::move(impl_->focus_restore);
    impl_->focus_restore.reset();
    if (auto previous = restore.lock()) {
        const auto* focused = host_focused_widget();
        if (focused == nullptr || focused == this || contains_widget(focused)) {
            previous->grab_focus();
        }
    }
    impl_->dismissing = false;
    impl_->dismissed.emit();
    queue_redraw();
}

bool Popover::is_open() const {
    return impl_->open;
}

Signal<>& Popover::on_dismissed() {
    return impl_->dismissed;
}

bool Popover::contains_widget(const Widget* widget) const {
    for (const auto* current = widget; current != nullptr; current = current->parent()) {
        if (current == this) {
            return true;
        }
    }
    return false;
}

Widget* Popover::target_at(Point point) const {
    return deepest_hit(impl_->child.get(), point);
}

SizeRequest Popover::measure(const Constraints& /*constraints*/) const {
    return {0.0F, 0.0F, 0.0F, 0.0F};
}

void Popover::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    update_popup_geometry();
}

bool Popover::hit_test(Point /*point*/) const {
    // An open popover covers the window so a press outside its body reaches
    // this widget and dismisses it. Descendants are still found first.
    return impl_->open;
}

std::vector<Rect> Popover::damage_regions() const {
    if (!impl_->open || impl_->popup_rect.width <= 0.0F) {
        return {};
    }
    constexpr float shadow_extend = 8.0F;
    return {{
        std::min(impl_->popup_rect.x, impl_->arrow_rect.x) - shadow_extend,
        std::min(impl_->popup_rect.y, impl_->arrow_rect.y) - shadow_extend,
        std::max(impl_->popup_rect.right(), impl_->arrow_rect.right()) -
            std::min(impl_->popup_rect.x, impl_->arrow_rect.x) + (shadow_extend * 2.0F),
        std::max(impl_->popup_rect.bottom(), impl_->arrow_rect.bottom()) -
            std::min(impl_->popup_rect.y, impl_->arrow_rect.y) + (shadow_extend * 2.0F),
    }};
}

bool Popover::handle_mouse_event(const MouseEvent& event) {
    if (!impl_->open) {
        return false;
    }
    const Point point{event.x, event.y};
    const bool inside = impl_->popup_rect.contains(point) || impl_->arrow_rect.contains(point);
    if (event.type == MouseEvent::Type::Press && event.button == 1 && !inside) {
        dismiss();
        return true;
    }
    if (auto* target = target_at(point); target != nullptr && target->handle_mouse_event(event)) {
        return true;
    }
    return inside;
}

bool Popover::handle_key_event(const KeyEvent& event) {
    if (!impl_->open || event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (event.key == KeyCode::Escape) {
        dismiss();
        return true;
    }
    if (auto* focused = host_focused_widget();
        focused != nullptr && focused != this && contains_widget(focused)) {
        return focused->handle_key_event(event);
    }
    return false;
}

void Popover::snapshot(SnapshotContext& ctx) const {
    if (!impl_->open) {
        return;
    }

    const float corner_radius = theme_number("corner-radius", 12.0F);
    const auto& popup = impl_->popup_rect;
    const auto background = theme_color("background", Color{0.98F, 0.98F, 0.99F, 1.0F});

    ctx.push_overlay_container(popup);
    ctx.add_rounded_rect({popup.x, popup.y + 2.0F, popup.width, popup.height},
                         Color{0.08F, 0.12F, 0.18F, 0.12F},
                         corner_radius + 1.0F);
    ctx.add_rounded_rect(impl_->arrow_rect, background, 2.0F);
    ctx.add_rounded_rect(popup, background, corner_radius);
    ctx.add_border(popup, theme_color("border-color"), 1.0F, corner_radius);
    if (impl_->child) {
        Widget::snapshot(ctx);
    }
    ctx.pop_container();
}

} // namespace nk
