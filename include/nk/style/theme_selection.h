#pragma once

/// @file theme_selection.h
/// @brief Theme family and selection policy for NodalKit visual resolution.

#include <memory>
#include <nk/platform/system_preferences.h>
#include <optional>

namespace nk {

class Theme;

/// High-level platform visual family.
enum class ThemeFamily {
    SystemDefault,
    LinuxGnome,
    Windows11,
    MacOS26,
};

/// Density preference exposed to applications and theme resolution.
enum class ThemeDensity {
    SystemDefault,
    Compact,
    Standard,
    Comfortable,
};

/// Motion policy override for theme resolution.
enum class MotionPolicy {
    FollowSystem,
    Normal,
    Reduced,
};

/// Transparency-effects policy override for theme resolution.
enum class TransparencyPolicy {
    FollowSystem,
    Allowed,
    Reduced,
};

/// Theme selection policy exposed to applications.
struct ThemeSelection {
    ThemeFamily family = ThemeFamily::SystemDefault;
    std::optional<ColorScheme> color_scheme_override;
    ThemeDensity density = ThemeDensity::SystemDefault;
    std::optional<Color> accent_color_override;
    bool force_high_contrast = false;
    MotionPolicy motion_policy = MotionPolicy::FollowSystem;
    TransparencyPolicy transparency_policy = TransparencyPolicy::FollowSystem;

    constexpr bool operator==(const ThemeSelection&) const = default;
};

/// Fully resolved visual policy after system preferences are applied.
struct ResolvedThemeSelection {
    ThemeFamily family = ThemeFamily::LinuxGnome;
    ColorScheme color_scheme = ColorScheme::Light;
    ThemeDensity density = ThemeDensity::Standard;
    std::optional<Color> accent_color;
    bool high_contrast = false;
    bool reduced_motion = false;
    bool transparency_allowed = true;

    constexpr bool operator==(const ResolvedThemeSelection&) const = default;
};

/// Resolve the final theme family and variant from application policy and
/// current system preferences.
[[nodiscard]] ResolvedThemeSelection
resolve_theme_selection(const ThemeSelection& selection,
                        const SystemPreferences& system_preferences);

/// Build a concrete theme for the resolved selection.
[[nodiscard]] std::shared_ptr<Theme> make_theme(const ResolvedThemeSelection& selection,
                                                const SystemPreferences& system_preferences);

/// Choose the default theme family for the current host platform.
[[nodiscard]] ThemeFamily default_theme_family_for(const SystemPreferences& system_preferences);

} // namespace nk
