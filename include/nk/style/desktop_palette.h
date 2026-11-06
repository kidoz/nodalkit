#pragma once

/// @file desktop_palette.h
/// @brief Extract named colors from an on-disk desktop theme stylesheet.
///
/// GNOME's full Adwaita palette lives inside libadwaita (compiled-in), which
/// NodalKit does not link against. As a pragmatic middle ground, this module
/// parses the `@define-color name value;` lines that desktop themes ship in
/// their stylesheet (for example `/usr/share/themes/Adwaita/gtk-4.0/gtk.css`).
/// When the theme or stylesheet is unavailable, `load_desktop_palette()` returns
/// `std::nullopt` and callers fall back to the hardcoded palette.

#include <nk/foundation/types.h>
#include <optional>
#include <string_view>

namespace nk {

/// Named colors parsed from a desktop theme stylesheet.
struct DesktopPalette {
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

/// Parse `@define-color` declarations from CSS text. This accepts literal hex
/// and rgb/rgba colors; unresolved functions and references are skipped.
[[nodiscard]] std::optional<DesktopPalette> parse_desktop_palette_css(std::string_view css);

/// Load the named colors for the desktop theme `theme_name` (for example
/// "Adwaita" or "Adwaita-dark"). Resolves `/usr/share/themes/<name>/gtk-4.0/`
/// and the user's `~/.config/gtk-4.0/gtk.css`, selecting `gtk-dark.css` when
/// `dark` is true. Returns `std::nullopt` when no stylesheet is found or no
/// recognized colors are defined. The theme name must be one path segment;
/// invalid names and stylesheets exceeding a combined 1 MiB are rejected.
[[nodiscard]] std::optional<DesktopPalette> load_desktop_palette(std::string_view theme_name,
                                                                 bool dark);

} // namespace nk
