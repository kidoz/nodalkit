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

/// Window backdrop capability of the host, from least to most capable.
/// `None` means no backdrop blending at all, `Opaque` a solid fallback fill,
/// and `Material` a system material such as Windows Mica/Acrylic or macOS
/// vibrancy.
enum class BackdropCapability {
    None,
    Opaque,
    Material,
};

/// Visual preferences discovered from the current platform.
struct SystemPreferences {
    PlatformFamily platform_family = PlatformFamily::Unknown;
    DesktopEnvironment desktop_environment = DesktopEnvironment::Unknown;
    ColorScheme color_scheme = ColorScheme::Light;
    ContrastPreference contrast = ContrastPreference::Normal;
    MotionPreference motion = MotionPreference::Normal;
    TransparencyPreference transparency = TransparencyPreference::Allowed;
    BackdropCapability backdrop = BackdropCapability::Opaque;
    float text_scale_factor = 1.0F;
    std::optional<Color> accent_color;
    // OS version, when known. Zero means "unknown" and callers should assume the
    // most modern behavior. On Windows, os_version_build distinguishes Windows 11
    // (build >= 22000) from Windows 10.
    int os_version_major = 0;
    int os_version_build = 0;

    constexpr bool operator==(const SystemPreferences&) const = default;
};

/// Convert a platform family to a stable lowercase token.
[[nodiscard]] std::string_view to_string(PlatformFamily family);

/// Convert a desktop environment hint to a stable lowercase token.
[[nodiscard]] std::string_view to_string(DesktopEnvironment desktop_environment);

} // namespace nk
