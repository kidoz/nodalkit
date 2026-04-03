#pragma once

/// @file menu_bar.h
/// @brief Horizontal menu bar widget with menu model.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/platform/native_menu.h>
#include <nk/ui_core/widget.h>
#include <string_view>

namespace nk {

using MenuItem = NativeMenuItem;
using Menu = NativeMenu;

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
