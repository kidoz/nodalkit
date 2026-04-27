#pragma once

/// @file tree_view.h
/// @brief Outline/tree widget backed by TreeModel.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/model/tree_model.h>
#include <nk/ui_core/widget.h>

namespace nk {

class SelectionModel;

/// Tree/outline view with disclosure arrows, row selection, and keyboard navigation.
class TreeView : public Widget {
public:
    /// Creates a TreeView owned by a shared pointer.
    [[nodiscard]] static std::shared_ptr<TreeView> create();
    ~TreeView() override;

    /// Sets the hierarchical model displayed by the view.
    void set_model(std::shared_ptr<TreeModel> model);
    /// Returns the currently attached model, or nullptr.
    [[nodiscard]] TreeModel* model() const;

    /// Sets the selection model used for selected/current nodes.
    void set_selection_model(std::shared_ptr<SelectionModel> model);
    /// Returns the currently attached selection model, or nullptr.
    [[nodiscard]] SelectionModel* selection_model() const;

    /// Sets the visual row height in logical pixels.
    void set_row_height(float height);
    /// Returns the visual row height in logical pixels.
    [[nodiscard]] float row_height() const;

    /// Sets horizontal indentation per hierarchy level.
    void set_indent_width(float width);
    /// Returns horizontal indentation per hierarchy level.
    [[nodiscard]] float indent_width() const;

    /// Emitted with a node ID when a row is activated.
    Signal<TreeNodeId>& on_node_activated();

    /// Measures the view from the current model and row-height settings.
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    TreeView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    void reconnect_model();
    void queue_selection_redraw();
    void sync_accessible_summary();
    void select_node(TreeNodeId node);
    void ensure_node_visible(TreeNodeId node);
    [[nodiscard]] TreeNodeId current_node() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
