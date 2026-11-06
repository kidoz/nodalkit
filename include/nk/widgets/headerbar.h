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
#include <vector>

namespace nk {

/// Policy used to place the title between asymmetric headerbar controls.
enum class HeaderbarCenteringPolicy {
    Loose,  ///< Center in the free space between the start and end controls.
    Strict, ///< Center relative to the whole window whenever space permits.
};

/// A client-side headerbar. Add action widgets with `add_leading()` /
/// `add_trailing()`; the title and window controls are managed automatically.
/// Hide the window-control buttons with `set_window_controls_enabled(false)`.
class Headerbar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Headerbar> create(std::string title = {});
    ~Headerbar() override;

    /// Set the title text shown at the start of the bar.
    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);

    /// Secondary line under the title, matching GNOME's title/subtitle window
    /// heading. Empty (the default) renders the title alone, vertically centered.
    [[nodiscard]] std::string_view subtitle() const;
    void set_subtitle(std::string subtitle);

    /// Replace the centered title string with a widget, such as a
    /// `WindowTitle` or a view switcher. Null restores the title string.
    /// The widget stays between the leading and trailing controls and follows
    /// the bar's centering policy.
    void set_title_widget(std::shared_ptr<Widget> widget);
    [[nodiscard]] Widget* title_widget() const;

    /// Add a widget to the leading (start) side of the bar.
    void add_leading(std::shared_ptr<Widget> item);
    /// Add a widget to the trailing (end) side, before the window controls.
    ///
    /// Trailing widgets that fit at their natural width stay, packed inward
    /// from the window controls. The ones that do not fit move into an
    /// overflow popover opened from a button at the inner edge of that group.
    /// They stay the same widgets. A wide bar has no overflow button.
    void add_trailing(std::shared_ptr<Widget> item);

    /// Show or hide the minimize/maximize/close buttons (shown by default).
    void set_window_controls_enabled(bool enabled);

    /// Override the platform decoration layout. An empty string follows the
    /// system setting. The syntax is "start-controls:end-controls".
    [[nodiscard]] std::string_view decoration_layout() const;
    void set_decoration_layout(std::string layout);

    [[nodiscard]] HeaderbarCenteringPolicy centering_policy() const;
    void set_centering_policy(HeaderbarCenteringPolicy policy);

    /// Show a contextual back button at the start of the headerbar.
    [[nodiscard]] bool shows_back_button() const;
    void set_show_back_button(bool show);
    Signal<>& on_back_requested();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;

protected:
    explicit Headerbar(std::string title);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] float centered_content_width() const;
    void sync_trailing_overflow(const std::vector<int>& overflow);
    void open_trailing_overflow();
};

} // namespace nk
