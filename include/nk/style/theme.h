#pragma once

/// @file theme.h
/// @brief Theme token system and style rule collection.

#include <memory>
#include <nk/style/style_class.h>
#include <nk/style/theme_selection.h>
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

    Theme(const Theme&) = delete;
    Theme& operator=(const Theme&) = delete;

    [[nodiscard]] std::string_view name() const;

    /// Add a style rule.
    void add_rule(StyleRule rule);

    /// Set a named token value (e.g. "accent-color", "spacing-sm").
    void set_token(std::string name, StyleValue value);

    /// Look up a token. Returns nullptr if not found.
    [[nodiscard]] const StyleValue* token(std::string_view name) const;

    /// Resolve a property for a widget given its type, classes, and state.
    /// `min_specificity` filters out weaker rules; pass 1 to skip the
    /// selector-less base defaults so class-specific inheritance can win.
    [[nodiscard]] const StyleValue* resolve(std::string_view type_name,
                                            const std::vector<std::string>& classes,
                                            StateFlags state,
                                            std::string_view property_name,
                                            int min_specificity = 0) const;

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

    /// Create the Windows 11 theme family (Mica-neutral, rounded controls).
    static std::unique_ptr<Theme> make_windows_11(ColorScheme color_scheme);

    /// Create the Windows 10 fallback theme family (opaque surfaces, squared
    /// controls, classic accent) for hosts older than Windows 11.
    static std::unique_ptr<Theme> make_windows_10(ColorScheme color_scheme);

    /// Create a macOS 26 placeholder theme family using shared tokens.
    static std::unique_ptr<Theme> make_macos_26(ColorScheme color_scheme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
