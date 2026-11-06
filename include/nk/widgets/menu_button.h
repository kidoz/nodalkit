#pragma once

/// @file menu_button.h
/// @brief Button that opens a menu of actions.

#include <memory>
#include <nk/actions/action.h>
#include <nk/foundation/signal.h>
#include <nk/platform/native_menu.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A button that opens a `ContextMenu` anchored to its bottom edge.
///
/// The menu uses `NativeMenuItem` and follows `ContextMenu` action rules:
/// a bound group runs the action, and an unbound name is emitted from
/// `on_action()`. Root leaves also emit `on_item_activated()`. Return, Space,
/// or Down opens the menu. Choosing an item, pressing Escape, or moving focus
/// away closes it and returns focus here.
class MenuButton : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<MenuButton> create(std::string label = {});
    ~MenuButton() override;

    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    /// Replace the menu contents. An open menu keeps its current items until
    /// the next time it opens.
    void set_items(std::vector<NativeMenuItem> items);

    /// Bind menu action names. Null unbinds. See `ContextMenu::set_action_group()`.
    void set_action_group(std::shared_ptr<ActionGroup> actions);
    [[nodiscard]] ActionGroup* action_group() const;

    [[nodiscard]] bool is_open() const;

    Signal<int>& on_item_activated();
    Signal<std::string_view>& on_action();
    Signal<>& on_dismissed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit MenuButton(std::string label);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void toggle_menu();
    [[nodiscard]] bool activate();
};

} // namespace nk
