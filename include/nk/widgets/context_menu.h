#pragma once

/// @file context_menu.h
/// @brief Right-click popup menu widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A popup menu shown on right-click or programmatic request.
class ContextMenu : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ContextMenu> create();
    ~ContextMenu() override;

    /// Add a menu item with a label.
    void add_item(std::string label);

    /// Add a separator between items.
    void add_separator();

    /// Remove all items.
    void clear();

    [[nodiscard]] std::size_t item_count() const;

    /// Show the menu at the given position (window coordinates).
    void show_at(Point position);

    /// Hide the menu.
    void dismiss();

    [[nodiscard]] bool is_open() const;

    /// Emitted when an item is activated, with its index.
    Signal<int>& on_item_activated();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;

protected:
    ContextMenu();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
