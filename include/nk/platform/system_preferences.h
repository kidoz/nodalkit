#pragma once

/// @file system_preferences.h
/// @brief Cross-platform system visual preference model for theme selection.

#include <nk/foundation/types.h>
#include <optional>
#include <string>
#include <string_view>

namespace nk {

/// Color scheme preference.
enum class ColorScheme { Light, Dark };

/// Host platform family used for visual policy selection.
enum class PlatformFamily {
    Unknown,
    Linux,
    Windows,
    MacOS,
};

/// Desktop environment hint for Linux and future shell adaptation.
enum class DesktopEnvironment {
    Unknown,
    Gnome,
    KDE,
    Other,
};

/// User contrast preference.
enum class ContrastPreference {
    Normal,
    High,
};

/// User motion preference.
enum class MotionPreference {
    Normal,
    Reduced,
};

/// Transparency-effects preference.
enum class TransparencyPreference {
    Allowed,
    Reduced,
};

/// Visual preferences discovered from the current platform.
struct SystemPreferences {
    PlatformFamily platform_family = PlatformFamily::Unknown;
    DesktopEnvironment desktop_environment = DesktopEnvironment::Unknown;
    ColorScheme color_scheme = ColorScheme::Light;
    ContrastPreference contrast = ContrastPreference::Normal;
    MotionPreference motion = MotionPreference::Normal;
    TransparencyPreference transparency = TransparencyPreference::Allowed;
    float text_scale_factor = 1.0F;
    std::optional<Color> accent_color;

    constexpr bool operator==(const SystemPreferences&) const = default;
};

/// Convert a platform family to a stable lowercase token.
[[nodiscard]] std::string_view to_string(PlatformFamily family);

/// Convert a desktop environment hint to a stable lowercase token.
[[nodiscard]] std::string_view to_string(DesktopEnvironment desktop_environment);

} // namespace nk
