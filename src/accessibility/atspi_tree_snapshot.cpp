#include "atspi_tree_snapshot.h"

#include "../text/text_boundaries.h"

#include <algorithm>
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
        AtspiTreeNode entry{.node = {}, .id = id};
        auto& node = entry.node;
        node.object_name = std::string(window_object_name) + "_n" + std::to_string(id);
        node.object_path = std::string(subtree_root_path) + "/" + node.object_name;
        node.parent_path = out[parent_index].node.object_path;
        node.role_name = atspi_role_name(accessible_role_name(info->role));
        node.name = info->name;
        node.description = info->description;
        node.value = info->value;
        node.bounds = info->bounds;
        node.state = state_bits(*info);
        for (const auto action : info->actions) {
            node.action_names.emplace_back(accessible_action_name(action));
        }
        node.interfaces = {AccessibleInterface, ComponentInterface};
        if (!node.action_names.empty()) {
            node.interfaces.emplace_back(ActionInterface);
        }
        if (!node.value.empty() || info->role == AccessibleRole::TextInput) {
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
                                                  Rect window_bounds) {
    std::vector<AtspiTreeNode> out;
    AtspiTreeNode window{.node = {}, .id = 0};
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
