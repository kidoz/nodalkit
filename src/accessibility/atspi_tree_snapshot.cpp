#include "atspi_tree_snapshot.h"

#include "../text/text_boundaries.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace nk::detail {

namespace {

constexpr const char* AccessibleInterface = "org.a11y.atspi.Accessible";
constexpr const char* ComponentInterface = "org.a11y.atspi.Component";
constexpr const char* ActionInterface = "org.a11y.atspi.Action";
constexpr const char* TextInterface = "org.a11y.atspi.Text";

AtspiStateBit state_bits(const AccessibleNodeInfo& info) {
    // Every node in the snapshot is exposed, so it is visible and showing.
    auto state = AtspiStateBit::Visible | AtspiStateBit::Showing;
    if (info.enabled) {
        state |= AtspiStateBit::Enabled | AtspiStateBit::Sensitive;
    }
    if (info.focusable) {
        state |= AtspiStateBit::Focusable;
    }
    if (info.focused) {
        state |= AtspiStateBit::Focused;
    }
    if ((info.state & StateFlags::Hovered) != StateFlags::None) {
        state |= AtspiStateBit::Hovered;
    }
    if ((info.state & StateFlags::Pressed) != StateFlags::None) {
        state |= AtspiStateBit::Pressed;
    }
    return state;
}

struct RoleMapping {
    AtspiRoleValue role;
    std::string_view name;
};

bool type_is(const AccessibleNodeInfo& info, std::string_view type) {
    return info.type_name == type || info.type_name.ends_with("::" + std::string(type));
}

RoleMapping atspi_role(const AccessibleNodeInfo& info) {
    using Role = AccessibleRole;
    if (type_is(info, "ComboBox")) {
        return {AtspiRoleValue::ComboBox, "combo box"};
    }
    if (type_is(info, "TextArea")) {
        return {AtspiRoleValue::Text, "text"};
    }
    switch (info.role) {
    case Role::Button:
        return {AtspiRoleValue::PushButton, "push button"};
    case Role::CheckBox:
        return {AtspiRoleValue::CheckBox, "check box"};
    case Role::Dialog:
        return {AtspiRoleValue::Dialog, "dialog"};
    case Role::Grid:
        return {AtspiRoleValue::Table, "table"};
    case Role::GridCell:
        return {AtspiRoleValue::TableCell, "table cell"};
    case Role::Group:
    case Role::TabPanel:
        return {AtspiRoleValue::Panel, "panel"};
    case Role::Image:
        return {AtspiRoleValue::Image, "image"};
    case Role::Label:
        return {AtspiRoleValue::Label, "label"};
    case Role::Link:
        return {AtspiRoleValue::Link, "link"};
    case Role::List:
        return {AtspiRoleValue::List, "list"};
    case Role::ListItem:
        return {AtspiRoleValue::ListItem, "list item"};
    case Role::Menu:
        return {AtspiRoleValue::Menu, "menu"};
    case Role::MenuBar:
        return {AtspiRoleValue::MenuBar, "menu bar"};
    case Role::MenuItem:
        return {AtspiRoleValue::MenuItem, "menu item"};
    case Role::ProgressBar:
        return {AtspiRoleValue::ProgressBar, "progress bar"};
    case Role::RadioButton:
        return {AtspiRoleValue::RadioButton, "radio button"};
    case Role::ScrollBar:
        return {AtspiRoleValue::ScrollBar, "scroll bar"};
    case Role::Separator:
        return {AtspiRoleValue::Separator, "separator"};
    case Role::Slider:
        return {AtspiRoleValue::Slider, "slider"};
    case Role::SpinButton:
        return {AtspiRoleValue::SpinButton, "spin button"};
    case Role::Status:
        return {AtspiRoleValue::StatusBar, "status bar"};
    case Role::Tab:
        return {AtspiRoleValue::PageTab, "page tab"};
    case Role::TabList:
        return {AtspiRoleValue::PageTabList, "page tab list"};
    case Role::TextInput:
        return {AtspiRoleValue::Entry, "entry"};
    case Role::ToggleButton:
        return {AtspiRoleValue::ToggleButton, "toggle button"};
    case Role::Toolbar:
        return {AtspiRoleValue::ToolBar, "tool bar"};
    case Role::Tree:
        return {AtspiRoleValue::Tree, "tree"};
    case Role::TreeItem:
        return {AtspiRoleValue::TreeItem, "tree item"};
    case Role::Window:
        return {AtspiRoleValue::Frame, "frame"};
    case Role::None:
        break;
    }
    return {AtspiRoleValue::Unknown, "unknown"};
}

AtspiStateSet atspi_states(const AccessibleNodeInfo& info, AtspiRoleValue role) {
    const auto has = [&](StateFlags flag) { return (info.state & flag) == flag; };
    AtspiStateSet set{};
    add_atspi_state(set, AtspiStateValue::Visible);
    add_atspi_state(set, AtspiStateValue::Showing);
    if (info.enabled) {
        add_atspi_state(set, AtspiStateValue::Enabled);
        add_atspi_state(set, AtspiStateValue::Sensitive);
    }
    if (info.focusable) {
        add_atspi_state(set, AtspiStateValue::Focusable);
    }
    if (info.focused) {
        add_atspi_state(set, AtspiStateValue::Focused);
    }
    if (has(StateFlags::Pressed)) {
        add_atspi_state(set, AtspiStateValue::Pressed);
    }
    switch (role) {
    case AtspiRoleValue::CheckBox:
    case AtspiRoleValue::RadioButton:
    case AtspiRoleValue::ToggleButton:
        if (type_is(info, "Expander")) {
            add_atspi_state(set, AtspiStateValue::Expandable);
            if (has(StateFlags::Checked)) {
                add_atspi_state(set, AtspiStateValue::Expanded);
            }
            break;
        }
        add_atspi_state(set, AtspiStateValue::Checkable);
        if (has(StateFlags::Checked)) {
            add_atspi_state(set, AtspiStateValue::Checked);
        }
        break;
    case AtspiRoleValue::PageTab:
    case AtspiRoleValue::ListItem:
    case AtspiRoleValue::TreeItem:
    case AtspiRoleValue::TableCell:
        add_atspi_state(set, AtspiStateValue::Selectable);
        if (has(StateFlags::Selected) || has(StateFlags::Checked)) {
            add_atspi_state(set, AtspiStateValue::Selected);
        }
        break;
    case AtspiRoleValue::Entry:
    case AtspiRoleValue::Text:
        if (info.enabled) {
            add_atspi_state(set, AtspiStateValue::Editable);
        }
        add_atspi_state(set,
                        role == AtspiRoleValue::Text ? AtspiStateValue::MultiLine
                                                     : AtspiStateValue::SingleLine);
        break;
    default:
        break;
    }
    return set;
}

struct Builder {
    AccessibilityTree& tree;
    std::string_view subtree_root_path;
    std::string_view window_object_name;
    std::vector<AtspiTreeNode>& out;

    void append(AccessibleId id, std::size_t parent_index) {
        const auto info = tree.info(id);
        if (!info.has_value()) {
            return;
        }
        AtspiTreeNode entry{.node = {}, .id = id, .role = AtspiRoleValue::Unknown, .states = {}};
        auto& node = entry.node;
        node.object_name = std::string(window_object_name) + "_n" + std::to_string(id);
        node.object_path = std::string(subtree_root_path) + "/" + node.object_name;
        node.parent_path = out[parent_index].node.object_path;
        const auto mapping = atspi_role(*info);
        entry.role = mapping.role;
        entry.states = atspi_states(*info, mapping.role);
        entry.accessible_id = info->debug_name;
        node.role_name = std::string(mapping.name);
        node.name = info->name;
        node.description = info->description;
        node.value = info->value;
        node.bounds = info->bounds;
        node.state = state_bits(*info);
        // AT-SPI treats the first action as the default, so activation precedes focus.
        auto actions = info->actions;
        std::ranges::stable_partition(
            actions, [](AccessibleAction action) { return action != AccessibleAction::Focus; });
        for (const auto action : actions) {
            node.action_names.emplace_back(accessible_action_name(action));
        }
        node.interfaces = {AccessibleInterface, ComponentInterface};
        if (!node.action_names.empty()) {
            node.interfaces.emplace_back(ActionInterface);
        }
        if (!node.value.empty() || mapping.role == AtspiRoleValue::Entry ||
            mapping.role == AtspiRoleValue::Text) {
            node.interfaces.emplace_back(TextInterface);
        }
        out[parent_index].node.child_paths.push_back(node.object_path);

        const auto index = out.size();
        out.push_back(std::move(entry));
        for (const auto child : tree.children(id)) {
            append(child, index);
        }
    }
};

// Character offset -> byte offset, clamped to the text.
std::size_t byte_offset(std::string_view text, int characters) {
    if (characters <= 0) {
        return 0;
    }
    const auto units = decode_utf8_units(text);
    const auto index = static_cast<std::size_t>(characters);
    return index < units.size() ? units[index].byte_index : text.size();
}

} // namespace

std::vector<AtspiTreeNode> build_atspi_tree_nodes(AccessibilityTree& tree,
                                                  std::string_view subtree_root_path,
                                                  std::string_view window_object_name,
                                                  std::string_view window_title,
                                                  Rect window_bounds,
                                                  bool window_active) {
    std::vector<AtspiTreeNode> out;
    AtspiTreeNode window{.node = {}, .id = 0, .role = AtspiRoleValue::Frame, .states = {}};
    for (const auto state : {AtspiStateValue::Enabled,
                             AtspiStateValue::Sensitive,
                             AtspiStateValue::Visible,
                             AtspiStateValue::Showing}) {
        add_atspi_state(window.states, state);
    }
    if (window_active) {
        add_atspi_state(window.states, AtspiStateValue::Active);
    }
    window.node.object_name = std::string(window_object_name);
    window.node.object_path = std::string(subtree_root_path) + "/" + window.node.object_name;
    window.node.parent_path = std::string(subtree_root_path);
    window.node.role_name = "frame";
    window.node.name = std::string(window_title);
    window.node.bounds = window_bounds;
    window.node.state = AtspiStateBit::Enabled | AtspiStateBit::Sensitive | AtspiStateBit::Visible |
                        AtspiStateBit::Showing;
    window.node.interfaces = {AccessibleInterface, ComponentInterface};
    out.push_back(std::move(window));

    Builder builder{tree, subtree_root_path, window_object_name, out};
    for (const auto id : tree.root_children()) {
        builder.append(id, 0);
    }
    return out;
}

namespace {

constexpr const char* ObjectEvents = "org.a11y.atspi.Event.Object";
constexpr const char* WindowEvents = "org.a11y.atspi.Event.Window";

struct TrackedState {
    AtspiStateValue state;
    std::string_view name;
};

constexpr TrackedState tracked_states[] = {
    {AtspiStateValue::Focused, "focused"},
    {AtspiStateValue::Checked, "checked"},
    {AtspiStateValue::Selected, "selected"},
    {AtspiStateValue::Expanded, "expanded"},
    {AtspiStateValue::Enabled, "enabled"},
    {AtspiStateValue::Sensitive, "sensitive"},
    {AtspiStateValue::Active, "active"},
};

AtspiEvent object_event(std::string_view path, std::string_view member, std::string_view detail) {
    return {.object_path = std::string(path),
            .interface_name = ObjectEvents,
            .member = std::string(member),
            .detail = std::string(detail)};
}

void append_text_changes(const AtspiTreeNode& node,
                         std::string_view before,
                         std::string_view after,
                         std::vector<AtspiEvent>& events) {
    const auto old_units = decode_utf8_units(before);
    const auto new_units = decode_utf8_units(after);
    const auto unit_text = [](std::string_view text,
                              const std::vector<Utf8Unit>& units,
                              std::size_t first,
                              std::size_t last) {
        const auto start = first < units.size() ? units[first].byte_index : text.size();
        const auto end = last < units.size() ? units[last].byte_index : text.size();
        return std::string(text.substr(start, end - start));
    };
    std::size_t prefix = 0;
    while (prefix < old_units.size() && prefix < new_units.size() &&
           old_units[prefix].code_point == new_units[prefix].code_point) {
        ++prefix;
    }
    std::size_t suffix = 0;
    while (suffix < old_units.size() - prefix && suffix < new_units.size() - prefix &&
           old_units[old_units.size() - 1 - suffix].code_point ==
               new_units[new_units.size() - 1 - suffix].code_point) {
        ++suffix;
    }
    const auto deleted = old_units.size() - prefix - suffix;
    const auto inserted = new_units.size() - prefix - suffix;
    const auto& path = node.node.object_path;
    if (deleted > 0) {
        auto event = object_event(path, "TextChanged", "delete");
        event.detail1 = static_cast<int>(prefix);
        event.detail2 = static_cast<int>(deleted);
        event.data = AtspiEvent::Data::Text;
        event.data_value = unit_text(before, old_units, prefix, prefix + deleted);
        events.push_back(std::move(event));
    }
    if (inserted > 0) {
        auto event = object_event(path, "TextChanged", "insert");
        event.detail1 = static_cast<int>(prefix);
        event.detail2 = static_cast<int>(inserted);
        event.data = AtspiEvent::Data::Text;
        event.data_value = unit_text(after, new_units, prefix, prefix + inserted);
        events.push_back(std::move(event));
    }
}

} // namespace

std::vector<AtspiEvent> diff_atspi_nodes(const std::vector<AtspiTreeNode>& before,
                                         const std::vector<AtspiTreeNode>& after) {
    std::unordered_map<std::string_view, const AtspiTreeNode*> old_nodes;
    for (const auto& node : before) {
        old_nodes.emplace(node.node.object_path, &node);
    }
    std::vector<std::pair<const AtspiTreeNode*, const AtspiTreeNode*>> kept;
    for (const auto& node : after) {
        if (const auto found = old_nodes.find(node.node.object_path); found != old_nodes.end()) {
            kept.emplace_back(found->second, &node);
        }
    }

    std::vector<AtspiEvent> events;
    for (const auto& [old_node, new_node] : kept) {
        const auto& old_children = old_node->node.child_paths;
        const auto& new_children = new_node->node.child_paths;
        for (std::size_t i = 0; i < old_children.size(); ++i) {
            if (std::ranges::find(new_children, old_children[i]) == new_children.end()) {
                auto event = object_event(new_node->node.object_path, "ChildrenChanged", "remove");
                event.detail1 = static_cast<int>(i);
                event.data = AtspiEvent::Data::Object;
                event.data_value = old_children[i];
                events.push_back(std::move(event));
            }
        }
        for (std::size_t i = 0; i < new_children.size(); ++i) {
            if (std::ranges::find(old_children, new_children[i]) == old_children.end()) {
                auto event = object_event(new_node->node.object_path, "ChildrenChanged", "add");
                event.detail1 = static_cast<int>(i);
                event.data = AtspiEvent::Data::Object;
                event.data_value = new_children[i];
                events.push_back(std::move(event));
            }
        }
    }
    for (const bool turning_on : {false, true}) {
        for (const auto& [old_node, new_node] : kept) {
            for (const auto& tracked : tracked_states) {
                const bool was = has_atspi_state(old_node->states, tracked.state);
                const bool is = has_atspi_state(new_node->states, tracked.state);
                if (was == is || is != turning_on) {
                    continue;
                }
                auto event = object_event(new_node->node.object_path, "StateChanged", tracked.name);
                event.detail1 = is ? 1 : 0;
                events.push_back(std::move(event));
                if (tracked.state == AtspiStateValue::Active &&
                    new_node->role == AtspiRoleValue::Frame) {
                    events.push_back({.object_path = new_node->node.object_path,
                                      .interface_name = WindowEvents,
                                      .member = is ? "Activate" : "Deactivate"});
                }
            }
        }
    }
    for (const auto& [old_node, new_node] : kept) {
        if (old_node->node.name != new_node->node.name) {
            auto event =
                object_event(new_node->node.object_path, "PropertyChange", "accessible-name");
            event.data = AtspiEvent::Data::Text;
            event.data_value = new_node->node.name;
            events.push_back(std::move(event));
        }
        if (old_node->node.description != new_node->node.description) {
            auto event = object_event(
                new_node->node.object_path, "PropertyChange", "accessible-description");
            event.data = AtspiEvent::Data::Text;
            event.data_value = new_node->node.description;
            events.push_back(std::move(event));
        }
        const bool text_role =
            new_node->role == AtspiRoleValue::Entry || new_node->role == AtspiRoleValue::Text;
        if (text_role && old_node->node.value != new_node->node.value) {
            append_text_changes(*new_node, old_node->node.value, new_node->node.value, events);
        }
    }
    return events;
}

void add_atspi_state(AtspiStateSet& set, AtspiStateValue state) {
    const auto bit = static_cast<std::uint32_t>(state);
    set.at(bit / 32U) |= 1U << (bit % 32U);
}

bool has_atspi_state(const AtspiStateSet& set, AtspiStateValue state) {
    const auto bit = static_cast<std::uint32_t>(state);
    return (set.at(bit / 32U) & (1U << (bit % 32U))) != 0U;
}

int atspi_character_count(std::string_view text) {
    return static_cast<int>(decode_utf8_units(text).size());
}

std::string atspi_text_slice(std::string_view text, int start, int end) {
    const auto count = atspi_character_count(text);
    start = std::clamp(start, 0, count);
    end = end < 0 ? count : std::clamp(end, start, count);
    const auto first = byte_offset(text, start);
    return std::string(text.substr(first, byte_offset(text, end) - first));
}

std::optional<AccessibleAction> atspi_action_from_name(std::string_view name) {
    for (const auto action :
         {AccessibleAction::Activate, AccessibleAction::Focus, AccessibleAction::Toggle}) {
        if (name == accessible_action_name(action)) {
            return action;
        }
    }
    return std::nullopt;
}

} // namespace nk::detail
