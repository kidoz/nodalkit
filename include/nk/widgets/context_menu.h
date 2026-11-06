#pragma once

/// @file context_menu.h
/// @brief Right-click popup menu widget.

#include <memory>
#include <nk/actions/action.h>
#include <nk/foundation/signal.h>
#include <nk/platform/native_menu.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A popup menu shown on right-click or programmatic request.
///
/// Items are `NativeMenuItem` values, the same model `MenuBar` uses. A menu
/// may contain separators and one level of submenu. `add_item()` remains a
/// label-only convenience: those items report their index through
/// `on_item_activated()`.
///
/// With an action group, a named leaf runs that action and does not emit
/// `on_action()`. Without a group, a non-empty action name is emitted from
/// `on_action()`. A root-level leaf also emits `on_item_activated()` with its
/// index, including separators in the count. Submenu leaves report only the
/// action name.
class ContextMenu : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ContextMenu> create();
    ~ContextMenu() override;

    /// Append a label-only item. Activation reports its index.
    void add_item(std::string label);

    /// Add a separator between items.
    void add_separator();

    /// Replace the menu contents. Closes an open submenu highlight.
    void set_items(std::vector<NativeMenuItem> items);

    /// Remove all items.
    void clear();

    [[nodiscard]] std::size_t item_count() const;

    /// Bind item action names to a shared registry. Missing or disabled
    /// actions disable their leaves; checked actions display a check mark.
    /// Null unbinds the group.
    void set_action_group(std::shared_ptr<ActionGroup> actions);
    [[nodiscard]] ActionGroup* action_group() const;

    /// Show the menu at the given position (window coordinates).
    /// Keyboard focus moves to the menu and returns to the previously focused
    /// widget when the menu closes.
    void show_at(Point position);

    /// Hide the menu.
    void dismiss();

    [[nodiscard]] bool is_open() const;

    /// Emitted when a root-level leaf is activated, with its index.
    Signal<int>& on_item_activated();

    /// Emitted when an unbound named leaf is activated. The argument is the
    /// item's action name. A bound action group handles activation itself.
    Signal<std::string_view>& on_action();

    /// Emitted when the menu is dismissed without an item activation
    /// (Escape or outside click). The menu is already hidden when this fires.
    Signal<>& on_dismissed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;

protected:
    ContextMenu();
    void snapshot(SnapshotContext& ctx) const override;
    void on_focus_changed(bool focused) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void append_model_item(NativeMenuItem item);
    void sync_accessible_items();
    void dismiss_without_activation();
    void activate_leaf(int root_index, int child_index);
    void move_highlight(int delta);
    void open_highlighted_submenu();
    void close_submenu();
    [[nodiscard]] bool item_enabled(const NativeMenuItem& item) const;
    void bind_actions();
};

} // namespace nk
