#include <algorithm>
#include <cctype>
#include <nk/accessibility/atspi_bridge.h>

namespace nk {

namespace {

bool debug_node_is_accessible(const WidgetDebugNode& node) {
    return !node.accessible_hidden && !node.accessible_role.empty() &&
           node.accessible_role != "none";
}

void append_accessible_descendants(const WidgetDebugNode& widget_node,
                                   std::string_view subtree_root_path,
                                   std::string_view object_name_prefix,
                                   std::string_view parent_path,
                                   std::vector<AtspiAccessibleNode>& nodes,
                                   std::size_t& counter) {
    if (widget_node.accessible_hidden || !widget_node.visible) {
        return;
    }

    if (debug_node_is_accessible(widget_node)) {
        const std::string object_name =
            std::string(object_name_prefix) + "_n" + std::to_string(counter++);
        const std::string object_path = std::string(subtree_root_path) + "/" + object_name;

        AtspiAccessibleNode node;
        node.object_name = object_name;
        node.object_path = object_path;
        node.parent_path = std::string(parent_path);
        node.role_name = atspi_role_name(widget_node.accessible_role);
        node.name = widget_node.accessible_name.empty() ? widget_node.debug_name
                                                        : widget_node.accessible_name;
        node.description = widget_node.accessible_description;
        node.value = widget_node.accessible_value;
        node.bounds = widget_node.allocation;
        node.state = atspi_state_bits(widget_node);
        node.tree_path = widget_node.tree_path;
        node.action_names = widget_node.accessible_actions;
        node.relations = widget_node.accessible_relations;
        node.interfaces = {"org.a11y.atspi.Accessible", "org.a11y.atspi.Component"};
        if (!node.action_names.empty()) {
            node.interfaces.push_back("org.a11y.atspi.Action");
        }
        if (!node.value.empty()) {
            node.interfaces.push_back("org.a11y.atspi.Text");
        }

        const std::size_t node_index = nodes.size();
        nodes.push_back(std::move(node));

        for (const auto& child : widget_node.children) {
            const std::size_t child_start = nodes.size();
            append_accessible_descendants(
                child, subtree_root_path, object_name_prefix, object_path, nodes, counter);
            for (std::size_t child_index = child_start; child_index < nodes.size(); ++child_index) {
                if (nodes[child_index].parent_path == object_path) {
                    nodes[node_index].child_paths.push_back(nodes[child_index].object_path);
                }
            }
        }
        return;
    }

    for (const auto& child : widget_node.children) {
        append_accessible_descendants(
            child, subtree_root_path, object_name_prefix, parent_path, nodes, counter);
    }
}

} // namespace

std::string atspi_sanitize_object_name(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            sanitized += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else {
            sanitized += '_';
        }
    }
    while (!sanitized.empty() && sanitized.front() == '_') {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }
    if (sanitized.empty()) {
        sanitized = "node";
    }
    return sanitized;
}

std::string atspi_role_name(std::string_view accessible_role) {
    if (accessible_role == "button" || accessible_role == "togglebutton") {
        return "push button";
    }
    if (accessible_role == "checkbox") {
        return "check box";
    }
    if (accessible_role == "dialog") {
        return "dialog";
    }
    if (accessible_role == "grid") {
        return "table";
    }
    if (accessible_role == "gridcell") {
        return "table cell";
    }
    if (accessible_role == "image") {
        return "image";
    }
    if (accessible_role == "label") {
        return "label";
    }
    if (accessible_role == "link") {
        return "link";
    }
    if (accessible_role == "list") {
        return "list";
    }
    if (accessible_role == "listitem") {
        return "list item";
    }
    if (accessible_role == "menu") {
        return "menu";
    }
    if (accessible_role == "menubar") {
        return "menu bar";
    }
    if (accessible_role == "menuitem") {
        return "menu item";
    }
    if (accessible_role == "progressbar") {
        return "progress bar";
    }
    if (accessible_role == "radiobutton") {
        return "radio button";
    }
    if (accessible_role == "scrollbar") {
        return "scroll bar";
    }
    if (accessible_role == "separator") {
        return "separator";
    }
    if (accessible_role == "slider") {
        return "slider";
    }
    if (accessible_role == "spinbutton") {
        return "spin button";
    }
    if (accessible_role == "tab") {
        return "page tab";
    }
    if (accessible_role == "tablist") {
        return "page tab list";
    }
    if (accessible_role == "tabpanel") {
        return "panel";
    }
    if (accessible_role == "textinput") {
        return "text";
    }
    if (accessible_role == "toolbar") {
        return "tool bar";
    }
    if (accessible_role == "tree") {
        return "tree";
    }
    if (accessible_role == "treeitem") {
        return "tree item";
    }
    if (accessible_role == "window") {
        return "frame";
    }
    return accessible_role.empty() ? "unknown" : std::string(accessible_role);
}

AtspiStateBit atspi_state_bits(const WidgetDebugNode& node) {
    AtspiStateBit state = AtspiStateBit::None;
    if (node.sensitive) {
        state |= AtspiStateBit::Enabled;
        state |= AtspiStateBit::Sensitive;
    }
    if (node.visible && !node.accessible_hidden) {
        state |= AtspiStateBit::Visible;
        state |= AtspiStateBit::Showing;
    }
    if (node.focusable) {
        state |= AtspiStateBit::Focusable;
    }
    if (node.focused) {
        state |= AtspiStateBit::Focused;
    }
    if (node.hovered) {
        state |= AtspiStateBit::Hovered;
    }
    if (node.pressed) {
        state |= AtspiStateBit::Pressed;
    }
    return state;
}

AtspiAccessibleSnapshot build_atspi_window_snapshot(std::string_view subtree_root_path,
                                                    std::string_view window_object_name,
                                                    std::string_view window_title,
                                                    Rect window_bounds,
                                                    const WidgetDebugNode& widget_root) {
    AtspiAccessibleSnapshot snapshot;

    AtspiAccessibleNode window_node;
    window_node.object_name = atspi_sanitize_object_name(window_object_name);
    window_node.object_path = std::string(subtree_root_path) + "/" + window_node.object_name;
    window_node.role_name = "frame";
    window_node.name = std::string(window_title);
    window_node.bounds = window_bounds;
    window_node.state = AtspiStateBit::Enabled | AtspiStateBit::Sensitive | AtspiStateBit::Visible |
                        AtspiStateBit::Showing;
    window_node.interfaces = {"org.a11y.atspi.Accessible", "org.a11y.atspi.Component"};
    snapshot.nodes.push_back(window_node);

    std::size_t counter = 1;
    for (const auto& child : widget_root.children) {
        const std::size_t child_start = snapshot.nodes.size();
        append_accessible_descendants(child,
                                      subtree_root_path,
                                      window_node.object_name,
                                      window_node.object_path,
                                      snapshot.nodes,
                                      counter);
        for (std::size_t child_index = child_start; child_index < snapshot.nodes.size();
             ++child_index) {
            if (snapshot.nodes[child_index].parent_path == window_node.object_path) {
                snapshot.nodes.front().child_paths.push_back(
                    snapshot.nodes[child_index].object_path);
            }
        }
    }

    return snapshot;
}

const AtspiAccessibleNode* find_atspi_accessible_node(const AtspiAccessibleSnapshot& snapshot,
                                                      std::string_view object_path) noexcept {
    const auto it = std::find_if(
        snapshot.nodes.begin(), snapshot.nodes.end(), [&](const AtspiAccessibleNode& node) {
            return node.object_path == object_path;
        });
    return it != snapshot.nodes.end() ? &*it : nullptr;
}

} // namespace nk
