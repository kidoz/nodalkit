#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/expander.h>

namespace nk {

namespace {

FontDescriptor expander_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

} // namespace

struct Expander::Impl {
    std::string title;
    bool expanded = false;
    std::shared_ptr<Widget> child;
    Signal<bool> expanded_changed;
};

std::shared_ptr<Expander> Expander::create(std::string title) {
    return std::shared_ptr<Expander>(new Expander(std::move(title)));
}

Expander::Expander(std::string title) : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    set_focusable(true);
    add_style_class("expander");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::ToggleButton);
    accessible.set_name(impl_->title);
    accessible.add_action(AccessibleAction::Toggle, [this]() {
        set_expanded(!impl_->expanded);
        return true;
    });
}

Expander::~Expander() = default;

std::string_view Expander::title() const {
    return impl_->title;
}

void Expander::set_title(std::string title) {
    if (impl_->title != title) {
        impl_->title = std::move(title);
        ensure_accessible().set_name(impl_->title);
        queue_layout();
        queue_redraw();
    }
}

bool Expander::is_expanded() const {
    return impl_->expanded;
}

void Expander::set_expanded(bool expanded) {
    if (impl_->expanded == expanded) {
        return;
    }
    impl_->expanded = expanded;
    if (impl_->child) {
        impl_->child->set_visible(expanded);
    }
    impl_->expanded_changed.emit(expanded);
    queue_layout();
    queue_redraw();
}

void Expander::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child) {
        impl_->child->set_visible(impl_->expanded);
        append_child(impl_->child);
    }
    queue_layout();
    queue_redraw();
}

Signal<bool>& Expander::on_expanded_changed() {
    return impl_->expanded_changed;
}

SizeRequest Expander::measure(const Constraints& /*constraints*/) const {
    const auto font = expander_font();
    const auto title_size = measure_text(impl_->title, font);
    const float header_height = theme_number("header-height", 36.0F);
    const float arrow_space = theme_number("arrow-space", 24.0F);
    const float padding_x = theme_number("padding-x", 12.0F);

    float nat_w = title_size.width + arrow_space + (padding_x * 2.0F);
    float nat_h = header_height;

    if (impl_->expanded && impl_->child) {
        const auto child_req = impl_->child->measure_for_diagnostics(Constraints::unbounded());
        nat_w = std::max(nat_w, child_req.natural_width);
        nat_h += child_req.natural_height;
    }

    return {nat_w, header_height, nat_w, nat_h};
}

void Expander::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    const float header_height = theme_number("header-height", 36.0F);

    if (impl_->expanded && impl_->child) {
        const float child_y = allocation.y + header_height;
        const float child_h = std::max(0.0F, allocation.height - header_height);
        impl_->child->allocate({allocation.x, child_y, allocation.width, child_h});
    }
}

bool Expander::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        return allocation().contains({event.x, event.y});
    case MouseEvent::Type::Release: {
        const auto a = allocation();
        const float header_height = theme_number("header-height", 36.0F);
        const Point point{event.x, event.y};
        if (point.x >= a.x && point.x < a.right() && point.y >= a.y &&
            point.y < a.y + header_height) {
            set_expanded(!impl_->expanded);
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool Expander::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        set_expanded(!impl_->expanded);
        return true;
    }

    return false;
}

CursorShape Expander::cursor_shape() const {
    return CursorShape::PointingHand;
}

void Expander::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float header_height = theme_number("header-height", 36.0F);
    const float corner_radius = theme_number("corner-radius", 8.0F);
    const float arrow_space = theme_number("arrow-space", 24.0F);
    const float padding_x = theme_number("padding-x", 12.0F);
    const auto font = expander_font();

    // Header background.
    const Rect header{a.x, a.y, a.width, header_height};
    ctx.add_rounded_rect(header,
                         theme_color("header-background", Color{0.94F, 0.95F, 0.97F, 1.0F}),
                         corner_radius);

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_border(header,
                       theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                       1.5F,
                       corner_radius);
    }

    // Arrow indicator.
    const auto arrow_font = FontDescriptor{
        .family = {},
        .size = 12.0F,
        .weight = FontWeight::Regular,
    };
    const std::string arrow = impl_->expanded ? "\xe2\x96\xbe" : "\xe2\x96\xb8"; // ▾ or ▸
    const auto arrow_size = measure_text(arrow, arrow_font);
    const float arrow_x = a.x + padding_x;
    const float arrow_y = a.y + std::max(0.0F, (header_height - arrow_size.height) * 0.5F);
    ctx.add_text({arrow_x, arrow_y},
                 arrow,
                 theme_color("arrow-color", Color{0.3F, 0.3F, 0.3F, 1.0F}),
                 arrow_font);

    // Title text.
    const auto title_size = measure_text(impl_->title, font);
    const float title_x = a.x + padding_x + arrow_space;
    const float title_y = a.y + std::max(0.0F, (header_height - title_size.height) * 0.5F);
    ctx.add_text({title_x, title_y},
                 std::string(impl_->title),
                 theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
                 font);

    // Child content.
    if (impl_->expanded && impl_->child) {
        Widget::snapshot(ctx);
    }
}

} // namespace nk
