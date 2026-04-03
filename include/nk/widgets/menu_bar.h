#pragma once

/// @file menu_bar.h
/// @brief Horizontal menu bar widget with menu model.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A single menu item (can have submenu children).
struct MenuItem {
    std::string label;
    std::string action_name; ///< Links to ActionGroup (e.g. "app.open").
    bool enabled = true;
    bool separator = false; ///< If true, renders as a separator line.
    std::vector<MenuItem> children;

    /// Create an action item with a label and action name.
    static MenuItem action(std::string label, std::string action);

    /// Create a submenu item containing child items.
    static MenuItem submenu(std::string label, std::vector<MenuItem> items);

    /// Create a separator line.
    static MenuItem make_separator();
};

/// A top-level menu in the menu bar.
struct Menu {
    std::string title;
    std::vector<MenuItem> items;
};

/// Horizontal menu bar at the top of a window.
class MenuBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<MenuBar> create();
    ~MenuBar() override;

    /// Add a top-level menu.
    void add_menu(Menu menu);

    /// Remove all menus.
    void clear();

    /// Emitted when a menu action is activated. Arg: action name.
    Signal<std::string_view>& on_action();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    MenuBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
