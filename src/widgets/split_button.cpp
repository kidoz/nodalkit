#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/button_content.h>
#include <nk/widgets/context_menu.h>
#include <nk/widgets/split_button.h>
#include <utility>

namespace nk {

namespace {

enum class Arm { None, Primary, Menu };

} // namespace

struct SplitButton::Impl {
    explicit Impl(std::string label)
        : label(std::move(label))
        , menu(ContextMenu::create())
        , content(ButtonContent::create(this->label)) {}

    std::string label;
    std::shared_ptr<ContextMenu> menu;
    std::shared_ptr<ButtonContent> content;
    std::shared_ptr<Action> action;
    ScopedConnection action_changed;
    Signal<> clicked;
    Arm armed = Arm::None;
};

std::shared_ptr<SplitButton> SplitButton::create(std::string label) {
    return std::shared_ptr<SplitButton>(new SplitButton(std::move(label)));
}

SplitButton::SplitButton(std::string label) : impl_(std::make_unique<Impl>(std::move(label))) {
    set_focusable(true);
    add_style_class("split-button");
    impl_->content->set_shows_indicator(true);
    append_child(impl_->menu);
    append_child(impl_->content);
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Button);
    accessible.set_name(impl_->label);
    accessible.add_action(AccessibleAction::Activate, [this] { return activate_primary(); });
}

SplitButton::~SplitButton() = default;

std::string_view SplitButton::label() const {
    return impl_->label;
}

void SplitButton::set_label(std::string label) {
    if (impl_->label == label) {
        return;
    }
    impl_->label = std::move(label);
    sync_content_label();
    ensure_accessible().set_name(impl_->label);
    queue_layout();
    queue_redraw();
}

void SplitButton::set_action(std::shared_ptr<Action> action) {
    impl_->action_changed.disconnect();
    impl_->action = std::move(action);
    if (impl_->action) {
        impl_->action_changed =
            ScopedConnection{impl_->action->on_changed().connect([weak = weak_from_this()] {
                if (auto widget = weak.lock()) {
                    static_cast<SplitButton&>(*widget).sync_action();
                }
            })};
    }
    sync_action();
}

Action* SplitButton::action() const {
    return impl_->action.get();
}

Signal<>& SplitButton::on_clicked() {
    return impl_->clicked;
}

void SplitButton::set_items(std::vector<NativeMenuItem> items) {
    impl_->menu->set_items(std::move(items));
}

void SplitButton::set_action_group(std::shared_ptr<ActionGroup> actions) {
    impl_->menu->set_action_group(std::move(actions));
}

ActionGroup* SplitButton::action_group() const {
    return impl_->menu->action_group();
}

bool SplitButton::is_open() const {
    return impl_->menu->is_open();
}

Signal<int>& SplitButton::on_item_activated() {
    return impl_->menu->on_item_activated();
}

Signal<std::string_view>& SplitButton::on_action() {
    return impl_->menu->on_action();
}

Signal<>& SplitButton::on_dismissed() {
    return impl_->menu->on_dismissed();
}

Rect SplitButton::menu_rect() const {
    const auto bounds = allocation();
    const float width = std::min(theme_number("menu-width", 32.0F), bounds.width);
    return {bounds.right() - width, bounds.y, width, bounds.height};
}

void SplitButton::toggle_menu() {
    if (!is_sensitive()) {
        return;
    }
    if (impl_->menu->is_open()) {
        impl_->menu->dismiss();
        return;
    }
    if (impl_->menu->item_count() == 0) {
        return;
    }
    const auto bounds = allocation();
    impl_->menu->show_at({bounds.x, bounds.bottom()});
}

bool SplitButton::activate_primary() {
    if (!is_sensitive()) {
        return false;
    }
    if (impl_->menu->is_open()) {
        impl_->menu->dismiss();
    }
    if (auto bound = impl_->action) {
        if (!bound->is_enabled()) {
            return false;
        }
        bound->activate();
    } else {
        impl_->clicked.emit();
    }
    return true;
}

void SplitButton::sync_action() {
    const auto bound = impl_->action;
    ensure_accessible().set_role(bound && bound->is_checkable() ? AccessibleRole::ToggleButton
                                                                : AccessibleRole::Button);
    set_state_flag(StateFlags::Checked, bound && bound->is_checked());
    if (bound && !bound->is_enabled()) {
        impl_->armed = Arm::None;
    }
    set_sensitive(!bound || bound->is_enabled());
    sync_content_label();
    queue_layout();
    queue_redraw();
}

void SplitButton::sync_content_label() {
    const auto text =
        has_flag(state_flags(), StateFlags::Checked) ? "\u2713 " + impl_->label : impl_->label;
    impl_->content->set_label(text);
}

SizeRequest SplitButton::measure(const Constraints& constraints) const {
    const auto content = impl_->content->measure(constraints);
    const float padding_x = theme_number("padding-x", 16.0F);
    const float min_width = theme_number("min-width", 82.0F);
    const float min_height = theme_number("min-height", 36.0F);
    const float width = std::max(min_width, content.natural_width + (padding_x * 2.0F));
    const float height = std::max(min_height, content.natural_height + 12.0F);
    return {width, height, width, height};
}

void SplitButton::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const float inset = theme_number("padding-x", 16.0F) * 0.5F;
    impl_->content->allocate({allocation.x + inset,
                              allocation.y,
                              std::max(0.0F, allocation.width - (inset * 2.0F)),
                              allocation.height});
}

bool SplitButton::handle_mouse_event(const MouseEvent& event) {
    if (!is_sensitive()) {
        impl_->armed = Arm::None;
        return false;
    }
    if (event.button != 1) {
        return false;
    }
    const Point point{event.x, event.y};
    const bool in_bounds = allocation().contains(point);
    const bool in_menu = menu_rect().contains(point);
    switch (event.type) {
    case MouseEvent::Type::Press:
        if (!in_bounds) {
            impl_->armed = Arm::None;
            return false;
        }
        impl_->armed = in_menu ? Arm::Menu : Arm::Primary;
        queue_redraw();
        return true;
    case MouseEvent::Type::Release: {
        const auto armed = impl_->armed;
        impl_->armed = Arm::None;
        queue_redraw();
        if (!in_bounds) {
            return false;
        }
        const bool menu_release = in_menu;
        if (armed == Arm::Menu && menu_release) {
            toggle_menu();
            return true;
        }
        if (armed == Arm::Primary && !menu_release) {
            return activate_primary();
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

bool SplitButton::handle_key_event(const KeyEvent& event) {
    if (impl_->menu->is_open() && impl_->menu->handle_key_event(event)) {
        return true;
    }
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (event.key == KeyCode::Return || event.key == KeyCode::Space) {
        return activate_primary();
    }
    if (event.key == KeyCode::Down) {
        toggle_menu();
        return impl_->menu->is_open();
    }
    return false;
}

bool SplitButton::hit_test(Point point) const {
    return Widget::hit_test(point) || impl_->menu->hit_test(point);
}

std::vector<Rect> SplitButton::damage_regions() const {
    auto regions = Widget::damage_regions();
    const auto menu_regions = impl_->menu->damage_regions();
    regions.insert(regions.end(), menu_regions.begin(), menu_regions.end());
    return regions;
}

CursorShape SplitButton::cursor_shape() const {
    return CursorShape::PointingHand;
}

void SplitButton::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    auto body = bounds;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(bounds, theme_color("focus-ring-color"), corner_radius + 2.0F);
        body = {bounds.x + 2.0F, bounds.y + 2.0F, bounds.width - 4.0F, bounds.height - 4.0F};
    }

    const auto menu = menu_rect();
    const auto background = theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto pressed = theme_color("pressed-background", Color{0.88F, 0.9F, 0.93F, 1.0F});
    ctx.add_rounded_rect(body, background, corner_radius);
    if (impl_->armed == Arm::Primary || impl_->menu->is_open()) {
        ctx.add_rounded_rect(
            {body.x, body.y, std::max(0.0F, menu.x - body.x), body.height}, pressed, corner_radius);
    }
    if (impl_->armed == Arm::Menu || impl_->menu->is_open()) {
        ctx.add_rounded_rect(menu, pressed, corner_radius);
    }
    ctx.add_border(
        body, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, corner_radius);
    ctx.add_line({menu.x, body.y + 6.0F},
                 {menu.x, body.bottom() - 6.0F},
                 theme_color("separator-color", Color{0.78F, 0.8F, 0.84F, 1.0F}),
                 1.0F);

    Widget::snapshot(ctx);
}

} // namespace nk
