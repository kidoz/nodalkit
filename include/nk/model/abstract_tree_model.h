#pragma once

/// @file abstract_tree_model.h
/// @brief Abstract interface for tree data models.

#include <any>
#include <cstddef>
#include <nk/foundation/signal.h>
#include <string>

namespace nk {

/// Opaque index identifying a node within an AbstractTreeModel.
/// Application-specific models provide the internal_id or internal_pointer
/// needed to quickly locate their underlying data node.
///
/// The default-constructed value {nullptr, 0} is reserved for the invisible
/// root, so id-keyed models must not use internal_id 0 for a real node
/// (offset stored ids by one, or use internal_pointer instead).
struct TreeIndex {
    void* internal_pointer = nullptr;
    std::size_t internal_id = 0;

    [[nodiscard]] bool is_valid() const { return internal_pointer != nullptr || internal_id != 0; }

    [[nodiscard]] bool operator==(const TreeIndex& other) const = default;
};

/// Abstract base for arbitrary tree data models — the tree counterpart of
/// AbstractListModel. Lets applications expose hierarchical data they own
/// without copying it into the toolkit.
///
/// This is the canonical view-facing tree contract going forward, but no
/// in-tree widget consumes it yet: TreeView currently consumes the concrete
/// in-memory TreeModel and will migrate to this contract during 0.x.
///
/// The model relies on TreeIndex to point to specific nodes. An invalid
/// TreeIndex (is_valid() == false) represents the invisible root of the tree.
class AbstractTreeModel {
public:
    virtual ~AbstractTreeModel();

    AbstractTreeModel(const AbstractTreeModel&) = delete;
    AbstractTreeModel& operator=(const AbstractTreeModel&) = delete;

    /// Number of child nodes under the given parent.
    /// An invalid parent index refers to the root elements.
    [[nodiscard]] virtual std::size_t
    children_count(const TreeIndex& parent = TreeIndex{}) const = 0;

    /// Returns the TreeIndex for the specified child of the parent.
    [[nodiscard]] virtual TreeIndex child(const TreeIndex& parent, std::size_t row) const = 0;

    /// Returns the parent TreeIndex for a given node.
    /// Should return an invalid TreeIndex if the node is at the root level.
    [[nodiscard]] virtual TreeIndex parent(const TreeIndex& index) const = 0;

    /// Returns the row of the index relative to its parent.
    [[nodiscard]] virtual std::size_t row_of(const TreeIndex& index) const = 0;

    /// Data for the given node. The std::any may hold any application
    /// type; delegates must know how to unpack it.
    [[nodiscard]] virtual std::any data(const TreeIndex& index) const = 0;

    /// Convenience: data as string (default returns empty).
    [[nodiscard]] virtual std::string display_text(const TreeIndex& index) const;

    // --- Change signals ---

    /// Emitted before nodes are inserted. Args: parent_index, first_row, count.
    Signal<TreeIndex, std::size_t, std::size_t>& on_nodes_about_to_insert();

    /// Emitted after nodes are inserted. Args: parent_index, first_row, count.
    Signal<TreeIndex, std::size_t, std::size_t>& on_nodes_inserted();

    /// Emitted before nodes are removed. Args: parent_index, first_row, count.
    Signal<TreeIndex, std::size_t, std::size_t>& on_nodes_about_to_remove();

    /// Emitted after nodes are removed. Args: parent_index, first_row, count.
    Signal<TreeIndex, std::size_t, std::size_t>& on_nodes_removed();

    /// Emitted when data for existing nodes changes.
    /// Args: parent_index, first_row, last_row.
    Signal<TreeIndex, std::size_t, std::size_t>& on_data_changed();

    /// Emitted when the model is completely reset.
    Signal<>& on_model_reset();

protected:
    AbstractTreeModel();

    /// Subclasses call these to fire change signals.
    void begin_insert_nodes(const TreeIndex& parent, std::size_t first, std::size_t count);
    void end_insert_nodes();
    void begin_remove_nodes(const TreeIndex& parent, std::size_t first, std::size_t count);
    void end_remove_nodes();
    void notify_data_changed(const TreeIndex& parent, std::size_t first, std::size_t last);
    void notify_model_reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
