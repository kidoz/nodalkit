#pragma once

/// @file popover.h
/// @brief Anchored popup container.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// An anchored popup that displays one child widget.
///
/// `set_anchor()` ties the popup to a widget. `show()` opens it beside that
/// widget, preferring the space below and flipping above when the window has
/// no room. `show_at()` opens at a window point instead. An arrow points back
/// at the anchor. Focus moves into the content, and Escape or an outside
/// press closes the popup and returns focus to the widget that was focused
/// when it opened. While open, the popup is hit-tested before its siblings so
/// an outside press dismisses it.
class Popover : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Popover> create();
    ~Popover() override;

    /// Set the content widget displayed inside the popover.
    void set_child(std::shared_ptr<Widget> child);

    /// Anchor the popup to a widget. The popover does not own the anchor.
    void set_anchor(Widget& anchor);
    void clear_anchor();

    /// Open beside the anchor widget.
    void show();

    /// Open at a window point and clear any widget anchor.
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

    void open_beside(Rect anchor_rect);
    void update_popup_geometry();
    [[nodiscard]] Widget* target_at(Point point) const;
    [[nodiscard]] bool contains_widget(const Widget* widget) const;
};

} // namespace nk
