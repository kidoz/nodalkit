#pragma once

/// @file navigation_stack.h
/// @brief Stack of titled pages with header-bar back navigation.

#include <cstddef>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

class Headerbar;

/// A stack of pages. Only the top page is visible.
///
/// `bind_headerbar()` keeps that bar's title equal to the current page title
/// and shows its back button only when another page sits underneath. The back
/// button pops one page. `pop()` with a single page, and `pop_to()` to the
/// current depth, do nothing.
///
/// `set_root()` and `push()` throw `std::invalid_argument` for a null page.
/// A page that is already in a widget tree is rejected by the child contract.
class NavigationStack : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<NavigationStack> create();
    ~NavigationStack() override;

    /// Replace the stack with a single root page.
    void set_root(std::shared_ptr<Widget> page, std::string title);

    /// Push a page above the current one.
    void push(std::shared_ptr<Widget> page, std::string title);

    /// Remove the top page when one sits above the root.
    void pop();

    /// Leave `depth` pages on the stack, dropping every page above them.
    /// Zero clears the stack.
    void pop_to(std::size_t depth);

    [[nodiscard]] std::size_t depth() const;
    [[nodiscard]] std::string_view title() const;
    [[nodiscard]] Widget* current() const;

    /// Follow this stack from `headerbar` until `unbind_headerbar()`.
    void bind_headerbar(Headerbar& headerbar);
    void unbind_headerbar();

    /// Emitted with the new depth after the stack changes.
    Signal<std::size_t>& on_depth_changed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    NavigationStack();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void discard_to(std::size_t depth);
    void reveal_top();
    void sync_headerbar();
};

} // namespace nk
