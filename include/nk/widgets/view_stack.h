#pragma once

/// @file view_stack.h
/// @brief One visible page chosen from a set of peer pages.

#include <cstddef>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

class SegmentedControl;

/// Shows one page at a time from a set of peer pages.
///
/// `bind_switcher()` keeps a `SegmentedControl` on the same selection: the
/// control's segments are the page titles, and changing either side updates
/// the other. `unbind_switcher()` stops that link and leaves the control's
/// current segments in place.
///
/// `append()` and `insert()` throw `std::invalid_argument` for a null page.
/// A page that is already in a widget tree is rejected by the child contract.
/// `set_selected_index()` throws `std::out_of_range` when the index does not
/// name a page.
class ViewStack : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ViewStack> create();
    ~ViewStack() override;

    void append(std::shared_ptr<Widget> page, std::string title);
    /// Indices past the last page are rejected.
    void insert(std::size_t index, std::shared_ptr<Widget> page, std::string title);
    /// A page that is not in this stack is ignored.
    void remove(Widget& page);
    void clear();

    [[nodiscard]] std::size_t page_count() const;
    [[nodiscard]] std::string_view title(std::size_t index) const;
    [[nodiscard]] Widget* page(std::size_t index) const;

    /// Selected page, or -1 when the stack is empty.
    [[nodiscard]] int selected_index() const;
    void set_selected_index(int index);
    [[nodiscard]] Widget* current() const;

    void bind_switcher(SegmentedControl& control);
    void unbind_switcher();

    /// Emitted with the new index when the selected page changes.
    Signal<int>& on_selection_changed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    ViewStack();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void reveal();
    void sync_switcher();
};

} // namespace nk
