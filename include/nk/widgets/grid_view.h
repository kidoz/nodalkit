#pragma once

/// @file grid_view.h
/// @brief Uniform grid view backed by a list model.

#include <cstddef>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

class AbstractListModel;
class SelectionModel;

/// A model-backed grid of uniformly sized cells with selection and keyboard navigation.
class GridView : public Widget {
public:
    /// Creates a GridView owned by a shared pointer.
    [[nodiscard]] static std::shared_ptr<GridView> create();
    ~GridView() override;

    /// Sets the item model displayed by the grid.
    void set_model(std::shared_ptr<AbstractListModel> model);
    /// Returns the currently attached model, or nullptr.
    [[nodiscard]] AbstractListModel* model() const;

    /// Sets the selection model used for selected/current items.
    void set_selection_model(std::shared_ptr<SelectionModel> model);
    /// Returns the currently attached selection model, or nullptr.
    [[nodiscard]] SelectionModel* selection_model() const;

    /// Sets each cell's width in logical pixels.
    void set_cell_width(float width);
    /// Returns each cell's width in logical pixels.
    [[nodiscard]] float cell_width() const;

    /// Sets each cell's height in logical pixels.
    void set_cell_height(float height);
    /// Returns each cell's height in logical pixels.
    [[nodiscard]] float cell_height() const;

    /// Sets the spacing between cells in logical pixels.
    void set_gap(float gap);
    /// Returns the spacing between cells in logical pixels.
    [[nodiscard]] float gap() const;

    /// Emitted with a model row when an item is activated.
    Signal<std::size_t>& on_item_activated();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    GridView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    void reconnect_model();
    void queue_selection_redraw();
    void sync_accessible_summary();
    void select_item(std::size_t row);
    void ensure_item_visible(std::size_t row);
    [[nodiscard]] std::size_t current_item() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
