#pragma once

/// @file box_layout.h
/// @brief Linear layout manager (horizontal or vertical).

#include <nk/foundation/types.h>
#include <nk/layout/layout_manager.h>

namespace nk {

/// Arranges children in a single row or column.
///
/// Each child is measured with the remaining space. Children can be
/// given homogeneous sizing or expand to fill available space.
class BoxLayout : public LayoutManager {
public:
    /// Create a box layout with the given orientation.
    explicit BoxLayout(Orientation orientation = Orientation::Vertical);
    ~BoxLayout() override;

    /// Spacing between children in logical pixels.
    [[nodiscard]] float spacing() const;
    void set_spacing(float spacing);

    /// If true, all children get equal size along the main axis.
    [[nodiscard]] bool homogeneous() const;
    void set_homogeneous(bool homogeneous);

    // --- LayoutManager interface ---

    [[nodiscard]] bool has_height_for_width(const Widget& widget) const override;
    [[nodiscard]] float height_for_width(const Widget& widget, float width) const override;

    [[nodiscard]] SizeRequest measure(const Widget& widget,
                                      const Constraints& constraints) const override;

    void allocate(Widget& widget, const Rect& allocation) override;

private:
    Orientation orientation_;
    float spacing_ = 0;
    bool homogeneous_ = false;
};

} // namespace nk
