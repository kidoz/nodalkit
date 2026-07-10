#pragma once

/// @file gtk_palette.h
/// @brief Extract named colors from an on-disk GTK theme stylesheet.
///
/// GNOME's full Adwaita palette lives inside libadwaita (compiled-in), which
/// NodalKit does not link against. As a pragmatic middle ground, this module
/// parses the `@define-color name value;` lines that GTK themes ship in their
/// stylesheet (e.g. `/usr/share/themes/Adwaita/gtk-4.0/gtk.css`). When the theme
/// or stylesheet is unavailable, `GtkPalette::load()` returns `std::nullopt`
/// and callers fall back to the hardcoded palette.

#include <nk/foundation/types.h>
#include <optional>
#include <string_view>

namespace nk {

/// A handful of named colors parsed from a GTK theme stylesheet.
struct GtkPalette {
    Color window_bg{};
    Color window_fg{};
    Color view_bg{};
    Color view_fg{};
    Color accent_bg{};
    Color accent_fg{};
    Color borders{};
    Color borders_dark{};

    bool has_window_bg = false;
    bool has_window_fg = false;
    bool has_view_bg = false;
    bool has_view_fg = false;
    bool has_accent_bg = false;
    bool has_accent_fg = false;
    bool has_borders = false;
    bool has_borders_dark = false;
};

/// Parse GTK `@define-color` declarations from CSS text. This accepts literal
/// hex and rgb/rgba colors; unresolved GTK functions and references are skipped.
[[nodiscard]] std::optional<GtkPalette> parse_gtk_palette_css(std::string_view css);

/// Load the named colors from the GTK theme named `gtk_theme_name` (e.g.
/// "Adwaita", "Adwaita-dark"). Resolves `/usr/share/themes/<name>/gtk-4.0/` and
/// the user's `~/.config/gtk-4.0/gtk.css`, selecting `gtk-dark.css` when
/// `dark` is true. Returns `std::nullopt` when no stylesheet is found or no
/// recognized colors are defined.
[[nodiscard]] std::optional<GtkPalette> load_gtk_palette(std::string_view gtk_theme_name,
                                                         bool dark);

} // namespace nk
