#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/context_menu.h>
#include <nk/widgets/menu_button.h>
#include <utility>

namespace nk {

namespace {

FontDescriptor menu_button_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

void add_chevron(SnapshotContext& ctx, Point tip, Color color) {
    ctx.add_line({tip.x - 4.0F, tip.y - 2.0F}, {tip.x, tip.y + 2.0F}, color, 1.5F);
    ctx.add_line({tip.x, tip.y + 2.0F}, {tip.x + 4.0F, tip.y - 2.0F}, color, 1.5F);
}

} // namespace

struct MenuButton::Impl {
    explicit Impl(std::string label) : label(std::move(label)), menu(ContextMenu::create()) {}

    std::string label;
    std::shared_ptr<ContextMenu> menu;
    bool armed = false;
};

std::shared_ptr<MenuButton> MenuButton::create(std::string label) {
    return std::shared_ptr<MenuButton>(new MenuButton(std::move(label)));
}

MenuButton::MenuButton(std::string label) : impl_(std::make_unique<Impl>(std::move(label))) {
    set_focusable(true);
    add_style_class("menu-button");
    append_child(impl_->menu);
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Button);
    accessible.set_name(impl_->label);
    accessible.add_action(AccessibleAction::Activate, [this] { return activate(); });
}

MenuButton::~MenuButton() = default;

std::string_view MenuButton::label() const {
    return impl_->label;
}

void MenuButton::set_label(std::string label) {
    if (impl_->label == label) {
        return;
    }
    impl_->label = std::move(label);
    ensure_accessible().set_name(impl_->label);
    queue_layout();
    queue_redraw();
}

void MenuButton::set_items(std::vector<NativeMenuItem> items) {
    impl_->menu->set_items(std::move(items));
}

void MenuButton::set_action_group(std::shared_ptr<ActionGroup> actions) {
    impl_->menu->set_action_group(std::move(actions));
}

ActionGroup* MenuButton::action_group() const {
    return impl_->menu->action_group();
}

bool MenuButton::is_open() const {
    return impl_->menu->is_open();
}

Signal<int>& MenuButton::on_item_activated() {
    return impl_->menu->on_item_activated();
}

Signal<std::string_view>& MenuButton::on_action() {
    return impl_->menu->on_action();
}

Signal<>& MenuButton::on_dismissed() {
    return impl_->menu->on_dismissed();
}

void MenuButton::toggle_menu() {
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

bool MenuButton::activate() {
    toggle_menu();
    return impl_->menu->item_count() > 0 || impl_->menu->is_open();
}

SizeRequest MenuButton::measure(const Constraints& /*constraints*/) const {
    const auto measured = measure_text(impl_->label, menu_button_font());
    const float padding_x = theme_number("padding-x", 16.0F);
    const float min_width = theme_number("min-width", 82.0F);
    const float min_height = theme_number("min-height", 36.0F);
    constexpr float kChevronSpace = 18.0F;
    const float width = std::max(min_width, measured.width + (padding_x * 2.0F) + kChevronSpace);
    const float height = std::max(min_height, measured.height + 12.0F);
    return {width, height, width, height};
}

bool MenuButton::handle_mouse_event(const MouseEvent& event) {
    if (!is_sensitive()) {
        impl_->armed = false;
        return false;
    }
    if (event.button != 1) {
        return false;
    }
    const Point point{event.x, event.y};
    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed = allocation().contains(point);
        queue_redraw();
        return impl_->armed;
    case MouseEvent::Type::Release: {
        const bool toggle = impl_->armed && allocation().contains(point);
        impl_->armed = false;
        queue_redraw();
        if (toggle) {
            toggle_menu();
        }
        return toggle;
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

bool MenuButton::handle_key_event(const KeyEvent& event) {
    if (impl_->menu->is_open() && impl_->menu->handle_key_event(event)) {
        return true;
    }
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (event.key == KeyCode::Return || event.key == KeyCode::Space || event.key == KeyCode::Down) {
        toggle_menu();
        return impl_->menu->is_open();
    }
    return false;
}

bool MenuButton::hit_test(Point point) const {
    return Widget::hit_test(point) || impl_->menu->hit_test(point);
}

std::vector<Rect> MenuButton::damage_regions() const {
    auto regions = Widget::damage_regions();
    const auto menu_regions = impl_->menu->damage_regions();
    regions.insert(regions.end(), menu_regions.begin(), menu_regions.end());
    return regions;
}

CursorShape MenuButton::cursor_shape() const {
    return CursorShape::PointingHand;
}

void MenuButton::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    auto body = bounds;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(bounds, theme_color("focus-ring-color"), corner_radius + 2.0F);
        body = {bounds.x + 2.0F, bounds.y + 2.0F, bounds.width - 4.0F, bounds.height - 4.0F};
    }

    const bool open = impl_->menu->is_open();
    ctx.add_rounded_rect(body,
                         open || impl_->armed
                             ? theme_color("pressed-background", Color{0.88F, 0.9F, 0.93F, 1.0F})
                             : theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}),
                         corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, corner_radius);

    const auto font = menu_button_font();
    const auto measured = measure_text(impl_->label, font);
    constexpr float kChevronSpace = 18.0F;
    const float inset = theme_number("padding-x", 16.0F) * 0.5F;
    const float max_label_width = std::max(0.0F, body.width - (inset * 2.0F) - kChevronSpace);
    const float text_x = body.x + inset;
    const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
    const auto color = theme_color("text-color");
    add_text_elided(ctx, {text_x, text_y}, impl_->label, measured, max_label_width, color, font);
    add_chevron(ctx, {body.right() - 14.0F, body.y + (body.height * 0.5F)}, color);

    Widget::snapshot(ctx);
}

} // namespace nk
