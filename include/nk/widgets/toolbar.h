#pragma once

/// @file toolbar.h
/// @brief Horizontal container for action buttons and controls.

#include <memory>
#include <nk/ui_core/widget.h>

namespace nk {

/// A horizontal toolbar that arranges action controls in one row.
///
/// Items that fit stay visible from the leading side. The rest move into an
/// overflow popover opened from a trailing button, and they stay the same
/// widgets. A bar wide enough for every item has no overflow button.
///
/// The toolbar is one Tab stop. Left and Right move across the visible
/// focusable items and skip separators. Home and End move to the first and
/// last of those stops. The overflow button is one of the stops when it is
/// shown, and Return or Space opens it.
class Toolbar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Toolbar> create();
    ~Toolbar() override;

    /// Add a widget to the toolbar.
    void add_item(std::shared_ptr<Widget> item);

    /// Insert a visual separator at the current position.
    void add_separator();

    /// Remove all items.
    void clear_items();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    Toolbar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    struct Item {
        std::shared_ptr<Widget> widget;
        bool focusable = false;
    };

    void restore_focus(Item& item);
    void sync_overflow(const std::vector<int>& visible, const std::vector<int>& overflow);
    [[nodiscard]] std::vector<int> keyboard_stops() const;
    void open_overflow();
    void activate_stop(int stop);
};

} // namespace nk
