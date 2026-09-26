#include "accessibility_tree.h"

#include "../ui_core/widget_type_name.h"

#include <algorithm>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace nk::detail {

namespace {

bool has_role(const Widget& widget) {
    const auto* accessible = widget.accessible();
    return accessible != nullptr && accessible->role() != AccessibleRole::None;
}

bool is_shown(const Widget& widget) {
    const auto* accessible = widget.accessible();
    return widget.is_visible() && (accessible == nullptr || !accessible->is_hidden());
}

Widget* nearest_with_role(Widget* widget) {
    for (auto* current = widget; current != nullptr; current = current->parent()) {
        if (has_role(*current)) {
            return current;
        }
    }
    return nullptr;
}

bool is_enabled(const Widget& widget) {
    for (const auto* current = &widget; current != nullptr; current = current->parent()) {
        if (!current->is_sensitive()) {
            return false;
        }
    }
    const auto* accessible = widget.accessible();
    return accessible == nullptr ||
           (accessible->state() & StateFlags::Disabled) == StateFlags::None;
}

} // namespace

std::string_view accessible_role_name(AccessibleRole role) {
    switch (role) {
    case AccessibleRole::None:
        return "none";
    case AccessibleRole::Button:
        return "button";
    case AccessibleRole::CheckBox:
        return "checkbox";
    case AccessibleRole::Dialog:
        return "dialog";
    case AccessibleRole::Grid:
        return "grid";
    case AccessibleRole::GridCell:
        return "gridcell";
    case AccessibleRole::Group:
        return "group";
    case AccessibleRole::Image:
        return "image";
    case AccessibleRole::Label:
        return "label";
    case AccessibleRole::Link:
        return "link";
    case AccessibleRole::List:
        return "list";
    case AccessibleRole::ListItem:
        return "listitem";
    case AccessibleRole::Menu:
        return "menu";
    case AccessibleRole::MenuBar:
        return "menubar";
    case AccessibleRole::MenuItem:
        return "menuitem";
    case AccessibleRole::ProgressBar:
        return "progressbar";
    case AccessibleRole::RadioButton:
        return "radiobutton";
    case AccessibleRole::ScrollBar:
        return "scrollbar";
    case AccessibleRole::Separator:
        return "separator";
    case AccessibleRole::Slider:
        return "slider";
    case AccessibleRole::SpinButton:
        return "spinbutton";
    case AccessibleRole::Status:
        return "status";
    case AccessibleRole::Tab:
        return "tab";
    case AccessibleRole::TabList:
        return "tablist";
    case AccessibleRole::TabPanel:
        return "tabpanel";
    case AccessibleRole::TextInput:
        return "textinput";
    case AccessibleRole::ToggleButton:
        return "togglebutton";
    case AccessibleRole::Toolbar:
        return "toolbar";
    case AccessibleRole::Tree:
        return "tree";
    case AccessibleRole::TreeItem:
        return "treeitem";
    case AccessibleRole::Window:
        return "window";
    }
    return "none";
}

AccessibilityTree::AccessibilityTree(Window& window) : window_(window) {}

std::vector<AccessibleId> AccessibilityTree::root_children() {
    std::vector<AccessibleId> out;
    for (auto* root : window_.accessibility_roots()) {
        collect(*root, out);
    }
    return out;
}

std::vector<AccessibleId> AccessibilityTree::children(AccessibleId id) {
    std::vector<AccessibleId> out;
    if (const auto* widget = exposed_widget(id); widget != nullptr) {
        for (const auto& child : widget->children()) {
            if (child != nullptr) {
                collect(*child, out);
            }
        }
    }
    return out;
}

bool AccessibilityTree::is_exposed(AccessibleId id) const {
    return exposed_widget(id) != nullptr;
}

std::optional<AccessibleId> AccessibilityTree::parent(AccessibleId id) {
    const auto* widget = exposed_widget(id);
    if (widget == nullptr) {
        return std::nullopt;
    }
    if (auto* ancestor = nearest_with_role(widget->parent()); ancestor != nullptr) {
        return id_for(*ancestor);
    }
    return std::nullopt;
}

std::optional<AccessibleNodeInfo> AccessibilityTree::info(AccessibleId id) const {
    const auto* widget = exposed_widget(id);
    if (widget == nullptr) {
        return std::nullopt;
    }
    const auto& accessible = *widget->accessible();
    const auto actions = accessible.actions();
    return AccessibleNodeInfo{
        .role = accessible.role(),
        .type_name = widget_type_name(*widget),
        .debug_name = std::string(widget->debug_name()),
        .name = std::string(accessible.name()),
        .description = std::string(accessible.description()),
        .value = std::string(accessible.value()),
        .bounds = widget->allocation(),
        .state = accessible.state(),
        .enabled = is_enabled(*widget),
        .focusable = widget->is_focusable(),
        .focused = window_.focused_widget() == widget,
        .actions = {actions.begin(), actions.end()},
    };
}

std::optional<AccessibleId> AccessibilityTree::focused() {
    auto* widget = nearest_with_role(window_.focused_widget());
    if (widget == nullptr) {
        return std::nullopt;
    }
    const auto id = id_for(*widget);
    return is_exposed(id) ? std::optional(id) : std::nullopt;
}

std::optional<AccessibleId> AccessibilityTree::hit_test(Point point) {
    // Overlays follow the content, so the last root is topmost.
    for (auto* root : std::views::reverse(window_.accessibility_roots())) {
        if (const auto hit = hit_test(*root, point); hit.has_value()) {
            return hit;
        }
    }
    return std::nullopt;
}

bool AccessibilityTree::perform(AccessibleId id, AccessibleAction action) {
    auto* widget = exposed_widget(id);
    if (widget == nullptr || !is_enabled(*widget) ||
        !widget->accessible()->supports_action(action)) {
        return false;
    }
    // The action may remove the widget from its window; keep it alive until it returns.
    const auto keep_alive = widget->shared_from_this();
    return widget->accessible()->perform_action(action);
}

bool AccessibilityTree::focus(AccessibleId id) {
    auto* widget = exposed_widget(id);
    if (widget == nullptr || !is_enabled(*widget) || !widget->is_focusable()) {
        return false;
    }
    const auto keep_alive = widget->shared_from_this();
    widget->grab_focus();
    return window_.focused_widget() == widget;
}

std::vector<AccessibleId> AccessibilityTree::purge() {
    std::vector<AccessibleId> removed;
    for (auto it = widgets_.begin(); it != widgets_.end();) {
        if (it->second.expired()) {
            removed.push_back(it->first);
            ids_.erase(it->second);
            it = widgets_.erase(it);
        } else {
            ++it;
        }
    }
    std::ranges::sort(removed);
    return removed;
}

Widget* AccessibilityTree::exposed_widget(AccessibleId id) const {
    const auto found = widgets_.find(id);
    if (found == widgets_.end()) {
        return nullptr;
    }
    const auto widget = found->second.lock();
    if (widget == nullptr || !has_role(*widget)) {
        return nullptr;
    }
    Widget* top = nullptr;
    for (auto* current = widget.get(); current != nullptr; current = current->parent()) {
        if (!is_shown(*current)) {
            return nullptr;
        }
        top = current;
    }
    const auto roots = window_.accessibility_roots();
    // The window owns the tree the widget is attached to, so the pointer stays
    // valid after the local shared_ptr is released.
    return std::ranges::find(roots, top) != roots.end() ? widget.get() : nullptr;
}

AccessibleId AccessibilityTree::id_for(Widget& widget) {
    auto weak = widget.weak_from_this();
    if (weak.expired()) {
        return 0; // Not owned by a shared_ptr, so its lifetime cannot be tracked.
    }
    if (const auto found = ids_.find(weak); found != ids_.end()) {
        return found->second;
    }
    const auto id = next_id_++;
    ids_.emplace(weak, id);
    widgets_.emplace(id, std::move(weak));
    return id;
}

void AccessibilityTree::collect(Widget& widget, std::vector<AccessibleId>& out) {
    if (!is_shown(widget)) {
        return;
    }
    if (has_role(widget)) {
        if (const auto id = id_for(widget); id != 0) {
            out.push_back(id);
        }
        return;
    }
    for (const auto& child : widget.children()) {
        if (child != nullptr) {
            collect(*child, out);
        }
    }
}

std::optional<AccessibleId> AccessibilityTree::hit_test(Widget& widget, Point point) {
    if (!is_shown(widget)) {
        return std::nullopt;
    }
    // Later children paint over earlier ones.
    for (const auto& child : std::views::reverse(widget.children())) {
        if (child != nullptr) {
            if (const auto hit = hit_test(*child, point); hit.has_value()) {
                return hit;
            }
        }
    }
    if (has_role(widget) && widget.allocation().contains(point)) {
        if (const auto id = id_for(widget); id != 0) {
            return id;
        }
    }
    return std::nullopt;
}

} // namespace nk::detail
