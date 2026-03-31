#pragma once

/// @file stack_layout.h
/// @brief Overlay/z-stack layout manager.

#include <nk/layout/layout_manager.h>

namespace nk {

/// Arranges children on top of each other inside the same allocation.
///
/// Visible children all receive the full container rectangle, making this
/// suitable for overlays, page stacks, and single-visible-child switchers.
class StackLayout : public LayoutManager {
public:
    StackLayout();
    ~StackLayout() override;

    [[nodiscard]] SizeRequest measure(const Widget& widget,
                                      const Constraints& constraints) const override;

    void allocate(Widget& widget, const Rect& allocation) override;
};

} // namespace nk
