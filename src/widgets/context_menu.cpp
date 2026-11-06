#include "accessible_item.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/runtime/event_loop.h>
#include <nk/text/font.h>
#include <nk/ui_core/state_flags.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/context_menu.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nk {

namespace {

FontDescriptor context_menu_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

struct PopupGeometry {
    Rect bounds;
    float item_height = 28.0F;
    float separator_height = 9.0F;
};

struct MenuHit {
    int root = -1;
    int child = -1;
    bool inside = false;
};

std::string key_label(KeyCode key) {
    const auto value = static_cast<int>(key);
    const auto letter = static_cast<int>(KeyCode::A);
    if (value >= letter && value <= static_cast<int>(KeyCode::Z)) {
        return std::string(1, static_cast<char>('A' + (value - letter)));
    }
    const auto digit = static_cast<int>(KeyCode::Num1);
    if (value >= digit && value <= static_cast<int>(KeyCode::Num0)) {
        const int offset = value - digit;
        return std::string(1, offset == 9 ? '0' : static_cast<char>('1' + offset));
    }
    const auto function = static_cast<int>(KeyCode::F1);
    if (value >= function && value <= static_cast<int>(KeyCode::F12)) {
        return "F" + std::to_string(value - function + 1);
    }
    switch (key) {
    case KeyCode::Return:
        return "Return";
    case KeyCode::Escape:
        return "Esc";
    case KeyCode::Tab:
        return "Tab";
    case KeyCode::Space:
        return "Space";
    case KeyCode::Delete:
        return "Delete";
    case KeyCode::Left:
        return "Left";
    case KeyCode::Right:
        return "Right";
    case KeyCode::Up:
        return "Up";
    case KeyCode::Down:
        return "Down";
    default:
        return {};
    }
}

std::string shortcut_label(const std::optional<NativeMenuShortcut>& shortcut) {
    if (!shortcut.has_value() || shortcut->key == KeyCode::Unknown) {
        return {};
    }
    const auto key = key_label(shortcut->key);
    if (key.empty()) {
        return {};
    }
    std::string text;
    auto append = [&text](std::string_view part) {
        if (!text.empty()) {
            text += '+';
        }
        text += part;
    };
    const auto modifiers = shortcut->modifiers;
    if ((modifiers & NativeMenuModifier::Ctrl) != NativeMenuModifier::None) {
        append("Ctrl");
    }
    if ((modifiers & NativeMenuModifier::Alt) != NativeMenuModifier::None) {
        append("Alt");
    }
    if ((modifiers & NativeMenuModifier::Shift) != NativeMenuModifier::None) {
        append("Shift");
    }
    if ((modifiers & NativeMenuModifier::Super) != NativeMenuModifier::None) {
        append("Super");
    }
    append(key);
    return text;
}

bool has_submenu(const NativeMenuItem& item) {
    return !item.separator && !item.children.empty();
}

template <typename MeasureTextFn>
PopupGeometry popup_geometry(Point origin,
                             const std::vector<NativeMenuItem>& items,
                             const std::vector<std::string>& prefixes,
                             MeasureTextFn&& measure_text) {
    PopupGeometry geometry;
    float width = 160.0F;
    float text_height = 18.0F;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.separator) {
            continue;
        }
        const auto& prefix = prefixes[index];
        const auto label = prefix.empty() ? item.label : prefix + item.label;
        const auto measured = measure_text(label);
        text_height = std::max(text_height, measured.height);
        float row_width = measured.width + 28.0F;
        if (const auto shortcut = shortcut_label(item.shortcut); !shortcut.empty()) {
            row_width += measure_text(shortcut).width + 24.0F;
        }
        if (has_submenu(item)) {
            row_width += measure_text(">").width + 16.0F;
        }
        width = std::max(width, row_width);
    }
    geometry.item_height = text_height + 10.0F;
    float height = 2.0F;
    for (const auto& item : items) {
        height += item.separator ? geometry.separator_height : geometry.item_height;
    }
    geometry.bounds = {origin.x, origin.y, width, height};
    return geometry;
}

int row_index_at(const PopupGeometry& geometry,
                 const std::vector<NativeMenuItem>& items,
                 Point point) {
    if (!geometry.bounds.contains(point)) {
        return -1;
    }
    float y = geometry.bounds.y + 1.0F;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const float row_height =
            items[index].separator ? geometry.separator_height : geometry.item_height;
        if (point.y >= y && point.y < y + row_height) {
            return items[index].separator ? -1 : static_cast<int>(index);
        }
        y += row_height;
    }
    return -1;
}

Rect row_rect(const PopupGeometry& geometry, const std::vector<NativeMenuItem>& items, int index) {
    float y = geometry.bounds.y + 1.0F;
    for (int row = 0; row < index && row < static_cast<int>(items.size()); ++row) {
        y += items[static_cast<std::size_t>(row)].separator ? geometry.separator_height
                                                            : geometry.item_height;
    }
    const float height = index >= 0 && index < static_cast<int>(items.size()) &&
                                 items[static_cast<std::size_t>(index)].separator
                             ? geometry.separator_height
                             : geometry.item_height;
    return {geometry.bounds.x + 1.0F, y, std::max(0.0F, geometry.bounds.width - 2.0F), height};
}

int next_selectable(const std::vector<NativeMenuItem>& items,
                    const std::function<bool(const NativeMenuItem&)>& enabled,
                    int from,
                    int delta) {
    if (items.empty() || delta == 0) {
        return from;
    }
    int index = from;
    for (int step = 0; step < static_cast<int>(items.size()); ++step) {
        index += delta;
        if (index < 0) {
            index = static_cast<int>(items.size()) - 1;
        } else if (index >= static_cast<int>(items.size())) {
            index = 0;
        }
        const auto& item = items[static_cast<std::size_t>(index)];
        if (!item.separator && enabled(item)) {
            return index;
        }
    }
    return from;
}

} // namespace

struct ContextMenu::Impl {
    struct Row {
        std::shared_ptr<detail::AccessibleItem> accessible;
        std::vector<std::shared_ptr<detail::AccessibleItem>> children;
    };

    std::vector<NativeMenuItem> items;
    std::vector<Row> rows;
    std::shared_ptr<ActionGroup> actions;
    ScopedConnection actions_changed;
    Signal<int> item_activated;
    Signal<std::string_view> action_activated;
    Signal<> dismissed;
    bool open = false;
    Point position;
    int root_highlight = -1;
    int open_submenu = -1;
    int child_highlight = -1;
    int armed_root = -1;
    int armed_child = -1;
    std::weak_ptr<Widget> focus_restore;
    bool dismissing = false;
};

std::shared_ptr<ContextMenu> ContextMenu::create() {
    return std::shared_ptr<ContextMenu>(new ContextMenu());
}

ContextMenu::ContextMenu() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    set_visible(false);
    add_style_class("context-menu");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Menu);
    accessible.set_name("context menu");
}

ContextMenu::~ContextMenu() = default;

void ContextMenu::add_item(std::string label) {
    NativeMenuItem item;
    item.label = std::move(label);
    append_model_item(std::move(item));
}

void ContextMenu::add_separator() {
    append_model_item(NativeMenuItem::make_separator());
}

void ContextMenu::set_items(std::vector<NativeMenuItem> items) {
    clear();
    for (auto& item : items) {
        // One submenu level is interactive. Deeper children stay in the model
        // but are not opened.
        for (auto& child : item.children) {
            child.children.clear();
        }
        append_model_item(std::move(item));
    }
}

void ContextMenu::clear() {
    for (const auto& row : impl_->rows) {
        if (row.accessible) {
            remove_child(*row.accessible);
        }
    }
    impl_->items.clear();
    impl_->rows.clear();
    impl_->root_highlight = -1;
    impl_->open_submenu = -1;
    impl_->child_highlight = -1;
    impl_->armed_root = -1;
    impl_->armed_child = -1;
}

std::size_t ContextMenu::item_count() const {
    return impl_->items.size();
}

void ContextMenu::set_action_group(std::shared_ptr<ActionGroup> actions) {
    impl_->actions = std::move(actions);
    bind_actions();
}

ActionGroup* ContextMenu::action_group() const {
    return impl_->actions.get();
}

void ContextMenu::bind_actions() {
    impl_->actions_changed = ScopedConnection{};
    if (impl_->actions) {
        impl_->actions_changed = ScopedConnection{
            impl_->actions->on_changed().connect([weak = weak_from_this()](std::string_view) {
                if (auto widget = weak.lock()) {
                    auto& menu = static_cast<ContextMenu&>(*widget);
                    menu.sync_accessible_items();
                    menu.queue_redraw();
                }
            })};
    }
    sync_accessible_items();
    queue_redraw();
}

void ContextMenu::append_model_item(NativeMenuItem item) {
    const auto root_index = static_cast<int>(impl_->items.size());
    impl_->items.push_back(std::move(item));
    Impl::Row row;
    if (!impl_->items.back().separator) {
        row.accessible = detail::AccessibleItem::create(
            impl_->items.back().label,
            AccessibleRole::MenuItem,
            [weak = weak_from_this(), root_index] {
                auto self = std::static_pointer_cast<ContextMenu>(weak.lock());
                if (!self || !self->is_open() || !self->is_sensitive()) {
                    return false;
                }
                const auto& model = self->impl_->items[static_cast<std::size_t>(root_index)];
                if (!self->item_enabled(model)) {
                    return false;
                }
                if (has_submenu(model)) {
                    self->impl_->root_highlight = root_index;
                    self->open_highlighted_submenu();
                    self->sync_accessible_items();
                    self->queue_redraw();
                    return true;
                }
                self->activate_leaf(root_index, -1);
                return true;
            });
        append_child(row.accessible);
        const auto& children = impl_->items.back().children;
        row.children.resize(children.size());
        for (std::size_t child_index = 0; child_index < children.size(); ++child_index) {
            if (children[child_index].separator) {
                continue;
            }
            auto child = detail::AccessibleItem::create(
                children[child_index].label,
                AccessibleRole::MenuItem,
                [weak = weak_from_this(), root_index, child_index] {
                    auto self = std::static_pointer_cast<ContextMenu>(weak.lock());
                    if (!self || !self->is_open() || !self->is_sensitive() ||
                        self->impl_->open_submenu != root_index) {
                        return false;
                    }
                    const auto& model = self->impl_->items[static_cast<std::size_t>(root_index)]
                                            .children[child_index];
                    if (!self->item_enabled(model)) {
                        return false;
                    }
                    self->activate_leaf(root_index, static_cast<int>(child_index));
                    return true;
                });
            child->ensure_accessible().set_hidden(true);
            row.accessible->append(child);
            row.children[child_index] = std::move(child);
        }
    }
    impl_->rows.push_back(std::move(row));
    sync_accessible_items();
}

bool ContextMenu::item_enabled(const NativeMenuItem& item) const {
    if (!item.enabled || item.separator) {
        return false;
    }
    if (!impl_->actions || item.action_name.empty() || has_submenu(item)) {
        return true;
    }
    const auto action = impl_->actions->find(item.action_name);
    return action && action->is_enabled();
}

void ContextMenu::show_at(Point position) {
    if (auto* focused = host_focused_widget();
        focused != nullptr && focused != this && !impl_->open) {
        impl_->focus_restore = focused->weak_from_this();
    }
    preserve_damage_regions_for_next_redraw();
    impl_->position = position;
    impl_->open = true;
    impl_->root_highlight = -1;
    impl_->open_submenu = -1;
    impl_->child_highlight = -1;
    impl_->armed_root = -1;
    impl_->armed_child = -1;
    set_visible(true);
    grab_focus();
    sync_accessible_items();
    queue_redraw();
}

void ContextMenu::dismiss() {
    const bool was_open = impl_->open;
    dismiss_without_activation();
    if (was_open) {
        impl_->dismissed.emit();
    }
}

void ContextMenu::on_focus_changed(bool focused) {
    if (focused || !impl_->open || impl_->dismissing) {
        return;
    }
    auto* loop = EventLoop::current();
    if (loop == nullptr) {
        return;
    }
    // Focus changes before the window publishes the new target. Check on the
    // next loop turn so a cleared focus (the menu was disabled) does not close
    // it, while a move to another widget does.
    loop->post(
        [weak = weak_from_this()] {
            auto self = std::static_pointer_cast<ContextMenu>(weak.lock());
            if (!self || !self->impl_->open || self->impl_->dismissing) {
                return;
            }
            const auto* focused_widget = self->host_focused_widget();
            if (focused_widget == nullptr || focused_widget == self.get()) {
                return;
            }
            self->impl_->focus_restore.reset();
            self->dismiss();
        },
        "context-menu-focus");
}

void ContextMenu::dismiss_without_activation() {
    if (!impl_->open || impl_->dismissing) {
        return;
    }
    impl_->dismissing = true;
    preserve_damage_regions_for_next_redraw();
    impl_->open = false;
    impl_->root_highlight = -1;
    impl_->open_submenu = -1;
    impl_->child_highlight = -1;
    impl_->armed_root = -1;
    impl_->armed_child = -1;
    auto restore = std::move(impl_->focus_restore);
    impl_->focus_restore.reset();
    set_visible(false);
    if (auto previous = restore.lock()) {
        previous->grab_focus();
    } else {
        set_state_flag(StateFlags::Focused, false);
    }
    impl_->dismissing = false;
    sync_accessible_items();
    queue_redraw();
}

void ContextMenu::activate_leaf(int root_index, int child_index) {
    if (root_index < 0 || root_index >= static_cast<int>(impl_->items.size())) {
        return;
    }
    const NativeMenuItem* item = &impl_->items[static_cast<std::size_t>(root_index)];
    if (child_index >= 0) {
        if (child_index >= static_cast<int>(item->children.size())) {
            return;
        }
        item = &item->children[static_cast<std::size_t>(child_index)];
    }
    if (item->separator || has_submenu(*item) || !item_enabled(*item)) {
        return;
    }
    const int emitted_index = child_index < 0 ? root_index : -1;
    const std::string action_name = item->action_name;
    const bool use_group = impl_->actions != nullptr;
    dismiss_without_activation();
    if (emitted_index >= 0) {
        impl_->item_activated.emit(emitted_index);
    }
    if (use_group) {
        if (impl_->actions) {
            impl_->actions->activate(action_name);
        }
    } else if (!action_name.empty()) {
        impl_->action_activated.emit(action_name);
    }
}

void ContextMenu::open_highlighted_submenu() {
    const int index = impl_->root_highlight;
    if (index < 0 || index >= static_cast<int>(impl_->items.size())) {
        close_submenu();
        return;
    }
    const auto& item = impl_->items[static_cast<std::size_t>(index)];
    if (!has_submenu(item) || !item_enabled(item)) {
        close_submenu();
        return;
    }
    impl_->open_submenu = index;
    impl_->child_highlight = next_selectable(
        item.children, [this](const NativeMenuItem& child) { return item_enabled(child); }, -1, 1);
}

void ContextMenu::close_submenu() {
    impl_->open_submenu = -1;
    impl_->child_highlight = -1;
}

void ContextMenu::move_highlight(int delta) {
    if (impl_->open_submenu >= 0 && impl_->child_highlight >= 0) {
        const auto& children = impl_->items[static_cast<std::size_t>(impl_->open_submenu)].children;
        impl_->child_highlight = next_selectable(
            children,
            [this](const NativeMenuItem& item) { return item_enabled(item); },
            impl_->child_highlight,
            delta);
        return;
    }
    impl_->root_highlight = next_selectable(
        impl_->items,
        [this](const NativeMenuItem& item) { return item_enabled(item); },
        impl_->root_highlight,
        delta);
    if (impl_->root_highlight >= 0 &&
        has_submenu(impl_->items[static_cast<std::size_t>(impl_->root_highlight)])) {
        impl_->open_submenu = impl_->root_highlight;
        impl_->child_highlight = -1;
    } else {
        close_submenu();
    }
}

Signal<>& ContextMenu::on_dismissed() {
    return impl_->dismissed;
}

bool ContextMenu::is_open() const {
    return impl_->open;
}

Signal<int>& ContextMenu::on_item_activated() {
    return impl_->item_activated;
}

Signal<std::string_view>& ContextMenu::on_action() {
    return impl_->action_activated;
}

SizeRequest ContextMenu::measure(const Constraints& /*constraints*/) const {
    return {0.0F, 0.0F, 0.0F, 0.0F};
}

void ContextMenu::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    sync_accessible_items();
}

void ContextMenu::sync_accessible_items() {
    const auto font = context_menu_font();
    std::vector<std::string> prefixes(impl_->items.size());
    for (std::size_t index = 0; index < impl_->items.size(); ++index) {
        if (impl_->actions) {
            const auto action = impl_->actions->find(impl_->items[index].action_name);
            if (action && action->is_checked()) {
                prefixes[index] = "\u2713 ";
            }
        }
    }
    const auto geometry =
        popup_geometry(impl_->position, impl_->items, prefixes, [&](std::string_view text) {
            return measure_text(text, font);
        });
    for (std::size_t index = 0; index < impl_->items.size(); ++index) {
        const auto& item = impl_->items[index];
        auto& row = impl_->rows[index];
        if (!row.accessible) {
            continue;
        }
        const auto bounds = row_rect(geometry, impl_->items, static_cast<int>(index));
        row.accessible->allocate(bounds);
        row.accessible->set_sensitive(item_enabled(item));
        row.accessible->set_state_flag(StateFlags::Selected,
                                       static_cast<int>(index) == impl_->root_highlight);
        const auto action = impl_->actions ? impl_->actions->find(item.action_name) : nullptr;
        row.accessible->set_state_flag(StateFlags::Checked, action && action->is_checked());
        row.accessible->ensure_accessible().set_hidden(!impl_->open);

        const bool submenu_open = impl_->open_submenu == static_cast<int>(index);
        PopupGeometry child_geometry;
        std::vector<std::string> child_prefixes(item.children.size());
        if (submenu_open) {
            for (std::size_t child_index = 0; child_index < item.children.size(); ++child_index) {
                if (impl_->actions) {
                    const auto child_action =
                        impl_->actions->find(item.children[child_index].action_name);
                    if (child_action && child_action->is_checked()) {
                        child_prefixes[child_index] = "\u2713 ";
                    }
                }
            }
            child_geometry =
                popup_geometry({bounds.right() - 4.0F, bounds.y},
                               item.children,
                               child_prefixes,
                               [&](std::string_view text) { return measure_text(text, font); });
        }
        for (std::size_t child_index = 0; child_index < row.children.size(); ++child_index) {
            const auto& child = row.children[child_index];
            if (!child) {
                continue;
            }
            child->ensure_accessible().set_hidden(!submenu_open);
            child->set_sensitive(item_enabled(item.children[child_index]));
            child->set_state_flag(StateFlags::Selected,
                                  submenu_open &&
                                      impl_->child_highlight == static_cast<int>(child_index));
            const auto child_action =
                impl_->actions ? impl_->actions->find(item.children[child_index].action_name)
                               : nullptr;
            child->set_state_flag(StateFlags::Checked, child_action && child_action->is_checked());
            if (submenu_open) {
                child->allocate(
                    row_rect(child_geometry, item.children, static_cast<int>(child_index)));
            }
        }
    }
}

bool ContextMenu::hit_test(Point point) const {
    if (Widget::hit_test(point)) {
        return true;
    }
    if (!impl_->open || impl_->items.empty()) {
        return false;
    }
    const auto font = context_menu_font();
    const auto geometry =
        popup_geometry(impl_->position,
                       impl_->items,
                       std::vector<std::string>(impl_->items.size()),
                       [&](std::string_view text) { return measure_text(text, font); });
    if (geometry.bounds.contains(point)) {
        return true;
    }
    if (impl_->open_submenu < 0) {
        return false;
    }
    const auto submenu_bounds = row_rect(geometry, impl_->items, impl_->open_submenu);
    const auto& children = impl_->items[static_cast<std::size_t>(impl_->open_submenu)].children;
    const auto submenu =
        popup_geometry({submenu_bounds.right() - 4.0F, submenu_bounds.y},
                       children,
                       std::vector<std::string>(children.size()),
                       [&](std::string_view text) { return measure_text(text, font); });
    return submenu.bounds.contains(point);
}

std::vector<Rect> ContextMenu::damage_regions() const {
    auto regions = Widget::damage_regions();
    if (!impl_->open || impl_->items.empty()) {
        return regions;
    }
    const auto font = context_menu_font();
    const auto geometry =
        popup_geometry(impl_->position,
                       impl_->items,
                       std::vector<std::string>(impl_->items.size()),
                       [&](std::string_view text) { return measure_text(text, font); });
    regions.push_back(geometry.bounds);
    if (impl_->open_submenu >= 0) {
        const auto anchor = row_rect(geometry, impl_->items, impl_->open_submenu);
        const auto& children = impl_->items[static_cast<std::size_t>(impl_->open_submenu)].children;
        regions.push_back(
            popup_geometry({anchor.right() - 4.0F, anchor.y},
                           children,
                           std::vector<std::string>(children.size()),
                           [&](std::string_view text) { return measure_text(text, font); })
                .bounds);
    }
    return regions;
}

bool ContextMenu::handle_mouse_event(const MouseEvent& event) {
    const auto keep_alive = weak_from_this().lock();
    if (!impl_->open || impl_->items.empty()) {
        return false;
    }

    const auto font = context_menu_font();
    const auto geometry =
        popup_geometry(impl_->position,
                       impl_->items,
                       std::vector<std::string>(impl_->items.size()),
                       [&](std::string_view text) { return measure_text(text, font); });
    const Point point{event.x, event.y};
    MenuHit hit;
    if (impl_->open_submenu >= 0) {
        const auto anchor = row_rect(geometry, impl_->items, impl_->open_submenu);
        const auto& children = impl_->items[static_cast<std::size_t>(impl_->open_submenu)].children;
        const auto submenu =
            popup_geometry({anchor.right() - 4.0F, anchor.y},
                           children,
                           std::vector<std::string>(children.size()),
                           [&](std::string_view text) { return measure_text(text, font); });
        if (submenu.bounds.contains(point)) {
            hit = {impl_->open_submenu, row_index_at(submenu, children, point), true};
        }
    }
    if (!hit.inside && geometry.bounds.contains(point)) {
        hit = {row_index_at(geometry, impl_->items, point), -1, true};
    }

    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Leave) {
        return hit.inside;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (!hit.inside) {
            dismiss();
            return false;
        }
        impl_->armed_root = hit.root;
        impl_->armed_child = hit.child;
        return true;
    case MouseEvent::Type::Release:
        if (!hit.inside) {
            dismiss();
            return false;
        }
        if (impl_->armed_root == hit.root && impl_->armed_child == hit.child && hit.root >= 0) {
            if (hit.child >= 0) {
                activate_leaf(hit.root, hit.child);
            } else if (has_submenu(impl_->items[static_cast<std::size_t>(hit.root)])) {
                impl_->root_highlight = hit.root;
                open_highlighted_submenu();
            } else {
                activate_leaf(hit.root, -1);
            }
        }
        impl_->armed_root = -1;
        impl_->armed_child = -1;
        sync_accessible_items();
        queue_redraw();
        return true;
    case MouseEvent::Type::Move:
        if (hit.inside && hit.child >= 0) {
            impl_->child_highlight = hit.child;
        } else if (hit.inside) {
            impl_->root_highlight = hit.root;
            impl_->child_highlight = -1;
            if (hit.root >= 0 && has_submenu(impl_->items[static_cast<std::size_t>(hit.root)])) {
                impl_->open_submenu = hit.root;
            } else {
                close_submenu();
            }
        }
        sync_accessible_items();
        queue_redraw();
        return hit.inside;
    case MouseEvent::Type::Leave:
        impl_->root_highlight = -1;
        close_submenu();
        sync_accessible_items();
        queue_redraw();
        return false;
    case MouseEvent::Type::Enter:
        return hit.inside;
    case MouseEvent::Type::Scroll:
    case MouseEvent::Type::DragStart:
    case MouseEvent::Type::DragUpdate:
    case MouseEvent::Type::DragEnd:
        return hit.inside;
    }
    return false;
}

bool ContextMenu::handle_key_event(const KeyEvent& event) {
    const auto keep_alive = weak_from_this().lock();
    if (!impl_->open || event.type != KeyEvent::Type::Press || impl_->items.empty()) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Escape:
        dismiss();
        return true;
    case KeyCode::Up:
        move_highlight(-1);
        sync_accessible_items();
        queue_redraw();
        return true;
    case KeyCode::Down:
        move_highlight(1);
        sync_accessible_items();
        queue_redraw();
        return true;
    case KeyCode::Right:
        open_highlighted_submenu();
        sync_accessible_items();
        queue_redraw();
        return true;
    case KeyCode::Left:
        close_submenu();
        sync_accessible_items();
        queue_redraw();
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->child_highlight >= 0 && impl_->open_submenu >= 0) {
            activate_leaf(impl_->open_submenu, impl_->child_highlight);
        } else if (impl_->root_highlight >= 0 &&
                   has_submenu(impl_->items[static_cast<std::size_t>(impl_->root_highlight)])) {
            open_highlighted_submenu();
        } else if (impl_->root_highlight >= 0) {
            activate_leaf(impl_->root_highlight, -1);
        }
        sync_accessible_items();
        queue_redraw();
        return true;
    default:
        return false;
    }
}

void ContextMenu::snapshot(SnapshotContext& ctx) const {
    if (!impl_->open || impl_->items.empty()) {
        return;
    }

    const auto font = context_menu_font();
    const float popup_radius = theme_number("popup-radius", 12.0F);
    const auto popup_bg = theme_color("popup-background");
    const auto popup_border = theme_color("popup-border-color");
    const auto hover_bg = theme_color("hover-background");
    const auto text_color = theme_color("text-color");
    const auto disabled_text = theme_color("disabled-text-color", text_color);
    const auto separator_color = theme_color("separator-color");

    auto paint_panel = [&](const PopupGeometry& geometry,
                           const std::vector<NativeMenuItem>& items,
                           int highlight,
                           bool submenu_panel) {
        ctx.push_overlay_container(geometry.bounds);
        ctx.add_rounded_rect({geometry.bounds.x,
                              geometry.bounds.y + 1.0F,
                              geometry.bounds.width,
                              geometry.bounds.height},
                             Color{0.08F, 0.12F, 0.18F, 0.05F},
                             popup_radius + 1.0F);
        ctx.add_rounded_rect(geometry.bounds, popup_bg, popup_radius);
        ctx.add_border(geometry.bounds, popup_border, 1.0F, popup_radius);

        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            const auto row = row_rect(geometry, items, static_cast<int>(index));
            if (item.separator) {
                ctx.add_color_rect({row.x + 8.0F,
                                    row.y + (row.height * 0.5F),
                                    std::max(0.0F, row.width - 16.0F),
                                    1.0F},
                                   separator_color);
                continue;
            }

            if (static_cast<int>(index) == highlight) {
                ctx.add_rounded_rect(
                    {row.x + 4.0F, row.y + 2.0F, row.width - 8.0F, row.height - 4.0F},
                    hover_bg,
                    8.0F);
            }

            const bool enabled = item_enabled(item);
            const auto color = enabled ? text_color : disabled_text;
            const auto action = impl_->actions ? impl_->actions->find(item.action_name) : nullptr;
            const std::string label =
                action && action->is_checked() ? "\u2713 " + item.label : item.label;
            const auto measured = measure_text(label, font);
            const float text_y = row.y + std::max(0.0F, (row.height - measured.height) * 0.5F);
            ctx.add_text({row.x + 12.0F, text_y}, label, color, font);

            float trailing = 12.0F;
            if (!submenu_panel && has_submenu(item)) {
                const auto arrow = measure_text(">", font);
                ctx.add_text({row.right() - arrow.width - trailing, text_y}, ">", color, font);
                trailing += arrow.width + 8.0F;
            }
            if (const auto shortcut = shortcut_label(item.shortcut); !shortcut.empty()) {
                const auto shortcut_size = measure_text(shortcut, font);
                ctx.add_text(
                    {row.right() - shortcut_size.width - trailing, text_y}, shortcut, color, font);
            }
        }
        ctx.pop_container();
    };

    std::vector<std::string> prefixes(impl_->items.size());
    for (std::size_t index = 0; index < impl_->items.size(); ++index) {
        const auto action =
            impl_->actions ? impl_->actions->find(impl_->items[index].action_name) : nullptr;
        if (action && action->is_checked()) {
            prefixes[index] = "\u2713 ";
        }
    }
    const auto laid_out =
        popup_geometry(impl_->position, impl_->items, prefixes, [&](std::string_view text) {
            return measure_text(text, font);
        });
    paint_panel(laid_out, impl_->items, impl_->root_highlight, false);

    if (impl_->open_submenu >= 0) {
        const auto anchor = row_rect(laid_out, impl_->items, impl_->open_submenu);
        const auto& children = impl_->items[static_cast<std::size_t>(impl_->open_submenu)].children;
        std::vector<std::string> child_prefixes(children.size());
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto action =
                impl_->actions ? impl_->actions->find(children[index].action_name) : nullptr;
            if (action && action->is_checked()) {
                child_prefixes[index] = "\u2713 ";
            }
        }
        const auto submenu =
            popup_geometry({anchor.right() - 4.0F, anchor.y},
                           children,
                           child_prefixes,
                           [&](std::string_view text) { return measure_text(text, font); });
        paint_panel(submenu, children, impl_->child_highlight, true);
    }
}

} // namespace nk
