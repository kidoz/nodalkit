#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/banner.h>

namespace nk {

struct Banner::Impl {
    std::string title;
    std::string button_label;
    Signal<> clicked;
    bool revealed = true;
};

std::shared_ptr<Banner> Banner::create(std::string title) {
    return std::shared_ptr<Banner>(new Banner(std::move(title)));
}

Banner::Banner(std::string title) : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    add_style_class("banner");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Status);
    accessible.set_name(impl_->title);
}

Banner::~Banner() = default;

std::string_view Banner::title() const {
    return impl_->title;
}

void Banner::set_title(std::string title) {
    if (impl_->title != title) {
        impl_->title = std::move(title);
        ensure_accessible().set_name(impl_->title);
        queue_layout();
    }
}

std::string_view Banner::button_label() const {
    return impl_->button_label;
}

void Banner::set_button_label(std::string label) {
    if (impl_->button_label != label) {
        impl_->button_label = std::move(label);
        set_focusable(!impl_->button_label.empty());
        auto& accessible = ensure_accessible();
        if (impl_->button_label.empty()) {
            accessible.remove_action(AccessibleAction::Activate);
        } else {
            accessible.add_action(AccessibleAction::Activate, [this] {
                impl_->clicked.emit();
                return true;
            });
        }
        queue_layout();
    }
}

bool Banner::is_revealed() const {
    return impl_->revealed;
}

void Banner::set_revealed(bool revealed) {
    if (impl_->revealed != revealed) {
        impl_->revealed = revealed;
        set_visible(revealed);
    }
}

Signal<>& Banner::on_button_clicked() {
    return impl_->clicked;
}

SizeRequest Banner::measure(const Constraints& constraints) const {
    if (!impl_->revealed) {
        return {};
    }
    const auto font = FontDescriptor{
        .family = {}, .size = theme_number("font-size", 13.0F), .weight = FontWeight::Medium};
    const auto text = measure_text(impl_->title, font);
    const auto action = measure_text(impl_->button_label, font);
    const float width = text.width + action.width + (impl_->button_label.empty() ? 32.0F : 64.0F);
    return {std::min(width, constraints.max_width),
            46.0F,
            std::min(width, constraints.max_width),
            46.0F};
}

bool Banner::handle_mouse_event(const MouseEvent& event) {
    if (impl_->button_label.empty() || event.button != 1 ||
        event.type != MouseEvent::Type::Release) {
        return false;
    }
    const float action_width = std::max(80.0F, allocation().width * 0.25F);
    if (event.x >= allocation().right() - action_width &&
        allocation().contains({event.x, event.y})) {
        impl_->clicked.emit();
        return true;
    }
    return false;
}

bool Banner::handle_key_event(const KeyEvent& event) {
    if (!impl_->button_label.empty() && event.type == KeyEvent::Type::Press &&
        (event.key == KeyCode::Space || event.key == KeyCode::Return)) {
        impl_->clicked.emit();
        return true;
    }
    return false;
}

void Banner::snapshot(SnapshotContext& ctx) const {
    if (!impl_->revealed) {
        return;
    }
    const auto bounds = allocation();
    const auto font = FontDescriptor{
        .family = {}, .size = theme_number("font-size", 13.0F), .weight = FontWeight::Medium};
    ctx.add_color_rect(
        bounds,
        theme_color("background", theme_color("accent-soft", Color::from_rgb(222, 236, 252))));
    ctx.add_color_rect({bounds.x, bounds.bottom() - 1.0F, bounds.width, 1.0F},
                       theme_color("border-color"));
    const auto text = measure_text(impl_->title, font);
    ctx.add_text({bounds.x + 16.0F, bounds.y + ((bounds.height - text.height) * 0.5F)},
                 impl_->title,
                 theme_color("text-color"),
                 font);
    if (!impl_->button_label.empty()) {
        const auto action = measure_text(impl_->button_label, font);
        ctx.add_text(
            {bounds.right() - action.width - 16.0F,
             bounds.y + ((bounds.height - action.height) * 0.5F)},
            impl_->button_label,
            theme_color("action-color", theme_color("accent", Color::from_rgb(53, 132, 228))),
            font);
    }
}

} // namespace nk
