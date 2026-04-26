#pragma once

/// @file layout_manager.h
/// @brief Base class for layout strategies.

#include <nk/layout/constraints.h>

namespace nk {

class Widget;

/// Abstract base for layout managers. A layout manager controls how
/// a container widget measures and arranges its children.
///
/// The layout pipeline has two phases:
/// 1. **Measure**: compute this widget's SizeRequest given constraints.
/// 2. **Allocate**: assign each child a Rect within the allocated space.
class LayoutManager {
public:
    virtual ~LayoutManager();

    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    /// Measure the container and its children.
    [[nodiscard]] virtual SizeRequest measure(const Widget& widget,
                                              const Constraints& constraints) const = 0;

    /// Whether this layout depends on width to compute height.
    [[nodiscard]] virtual bool has_height_for_width(const Widget& widget) const;

    /// Compute the required height for a given width.
    [[nodiscard]] virtual float height_for_width(const Widget& widget, float width) const;

    /// Allocate positions for all children within the given rectangle.
    virtual void allocate(Widget& widget, const Rect& allocation) = 0;

protected:
    LayoutManager();
};

} // namespace nk
