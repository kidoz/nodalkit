#include <algorithm>
#include <nk/style/theme.h>
#include <nk/style/theme_selection.h>

namespace nk {

namespace {

float clamp_unit(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

Color lighten(Color color, float amount) {
    color.r = clamp_unit(color.r + ((1.0F - color.r) * amount));
    color.g = clamp_unit(color.g + ((1.0F - color.g) * amount));
    color.b = clamp_unit(color.b + ((1.0F - color.b) * amount));
    return color;
}

Color darken(Color color, float amount) {
    color.r = clamp_unit(color.r * (1.0F - amount));
    color.g = clamp_unit(color.g * (1.0F - amount));
    color.b = clamp_unit(color.b * (1.0F - amount));
    return color;
}

void apply_density(Theme& theme, ThemeDensity density) {
    switch (density) {
    case ThemeDensity::Compact:
        theme.set_token("spacing-sm", StyleValue{6.0F});
        theme.set_token("spacing-md", StyleValue{10.0F});
        theme.set_token("spacing-lg", StyleValue{14.0F});
        theme.set_token("spacing-xl", StyleValue{20.0F});
        theme.set_token("control-height", StyleValue{30.0F});
        theme.set_token("menu-height", StyleValue{30.0F});
        theme.set_token("status-height", StyleValue{26.0F});
        theme.set_token("density", StyleValue{std::string("compact")});
        break;
    case ThemeDensity::Comfortable:
        theme.set_token("spacing-sm", StyleValue{10.0F});
        theme.set_token("spacing-md", StyleValue{14.0F});
        theme.set_token("spacing-lg", StyleValue{18.0F});
        theme.set_token("spacing-xl", StyleValue{28.0F});
        theme.set_token("control-height", StyleValue{38.0F});
        theme.set_token("menu-height", StyleValue{34.0F});
        theme.set_token("status-height", StyleValue{30.0F});
        theme.set_token("density", StyleValue{std::string("comfortable")});
        break;
    case ThemeDensity::Standard:
    case ThemeDensity::SystemDefault:
    default:
        theme.set_token("density", StyleValue{std::string("standard")});
        break;
    }
}

void apply_accent_override(Theme& theme,
                           ColorScheme color_scheme,
                           const std::optional<Color>& accent_color) {
    if (!accent_color.has_value()) {
        return;
    }

    auto accent = *accent_color;
    auto hover = color_scheme == ColorScheme::Dark ? lighten(accent, 0.12F) : darken(accent, 0.08F);
    auto pressed =
        color_scheme == ColorScheme::Dark ? darken(accent, 0.10F) : darken(accent, 0.18F);
    auto soft = color_scheme == ColorScheme::Dark ? lighten(accent, 0.10F) : lighten(accent, 0.72F);

    theme.set_token("accent", StyleValue{accent});
    theme.set_token("accent-hover", StyleValue{hover});
    theme.set_token("accent-pressed", StyleValue{pressed});
    theme.set_token("accent-soft", StyleValue{soft});
    theme.set_token("accent-source", StyleValue{std::string("override")});
}

} // namespace

ThemeFamily default_theme_family_for(const SystemPreferences& system_preferences) {
    switch (system_preferences.platform_family) {
    case PlatformFamily::Windows:
        return ThemeFamily::Windows11;
    case PlatformFamily::MacOS:
        return ThemeFamily::MacOS26;
    case PlatformFamily::Linux:
    case PlatformFamily::Unknown:
    default:
        return ThemeFamily::LinuxGnome;
    }
}

ResolvedThemeSelection resolve_theme_selection(const ThemeSelection& selection,
                                               const SystemPreferences& system_preferences) {
    ResolvedThemeSelection resolved;
    resolved.family = selection.family == ThemeFamily::SystemDefault
                          ? default_theme_family_for(system_preferences)
                          : selection.family;
    resolved.color_scheme =
        selection.color_scheme_override.value_or(system_preferences.color_scheme);
    resolved.density = selection.density == ThemeDensity::SystemDefault ? ThemeDensity::Standard
                                                                        : selection.density;
    resolved.accent_color = selection.accent_color_override.has_value()
                                ? selection.accent_color_override
                                : system_preferences.accent_color;
    resolved.high_contrast =
        selection.force_high_contrast || system_preferences.contrast == ContrastPreference::High;

    switch (selection.motion_policy) {
    case MotionPolicy::Reduced:
        resolved.reduced_motion = true;
        break;
    case MotionPolicy::Normal:
        resolved.reduced_motion = false;
        break;
    case MotionPolicy::FollowSystem:
    default:
        resolved.reduced_motion = system_preferences.motion == MotionPreference::Reduced;
        break;
    }

    switch (selection.transparency_policy) {
    case TransparencyPolicy::Reduced:
        resolved.transparency_allowed = false;
        break;
    case TransparencyPolicy::Allowed:
        resolved.transparency_allowed = true;
        break;
    case TransparencyPolicy::FollowSystem:
    default:
        resolved.transparency_allowed =
            system_preferences.transparency == TransparencyPreference::Allowed;
        break;
    }

    return resolved;
}

std::shared_ptr<Theme> make_theme(const ResolvedThemeSelection& selection,
                                  const SystemPreferences& /*system_preferences*/) {
    std::unique_ptr<Theme> theme;

    switch (selection.family) {
    case ThemeFamily::LinuxGnome:
        theme = Theme::make_linux_gnome(selection.color_scheme);
        break;
    case ThemeFamily::Windows11:
        theme = Theme::make_windows_11(selection.color_scheme);
        break;
    case ThemeFamily::MacOS26:
        theme = Theme::make_macos_26(selection.color_scheme);
        break;
    case ThemeFamily::SystemDefault:
    default:
        theme = Theme::make_linux_gnome(selection.color_scheme);
        break;
    }

    if (selection.high_contrast) {
        theme->set_token("contrast-mode", StyleValue{std::string("high")});
    }
    if (!selection.transparency_allowed) {
        theme->set_token("transparency-mode", StyleValue{std::string("reduced")});
    }
    if (selection.reduced_motion) {
        theme->set_token("motion-mode", StyleValue{std::string("reduced")});
    }

    apply_density(*theme, selection.density);
    apply_accent_override(*theme, selection.color_scheme, selection.accent_color);

    return std::shared_ptr<Theme>(theme.release());
}

} // namespace nk
