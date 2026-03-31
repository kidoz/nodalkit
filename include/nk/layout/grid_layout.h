#pragma once

/// @file grid_layout.h
/// @brief Row/column grid layout with span support.

#include <nk/layout/layout_manager.h>
#include <unordered_map>

namespace nk {

/// Grid layout that arranges children into rows and columns.
///
/// Children are assigned to cells explicitly through attach(). Spans are
/// supported for forms, settings panes, and dashboard-style compositions.
class GridLayout : public LayoutManager {
public:
    struct Placement {
        int row = 0;
        int column = 0;
        int row_span = 1;
        int column_span = 1;
    };

    GridLayout();
    ~GridLayout() override;

    [[nodiscard]] float row_spacing() const;
    void set_row_spacing(float spacing);

    [[nodiscard]] float column_spacing() const;
    void set_column_spacing(float spacing);

    /// Attach a child to the given grid coordinates.
    void attach(Widget& child, int row, int column, int row_span = 1, int column_span = 1);

    /// Remove explicit placement metadata for a child.
    void remove(Widget& child);

    /// Clear all explicit placements.
    void clear();

    [[nodiscard]] SizeRequest measure(const Widget& widget,
                                      const Constraints& constraints) const override;

    void allocate(Widget& widget, const Rect& allocation) override;

private:
    [[nodiscard]] Placement placement_for(const Widget& child, int fallback_index) const;

    float row_spacing_ = 0.0F;
    float column_spacing_ = 0.0F;
    std::unordered_map<const Widget*, Placement> placements_;
};

} // namespace nk
