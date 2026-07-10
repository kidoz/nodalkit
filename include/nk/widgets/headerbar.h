#pragma once

/// @file headerbar.h
/// @brief Client-side titlebar with window controls, in the GNOME/libadwaita style.
///
/// Place `Headerbar` as the first row of a vertical root layout. It shows the
/// window title, optional leading/trailing action widgets, and accessible
/// minimize/maximize/close buttons whenever the surface reports CSD. The empty
/// headerbar area starts a compositor-managed window move on Wayland.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A client-side headerbar. Add action widgets with `add_leading()` /
/// `add_trailing()`; the title and window controls are managed automatically.
/// Hide the window-control buttons with `set_window_controls_enabled(false)`.
class Headerbar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Headerbar> create(std::string title = {});
    ~Headerbar() override;

    /// Set the title text shown at the start of the bar.
    void set_title(std::string title);

    /// Add a widget to the leading (start) side of the bar.
    void add_leading(std::shared_ptr<Widget> item);
    /// Add a widget to the trailing (end) side, before the window controls.
    void add_trailing(std::shared_ptr<Widget> item);

    /// Show or hide the minimize/maximize/close buttons (shown by default).
    void set_window_controls_enabled(bool enabled);

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;

protected:
    explicit Headerbar(std::string title);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
