#pragma once

/// @file tree_model.h
/// @brief Simple hierarchical model with stable node IDs and expansion state.

#include <cstddef>
#include <nk/foundation/signal.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Stable identifier for a node owned by a TreeModel.
using TreeNodeId = std::size_t;

/// Sentinel value used when a node relationship or selection is absent.
inline constexpr TreeNodeId InvalidTreeNodeId = static_cast<TreeNodeId>(-1);

/// In-memory tree model for TreeView and outline-style widgets.
class TreeModel {
public:
    TreeModel();
    ~TreeModel();

    TreeModel(const TreeModel&) = delete;
    TreeModel& operator=(const TreeModel&) = delete;

    /// Appends a top-level node and returns its stable ID.
    [[nodiscard]] TreeNodeId add_root(std::string text);
    /// Appends a child under an existing node and returns its stable ID.
    [[nodiscard]] TreeNodeId append_child(TreeNodeId parent, std::string text);
    /// Removes every node and emits a reset signal when the model was non-empty.
    void clear();

    /// Returns true when the node ID currently belongs to this model.
    [[nodiscard]] bool contains(TreeNodeId node) const;
    /// Returns the user-visible text for a node.
    [[nodiscard]] std::string_view display_text(TreeNodeId node) const;
    /// Replaces a node label and emits a reset signal.
    void set_display_text(TreeNodeId node, std::string text);

    /// Returns the node parent, or InvalidTreeNodeId for a root node.
    [[nodiscard]] TreeNodeId parent(TreeNodeId node) const;
    /// Returns the direct children of a node in display order.
    [[nodiscard]] std::span<const TreeNodeId> children(TreeNodeId node) const;
    /// Returns root nodes in display order.
    [[nodiscard]] std::span<const TreeNodeId> roots() const;
    /// Returns true when the node has one or more direct children.
    [[nodiscard]] bool has_children(TreeNodeId node) const;
    /// Returns the node depth, with roots at depth zero.
    [[nodiscard]] std::size_t depth(TreeNodeId node) const;

    /// Updates whether a node's descendants are visible.
    void set_expanded(TreeNodeId node, bool expanded);
    /// Toggles whether a node's descendants are visible.
    void toggle_expanded(TreeNodeId node);
    /// Returns true when a node's descendants are visible.
    [[nodiscard]] bool is_expanded(TreeNodeId node) const;

    /// Returns the number of currently visible flattened rows.
    [[nodiscard]] std::size_t visible_row_count() const;
    /// Returns the node shown at a visible row.
    [[nodiscard]] TreeNodeId visible_node_at(std::size_t row) const;
    /// Returns the visible row for a node, or std::nullopt when hidden.
    [[nodiscard]] std::optional<std::size_t> visible_row_for_node(TreeNodeId node) const;

    /// Emits when structure, labels, or expansion state changes.
    Signal<>& on_model_reset();

private:
    struct Node {
        TreeNodeId id = InvalidTreeNodeId;
        TreeNodeId parent = InvalidTreeNodeId;
        std::string text;
        std::vector<TreeNodeId> children;
        bool expanded = true;
    };

    [[nodiscard]] const Node& node(TreeNodeId id) const;
    [[nodiscard]] Node& node(TreeNodeId id);
    void append_visible_nodes(TreeNodeId node, std::vector<TreeNodeId>& visible) const;
    [[nodiscard]] std::vector<TreeNodeId> visible_nodes() const;

    std::vector<Node> nodes_;
    std::vector<TreeNodeId> roots_;
    Signal<> model_reset_;
};

} // namespace nk
