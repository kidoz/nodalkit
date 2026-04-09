#pragma once

/// @file list_view.h
/// @brief Scrollable list view backed by a data model.

#include <functional>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

class AbstractListModel;
class SelectionModel;

/// Factory function type for creating item widgets from model data.
using ItemFactory = std::function<std::shared_ptr<Widget>(std::size_t row)>;

/// A scrollable list view that creates item widgets on demand from
/// a model. Uses an ItemFactory (delegate) to create widgets for
/// visible rows.
class ListView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ListView> create();
    ~ListView() override;

    /// Set the data model.
    void set_model(std::shared_ptr<AbstractListModel> model);
    [[nodiscard]] AbstractListModel* model() const;

    /// Set the selection model.
    void set_selection_model(std::shared_ptr<SelectionModel> model);
    [[nodiscard]] SelectionModel* selection_model() const;

    /// Set the factory that creates a widget for each visible row.
    void set_item_factory(ItemFactory factory);

    /// Estimated row height for scrollbar calculations.
    void set_row_height(float height);

    /// Signal emitted when a row is activated (double-click / Enter).
    Signal<std::size_t>& on_row_activated();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    ListView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    [[nodiscard]] std::size_t clear_visible_items();
    void sync_visible_items();
    [[nodiscard]] Rect local_row_damage_rect(std::size_t row) const;
    void queue_row_redraw(std::size_t row);
    void queue_selection_redraw();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
