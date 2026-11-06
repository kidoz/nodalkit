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

FontDescriptor subtitle_font() {
    return FontDescriptor{
        .family = {},
        .size = 12.0F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

struct Expander::Impl {
    std::string title;
    std::string subtitle;
    bool expanded = false;
    std::shared_ptr<Widget> child;
    std::shared_ptr<Widget> suffix;
    Rect suffix_bounds{};
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

std::string_view Expander::subtitle() const {
    return impl_->subtitle;
}

void Expander::set_subtitle(std::string subtitle) {
    if (impl_->subtitle == subtitle) {
        return;
    }
    impl_->subtitle = std::move(subtitle);
    ensure_accessible().set_description(impl_->subtitle);
    queue_layout();
    queue_redraw();
}

void Expander::set_suffix(std::shared_ptr<Widget> suffix) {
    if (impl_->suffix == suffix) {
        return;
    }
    if (impl_->suffix != nullptr) {
        remove_child(*impl_->suffix);
    }
    impl_->suffix = std::move(suffix);
    impl_->suffix_bounds = {};
    if (impl_->suffix != nullptr) {
        append_child(impl_->suffix);
    }
    queue_layout();
    queue_redraw();
}

Widget* Expander::suffix() const {
    return impl_->suffix.get();
}

float Expander::header_height() const {
    const float minimum = theme_number("header-height", 36.0F);
    if (impl_->subtitle.empty()) {
        return minimum;
    }
    const auto title = measure_text(impl_->title, expander_font());
    const auto subtitle = measure_text(impl_->subtitle, subtitle_font());
    const float gap = theme_number("subtitle-gap", 2.0F);
    const float padding_y = theme_number("padding-y", 8.0F);
    return std::max(minimum, padding_y + title.height + gap + subtitle.height + padding_y);
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
    const auto title_size = measure_text(impl_->title, expander_font());
    const auto subtitle_size = measure_text(impl_->subtitle, subtitle_font());
    const float header = header_height();
    const float arrow_space = theme_number("arrow-space", 24.0F);
    const float padding_x = theme_number("padding-x", 12.0F);
    const float text_width =
        std::max(title_size.width, impl_->subtitle.empty() ? 0.0F : subtitle_size.width);
    float suffix_width = 0.0F;
    if (impl_->suffix != nullptr) {
        suffix_width =
            impl_->suffix->measure_for_diagnostics(Constraints::unbounded()).natural_width +
            theme_number("suffix-spacing", 8.0F);
    }

    float nat_w = text_width + arrow_space + (padding_x * 2.0F) + suffix_width;
    float nat_h = header;

    if (impl_->expanded && impl_->child) {
        const auto child_req = impl_->child->measure_for_diagnostics(Constraints::unbounded());
        nat_w = std::max(nat_w, child_req.natural_width);
        nat_h += child_req.natural_height;
    }

    return {nat_w, header, nat_w, nat_h};
}

void Expander::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    const float header = header_height();
    const float padding_x = theme_number("padding-x", 12.0F);
    impl_->suffix_bounds = {};
    if (impl_->suffix != nullptr) {
        const auto request = impl_->suffix->measure(Constraints::unbounded());
        const float suffix_height = std::min(request.natural_height, std::max(0.0F, header - 8.0F));
        const float suffix_width =
            std::min(request.natural_width, std::max(0.0F, allocation.width - (padding_x * 2.0F)));
        const float x = allocation.right() - padding_x - suffix_width;
        const float y = allocation.y + std::max(0.0F, (header - suffix_height) * 0.5F);
        impl_->suffix_bounds = {x, y, suffix_width, suffix_height};
        impl_->suffix->allocate(impl_->suffix_bounds);
    }

    if (impl_->expanded && impl_->child) {
        const float child_y = allocation.y + header;
        const float child_h = std::max(0.0F, allocation.height - header);
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
        const auto bounds = allocation();
        const float header = header_height();
        const Point point{event.x, event.y};
        const bool in_header = point.x >= bounds.x && point.x < bounds.right() &&
                               point.y >= bounds.y && point.y < bounds.y + header;
        if (in_header && !impl_->suffix_bounds.contains(point)) {
            set_expanded(!impl_->expanded);
            return true;
        }
        return false;
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
    const auto bounds = allocation();
    const float header = header_height();
    const float corner_radius = theme_number("corner-radius", 8.0F);
    const float arrow_space = theme_number("arrow-space", 24.0F);
    const float padding_x = theme_number("padding-x", 12.0F);
    const float padding_y = theme_number("padding-y", 8.0F);
    const auto font = expander_font();

    const Rect header_rect{bounds.x, bounds.y, bounds.width, header};
    ctx.add_rounded_rect(header_rect,
                         theme_color("header-background", Color{0.94F, 0.95F, 0.97F, 1.0F}),
                         corner_radius);

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_border(header_rect, theme_color("focus-ring-color"), 1.5F, corner_radius);
    }

    const auto arrow_font = FontDescriptor{
        .family = {},
        .size = 12.0F,
        .weight = FontWeight::Regular,
    };
    const std::string arrow = impl_->expanded ? "\xe2\x96\xbe" : "\xe2\x96\xb8"; // ▾ or ▸
    const auto arrow_size = measure_text(arrow, arrow_font);
    const float arrow_x = bounds.x + padding_x;
    const float arrow_y = bounds.y + std::max(0.0F, (header - arrow_size.height) * 0.5F);
    ctx.add_text({arrow_x, arrow_y},
                 arrow,
                 theme_color("arrow-color", Color{0.3F, 0.3F, 0.3F, 1.0F}),
                 arrow_font);

    const auto title_size = measure_text(impl_->title, font);
    const float title_x = bounds.x + padding_x + arrow_space;
    const float text_right =
        impl_->suffix != nullptr ? impl_->suffix_bounds.x : bounds.right() - padding_x;
    const float max_text_width = std::max(0.0F, text_right - title_x);
    if (impl_->subtitle.empty()) {
        const float title_y = bounds.y + std::max(0.0F, (header - title_size.height) * 0.5F);
        add_text_elided(ctx,
                        {title_x, title_y},
                        impl_->title,
                        title_size,
                        max_text_width,
                        theme_color("text-color"),
                        font);
    } else {
        const auto subtitle_size = measure_text(impl_->subtitle, subtitle_font());
        const float title_y = bounds.y + padding_y;
        add_text_elided(ctx,
                        {title_x, title_y},
                        impl_->title,
                        title_size,
                        max_text_width,
                        theme_color("text-color"),
                        font);
        const float subtitle_y = title_y + title_size.height + theme_number("subtitle-gap", 2.0F);
        add_text_elided(ctx,
                        {title_x, subtitle_y},
                        impl_->subtitle,
                        subtitle_size,
                        max_text_width,
                        theme_color("subtitle-color", Color{0.38F, 0.40F, 0.45F, 1.0F}),
                        subtitle_font());
    }

    Widget::snapshot(ctx);
}

} // namespace nk
