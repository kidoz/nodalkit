#pragma once

/// @file theme.h
/// @brief Theme token system and style rule collection.

#include <nk/style/style_class.h>
#include <nk/style/theme_selection.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nk {

/// A theme is a named collection of style rules and token values.
class Theme {
public:
    /// Create an empty named theme.
    explicit Theme(std::string name);
    ~Theme();

    Theme(Theme const&) = delete;
    Theme& operator=(Theme const&) = delete;

    [[nodiscard]] std::string_view name() const;

    /// Add a style rule.
    void add_rule(StyleRule rule);

    /// Set a named token value (e.g. "accent-color", "spacing-sm").
    void set_token(std::string name, StyleValue value);

    /// Look up a token. Returns nullptr if not found.
    [[nodiscard]] StyleValue const* token(std::string_view name) const;

    /// Resolve a property for a widget given its type, classes, and state.
    [[nodiscard]] StyleValue const* resolve(
        std::string_view type_name,
        std::vector<std::string> const& classes,
        StateFlags state,
        std::string_view property_name) const;

    /// Set the process-wide active theme used by widget snapshot helpers.
    static void set_active(std::shared_ptr<Theme> theme);

    /// Return the process-wide active theme, creating a default light theme if
    /// none has been installed yet.
    [[nodiscard]] static std::shared_ptr<Theme> active();

    /// Create a Linux-first light theme with semantic tokens and widget rules.
    static std::unique_ptr<Theme> make_light();

    /// Create a Linux-first dark theme with semantic tokens and widget rules.
    static std::unique_ptr<Theme> make_dark();

    /// Create the current GNOME-first Linux theme family.
    static std::unique_ptr<Theme> make_linux_gnome(ColorScheme color_scheme);

    /// Create a Windows 11 placeholder theme family using shared tokens.
    static std::unique_ptr<Theme> make_windows_11(ColorScheme color_scheme);

    /// Create a macOS 26 placeholder theme family using shared tokens.
    static std::unique_ptr<Theme> make_macos_26(ColorScheme color_scheme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
