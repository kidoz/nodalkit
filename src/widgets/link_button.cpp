#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/link_button.h>
#include <utility>

namespace nk {

namespace {

FontDescriptor link_font() {
    return FontDescriptor{.family = {}, .size = 13.5F, .weight = FontWeight::Medium};
}

} // namespace

struct LinkButton::Impl {
    std::string label;
    std::string target;
    Signal<std::string_view> activated;
    bool armed = false;
};

std::shared_ptr<LinkButton> LinkButton::create(std::string label, std::string target) {
    return std::shared_ptr<LinkButton>(new LinkButton(std::move(label), std::move(target)));
}

LinkButton::LinkButton(std::string label, std::string target) : impl_(std::make_unique<Impl>()) {
    impl_->label = std::move(label);
    impl_->target = std::move(target);
    set_focusable(true);
    add_style_class("link-button");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Link);
    accessible.set_name(impl_->label);
    accessible.add_action(AccessibleAction::Activate, [this] {
        activate();
        return true;
    });
}

LinkButton::~LinkButton() = default;

std::string_view LinkButton::label() const {
    return impl_->label;
}

void LinkButton::set_label(std::string label) {
    if (impl_->label == label) {
        return;
    }
    impl_->label = std::move(label);
    ensure_accessible().set_name(impl_->label);
    queue_layout();
    queue_redraw();
}

std::string_view LinkButton::target() const {
    return impl_->target;
}

void LinkButton::set_target(std::string target) {
    impl_->target = std::move(target);
}

Signal<std::string_view>& LinkButton::on_activated() {
    return impl_->activated;
}

void LinkButton::activate() {
    if (!is_sensitive()) {
        return;
    }
    const std::string_view reported =
        impl_->target.empty() ? std::string_view{impl_->label} : std::string_view{impl_->target};
    impl_->activated.emit(reported);
}

SizeRequest LinkButton::measure(const Constraints& constraints) const {
    const auto measured = measure_text(impl_->label, link_font());
    const float width = std::min(measured.width, constraints.max_width);
    const float height = measured.height;
    return {width, height, width, height};
}

bool LinkButton::handle_mouse_event(const MouseEvent& event) {
    if (!is_sensitive() || event.button != 1) {
        impl_->armed = false;
        return false;
    }
    const Point point{event.x, event.y};
    const bool inside = allocation().contains(point);
    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed = inside;
        queue_redraw();
        return inside;
    case MouseEvent::Type::Release: {
        const bool activate_link = impl_->armed && inside;
        impl_->armed = false;
        queue_redraw();
        if (activate_link) {
            activate();
        }
        return activate_link;
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

bool LinkButton::handle_key_event(const KeyEvent& event) {
    if (!is_sensitive() || event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (event.key == KeyCode::Return || event.key == KeyCode::Space) {
        activate();
        return true;
    }
    return false;
}

CursorShape LinkButton::cursor_shape() const {
    return CursorShape::PointingHand;
}

void LinkButton::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    const auto font = link_font();
    const auto measured = measure_text(impl_->label, font);
    const auto color =
        theme_color("link-color", theme_color("accent", Color::from_rgb(53, 132, 228)));
    const float text_y = bounds.y + std::max(0.0F, (bounds.height - measured.height) * 0.5F);
    add_text_elided(ctx, {bounds.x, text_y}, impl_->label, measured, bounds.width, color, font);
    const float underline_y = std::min(bounds.bottom() - 1.0F, text_y + measured.height);
    ctx.add_color_rect({bounds.x, underline_y, std::min(bounds.width, measured.width), 1.0F},
                       color);
}

} // namespace nk
