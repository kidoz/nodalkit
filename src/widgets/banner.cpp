#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/banner.h>
#include <nk/widgets/revealer.h>

namespace nk {

class Banner;

struct BannerState {
    std::string title;
    std::string button_label;
    Signal<> clicked;
    bool revealed = true;
};

class BannerBody : public Widget {
public:
    explicit BannerBody(BannerState& state) : state_(&state) {
        add_style_class("banner");
        ensure_accessible().set_hidden(true);
    }

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override {
        const auto font = FontDescriptor{
            .family = {}, .size = theme_number("font-size", 13.0F), .weight = FontWeight::Medium};
        const auto text = measure_text(state_->title, font);
        const auto action = measure_text(state_->button_label, font);
        const float width =
            text.width + action.width + (state_->button_label.empty() ? 32.0F : 64.0F);
        const float clamped = std::min(width, constraints.max_width);
        return {clamped, 46.0F, clamped, 46.0F};
    }

protected:
    void snapshot(SnapshotContext& ctx) const override {
        const auto bounds = allocation();
        const auto font = FontDescriptor{
            .family = {}, .size = theme_number("font-size", 13.0F), .weight = FontWeight::Medium};
        ctx.add_color_rect(
            bounds,
            theme_color("background", theme_color("accent-soft", Color::from_rgb(222, 236, 252))));
        ctx.add_color_rect({bounds.x, bounds.bottom() - 1.0F, bounds.width, 1.0F},
                           theme_color("border-color"));
        const auto text = measure_text(state_->title, font);
        ctx.add_text({bounds.x + 16.0F, bounds.y + ((bounds.height - text.height) * 0.5F)},
                     state_->title,
                     theme_color("text-color"),
                     font);
        if (!state_->button_label.empty()) {
            const auto action = measure_text(state_->button_label, font);
            ctx.add_text(
                {bounds.right() - action.width - 16.0F,
                 bounds.y + ((bounds.height - action.height) * 0.5F)},
                state_->button_label,
                theme_color("action-color", theme_color("accent", Color::from_rgb(53, 132, 228))),
                font);
        }
    }

private:
    BannerState* state_ = nullptr;
};

struct Banner::Impl {
    BannerState state;
    std::shared_ptr<BannerBody> body;
    std::shared_ptr<Revealer> revealer;
    ScopedConnection settled;
};

std::shared_ptr<Banner> Banner::create(std::string title) {
    return std::shared_ptr<Banner>(new Banner(std::move(title)));
}

Banner::Banner(std::string title) : impl_(std::make_unique<Impl>()) {
    impl_->state.title = std::move(title);
    add_style_class("banner");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Status);
    accessible.set_name(impl_->state.title);
    impl_->body = std::make_shared<BannerBody>(impl_->state);
    impl_->revealer = Revealer::create();
    impl_->revealer->set_child(impl_->body);
    append_child(impl_->revealer);
    impl_->settled = ScopedConnection{impl_->revealer->on_settled().connect([this] {
        if (!impl_->revealer->is_revealed() && impl_->revealer->reveal_fraction() <= 0.0F) {
            set_visible(false);
        }
    })};
}

Banner::~Banner() = default;

std::string_view Banner::title() const {
    return impl_->state.title;
}

void Banner::set_title(std::string title) {
    if (impl_->state.title != title) {
        impl_->state.title = std::move(title);
        ensure_accessible().set_name(impl_->state.title);
        queue_layout();
    }
}

std::string_view Banner::button_label() const {
    return impl_->state.button_label;
}

void Banner::set_button_label(std::string label) {
    if (impl_->state.button_label != label) {
        impl_->state.button_label = std::move(label);
        set_focusable(!impl_->state.button_label.empty());
        auto& accessible = ensure_accessible();
        if (impl_->state.button_label.empty()) {
            accessible.remove_action(AccessibleAction::Activate);
        } else {
            accessible.add_action(AccessibleAction::Activate, [this] {
                impl_->state.clicked.emit();
                return true;
            });
        }
        queue_layout();
    }
}

bool Banner::is_revealed() const {
    return impl_->state.revealed;
}

void Banner::set_revealed(bool revealed) {
    if (impl_->state.revealed == revealed) {
        return;
    }
    impl_->state.revealed = revealed;
    if (revealed) {
        set_visible(true);
    }
    impl_->revealer->set_revealed(revealed);
}

Signal<>& Banner::on_button_clicked() {
    return impl_->state.clicked;
}

SizeRequest Banner::measure(const Constraints& constraints) const {
    if (!is_visible()) {
        return {};
    }
    return impl_->revealer->measure(constraints);
}

void Banner::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    impl_->revealer->allocate(allocation);
}

bool Banner::handle_mouse_event(const MouseEvent& event) {
    if (impl_->state.button_label.empty() || event.button != 1 ||
        event.type != MouseEvent::Type::Release) {
        return false;
    }
    const float action_width = std::max(80.0F, allocation().width * 0.25F);
    if (event.x >= allocation().right() - action_width &&
        allocation().contains({event.x, event.y})) {
        impl_->state.clicked.emit();
        return true;
    }
    return false;
}

bool Banner::handle_key_event(const KeyEvent& event) {
    if (!impl_->state.button_label.empty() && event.type == KeyEvent::Type::Press &&
        (event.key == KeyCode::Space || event.key == KeyCode::Return)) {
        impl_->state.clicked.emit();
        return true;
    }
    return false;
}

} // namespace nk
