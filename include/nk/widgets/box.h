#pragma once

/// @file box.h
/// @brief General-purpose row or column container.

#include <cstddef>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

class BoxLayout;

/// A transparent container arranging children in a row or column.
/// Uses BoxLayout, including child margins, stretch, and hidden-size policies.
/// Child mutations and layout setters automatically invalidate layout.
class Box : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Box>
    create(Orientation orientation = Orientation::Vertical, float spacing = 0.0F);
    ~Box() override;

    [[nodiscard]] Orientation orientation() const;
    void set_orientation(Orientation orientation);

    [[nodiscard]] float spacing() const;
    /// Spacing must be finite and nonnegative; otherwise throws invalid_argument.
    void set_spacing(float spacing);
    [[nodiscard]] bool homogeneous() const;
    void set_homogeneous(bool homogeneous);

    /// Append, prepend, or insert an unattached child. Throws invalid_argument
    /// for null children, cycles, or children already in a tree or window.
    void append(std::shared_ptr<Widget> child);
    void prepend(std::shared_ptr<Widget> child);
    /// Indices beyond the child count append at the end.
    void insert(std::size_t index, std::shared_ptr<Widget> child);
    /// Detach a child; a widget not in this box is ignored.
    void remove(Widget& child);
    void clear();

protected:
    Box(Orientation orientation, float spacing);

private:
    // Configure the built-in layout through Box's setters.
    using Widget::set_layout_manager;
    [[nodiscard]] BoxLayout& box_layout() const;
};

} // namespace nk
