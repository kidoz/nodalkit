#pragma once

/// @file theme_defaults.h
/// @brief Central defensive fallbacks for widget theme properties.

#include <array>
#include <nk/foundation/types.h>
#include <string_view>

namespace nk {

/// One defensive fallback: the widget property it backs, the light-theme token
/// it aliases, and that token's value. The value must stay equal to the token
/// as defined by Theme::make_light() — a parity test enforces this — so a
/// missing token degrades invisibly instead of introducing a second design.
struct ThemeColorDefault {
    std::string_view property;
    std::string_view token;
    Color value;
};

/// The single source of widget color fallbacks. Widgets read these through the
/// fallback-free Widget::theme_color(property) overload instead of repeating
/// (and drifting from) the light palette at every call site. Properties whose
/// default genuinely differs per widget (e.g. "background") are not listed;
/// those call sites pass an explicit fallback equal to their light-theme value.
inline constexpr auto ThemeColorDefaults = std::to_array<ThemeColorDefault>({
    {"text-color", "text-primary", Color::from_rgb(37, 40, 46)},
    {"selected-text-color", "text-primary", Color::from_rgb(37, 40, 46)},
    {"caret-color", "text-primary", Color::from_rgb(37, 40, 46)},
    {"muted-text-color", "text-secondary", Color::from_rgb(96, 103, 114)},
    {"secondary-text-color", "text-secondary", Color::from_rgb(96, 103, 114)},
    {"placeholder-color", "text-secondary", Color::from_rgb(96, 103, 114)},
    {"disabled-text-color", "text-disabled", Color::from_rgb(145, 151, 161)},
    {"border-color", "border-subtle", Color::from_rgb(224, 228, 234)},
    {"separator-color", "border-subtle", Color::from_rgb(224, 228, 234)},
    {"row-separator-color", "border-subtle", Color::from_rgb(224, 228, 234)},
    {"popup-border-color", "border-subtle", Color::from_rgb(224, 228, 234)},
    {"focus-ring-color", "focus-ring", Color::from_rgb(76, 144, 228)},
    {"accent-color", "accent", Color::from_rgb(53, 132, 228)},
    {"selected-background", "accent-soft", Color::from_rgb(222, 236, 252)},
    {"inactive-selected-background", "selection-inactive-bg", Color::from_rgb(232, 236, 242)},
    {"inactive-selected-text-color", "selection-inactive-text", Color::from_rgb(37, 40, 46)},
    {"popup-selected-background", "accent-soft", Color::from_rgb(222, 236, 252)},
    {"hover-background", "surface-hover", Color::from_rgb(240, 243, 247)},
    {"popup-hover-background", "surface-hover", Color::from_rgb(240, 243, 247)},
    {"pressed-background", "surface-pressed", Color::from_rgb(232, 236, 242)},
    {"popup-background", "surface-card", Color::from_rgb(253, 253, 254)},
    {"scrollbar-track-color", "scrollbar-track", Color::from_rgb(226, 231, 238)},
    {"scrollbar-thumb-color", "scrollbar-thumb", Color::from_rgb(170, 180, 192)},
});

/// Look up the central fallback for a property. Returns nullptr for properties
/// that have no single default and must pass an explicit fallback.
[[nodiscard]] constexpr const Color* default_theme_color(std::string_view property_name) {
    for (const auto& entry : ThemeColorDefaults) {
        if (entry.property == property_name) {
            return &entry.value;
        }
    }
    return nullptr;
}

} // namespace nk
