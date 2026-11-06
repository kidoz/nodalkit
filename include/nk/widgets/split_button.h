#pragma once

/// @file split_button.h
/// @brief Button with a primary action and an attached menu.

#include <memory>
#include <nk/actions/action.h>
#include <nk/foundation/signal.h>
#include <nk/platform/native_menu.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A button split into a primary action and a menu.
///
/// The label side runs `set_action()` or emits `on_clicked()`, using the same
/// rule as `Button`: a bound action does not emit `on_clicked()`. The chevron
/// opens a `ContextMenu`. Return and Space run the primary action. Down opens
/// the menu. A disabled bound action disables the whole control, including the
/// menu.
class SplitButton : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<SplitButton> create(std::string label = {});
    ~SplitButton() override;

    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    /// Bind the primary command. Null unbinds and restores sensitivity.
    void set_action(std::shared_ptr<Action> action);
    [[nodiscard]] Action* action() const;

    /// Emitted when the label side is activated and no action is bound.
    Signal<>& on_clicked();

    void set_items(std::vector<NativeMenuItem> items);
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
    explicit SplitButton(std::string label);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] Rect menu_rect() const;
    void toggle_menu();
    [[nodiscard]] bool activate_primary();
    void sync_action();
    void sync_content_label();
};

} // namespace nk
