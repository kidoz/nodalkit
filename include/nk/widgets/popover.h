#pragma once

/// @file popover.h
/// @brief Anchored popup container.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// An anchored popup that displays a child widget relative to a
/// target position. Used for dropdown content, rich tooltips, etc.
class Popover : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Popover> create();
    ~Popover() override;

    /// Set the content widget displayed inside the popover.
    void set_child(std::shared_ptr<Widget> child);

    /// Show the popover anchored at the given position.
    void show_at(Point anchor);

    /// Dismiss the popover.
    void dismiss();

    [[nodiscard]] bool is_open() const;

    Signal<>& on_dismissed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;

protected:
    Popover();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
