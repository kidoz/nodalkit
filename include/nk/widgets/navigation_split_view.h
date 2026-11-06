#pragma once

/// @file navigation_split_view.h
/// @brief Adaptive sidebar/content container for hierarchical navigation.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

class Headerbar;

/// Shows a sidebar and content side by side, or one pane at a time when
/// collapsed. Applications typically connect a Breakpoint to set_collapsed().
/// `set_show_sidebar(false)` hides the sidebar in the expanded layout.
/// Collapsed navigation still chooses its pane with `set_show_content()`.
///
/// `bind_headerbar()` shows that bar's back button while the split is
/// collapsed on the content pane. Activating it returns to the sidebar.
/// `unbind_headerbar()` restores the bar's previous back-button visibility.
class NavigationSplitView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<NavigationSplitView> create();
    ~NavigationSplitView() override;

    void set_sidebar(std::shared_ptr<Widget> sidebar);
    [[nodiscard]] Widget* sidebar() const;

    void set_content(std::shared_ptr<Widget> content);
    [[nodiscard]] Widget* content() const;

    [[nodiscard]] bool is_collapsed() const;
    void set_collapsed(bool collapsed);

    [[nodiscard]] bool shows_content() const;
    void set_show_content(bool show_content);

    /// Sidebar visibility for the expanded layout. Defaults to true.
    /// Collapsed mode ignores this and uses `shows_content()` instead.
    [[nodiscard]] bool shows_sidebar() const;
    void set_show_sidebar(bool show_sidebar);

    [[nodiscard]] float sidebar_width_fraction() const;
    void set_sidebar_width_fraction(float fraction);
    void set_min_sidebar_width(float width);
    void set_max_sidebar_width(float width);

    Signal<bool>& on_collapsed_changed();
    Signal<bool>& on_show_content_changed();
    Signal<bool>& on_show_sidebar_changed();

    void bind_headerbar(Headerbar& headerbar);
    void unbind_headerbar();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    NavigationSplitView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void sync_bound_back_button();
};

} // namespace nk
