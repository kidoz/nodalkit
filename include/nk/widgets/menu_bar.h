#pragma once

/// @file menu_bar.h
/// @brief Horizontal menu bar widget with menu model.

#include <memory>
#include <nk/actions/action.h>
#include <nk/foundation/signal.h>
#include <nk/platform/native_menu.h>
#include <nk/ui_core/widget.h>
#include <string_view>

namespace nk {

using MenuItem = NativeMenuItem;
using Menu = NativeMenu;

/// Horizontal menu bar at the top of a window.
///
/// MenuBar is fully interactive: it draws a strip of top-level menu titles,
/// opens popup menus on click, tracks hover and armed state, supports
/// nested submenus, handles keyboard navigation (arrow keys, Escape, Return),
/// and emits on_action() with the action name when a leaf item is activated.
/// Popups are toolkit-level — rendered into the window's overlay layer, not
/// native OS menus. Use Application::set_native_app_menu() when you want
/// the native menu bar.
class MenuBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<MenuBar> create();
    ~MenuBar() override;

    /// Add a top-level menu. Items are described via NativeMenu / NativeMenuItem;
    /// leaf items carry an action name used for group activation when bound,
    /// or emitted from on_action() otherwise.
    void add_menu(Menu menu);

    /// Remove all top-level menus. Any currently open popup is dismissed.
    void clear();

    /// Bind menu action names to a shared registry. Missing/disabled actions
    /// disable their items; checked actions display a check mark. Null unbinds.
    void set_action_group(std::shared_ptr<ActionGroup> actions);
    [[nodiscard]] ActionGroup* action_group() const;

    /// Emitted when an unbound leaf menu item is activated by click or keyboard.
    /// With an action group, activation goes exclusively through that group.
    /// The argument is the item's action name (e.g. "file.open"). Items
    /// without an action name do not fire this signal.
    Signal<std::string_view>& on_action();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
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
    void sync_accessible_items();
    void rebuild_accessible_items();
    [[nodiscard]] bool item_enabled(const MenuItem& item) const;
    void activate_action(std::string name);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
