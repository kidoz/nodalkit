/// @file theme_test.cpp
/// @brief Regression tests for theme-family token sets and cascade resolution.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <nk/platform/system_preferences.h>
#include <nk/style/theme.h>
#include <nk/style/theme_selection.h>
#include <string>
#include <variant>
#include <vector>

namespace {

nk::Color color_token(const nk::Theme& theme, std::string_view name) {
    const auto* value = theme.token(name);
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<nk::Color>(*value));
    return std::get<nk::Color>(*value);
}

float metric_token(const nk::Theme& theme, std::string_view name) {
    const auto* value = theme.token(name);
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<float>(*value));
    return std::get<float>(*value);
}

std::string string_token(const nk::Theme& theme, std::string_view name) {
    const auto* value = theme.token(name);
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<std::string>(*value));
    return std::get<std::string>(*value);
}

int channel(float component) {
    return static_cast<int>(std::lround(component * 255.0F));
}

float resolved_metric(const nk::Theme& theme,
                      const std::vector<std::string>& classes,
                      std::string_view property) {
    const auto* value = theme.resolve("", classes, nk::StateFlags::None, property);
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<float>(*value));
    return std::get<float>(*value);
}

std::string resolved_string(const nk::Theme& theme,
                            const std::vector<std::string>& classes,
                            std::string_view property) {
    const auto* value = theme.resolve("", classes, nk::StateFlags::None, property);
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<std::string>(*value));
    return std::get<std::string>(*value);
}

} // namespace

TEST_CASE("Windows 11 theme is a native family, not a GNOME clone", "[theme][windows]") {
    auto windows = nk::Theme::make_windows_11(nk::ColorScheme::Light);
    auto gnome = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);

    REQUIRE(string_token(*windows, "theme-family") == "windows-11");
    REQUIRE(string_token(*gnome, "theme-family") == "linux-gnome");

    // Native Windows surfaces and control metrics diverge from the Linux defaults.
    const auto windows_bg = color_token(*windows, "window-bg");
    const auto gnome_bg = color_token(*gnome, "window-bg");
    REQUIRE(channel(windows_bg.r) == 243);
    REQUIRE(channel(windows_bg.g) == 243);
    REQUIRE(channel(windows_bg.b) == 243);
    REQUIRE(channel(gnome_bg.r) != 243);

    REQUIRE(metric_token(*windows, "control-height") == Catch::Approx(32.0F));
    REQUIRE(metric_token(*gnome, "control-height") == Catch::Approx(36.0F));
}

TEST_CASE("Windows 11 theme exposes shell layer-role tokens", "[theme][windows]") {
    auto windows = nk::Theme::make_windows_11(nk::ColorScheme::Light);

    for (const auto* token : {"layer-titlebar-bg",
                              "layer-titlebar-text",
                              "layer-titlebar-inactive-bg",
                              "layer-titlebar-inactive-text",
                              "layer-command-bg",
                              "layer-navigation-bg",
                              "layer-content-bg",
                              "layer-status-bg"}) {
        INFO("missing layer token: " << token);
        REQUIRE(windows->token(token) != nullptr);
    }

    // The command and status surfaces resolve to their layer-role tokens.
    REQUIRE(resolved_string(*windows, {"menu-bar"}, "background") == "layer-command-bg");
    REQUIRE(resolved_string(*windows, {"status-bar"}, "background") == "layer-status-bg");
}

TEST_CASE("Windows 11 theme exposes a strong focus-visible token", "[theme][windows]") {
    auto windows = nk::Theme::make_windows_11(nk::ColorScheme::Light);

    const auto focus_visible = color_token(*windows, "focus-visible");
    const auto focus_ring = color_token(*windows, "focus-ring");

    // Windows keyboard focus draws a strong near-black outline distinct from the
    // accent-colored focus ring used for pointer affordances.
    REQUIRE(channel(focus_visible.r) == 26);
    const bool differs = channel(focus_visible.r) != channel(focus_ring.r) ||
                         channel(focus_visible.g) != channel(focus_ring.g) ||
                         channel(focus_visible.b) != channel(focus_ring.b);
    REQUIRE(differs);
}

TEST_CASE("Windows 11 dark theme uses dark-on-light accent contrast", "[theme][windows]") {
    auto light = nk::Theme::make_windows_11(nk::ColorScheme::Light);
    auto dark = nk::Theme::make_windows_11(nk::ColorScheme::Dark);

    REQUIRE(string_token(*dark, "theme-family") == "windows-11");

    const auto light_text = color_token(*light, "text-primary");
    const auto dark_text = color_token(*dark, "text-primary");
    REQUIRE(channel(light_text.r) < 64);
    REQUIRE(channel(dark_text.r) > 192);

    // The light dark-mode accent (#60CDFF) takes black foreground text, unlike the
    // white contrast used by the light theme's deep-blue accent.
    const auto dark_accent_contrast = color_token(*dark, "accent-contrast");
    REQUIRE(channel(dark_accent_contrast.r) == 0);
    const auto light_accent_contrast = color_token(*light, "accent-contrast");
    REQUIRE(channel(light_accent_contrast.r) == 255);
}

TEST_CASE("Per-family override rules win the cascade tie-break", "[theme]") {
    // Shared rules give buttons a 10 px corner radius; the Windows and macOS
    // override rules are installed afterwards with equal specificity, so the
    // source-order tie-break must let them win.
    auto windows = nk::Theme::make_windows_11(nk::ColorScheme::Light);
    REQUIRE(resolved_metric(*windows, {"button"}, "corner-radius") == Catch::Approx(4.0F));
    REQUIRE(resolved_metric(*windows, {"button"}, "min-height") == Catch::Approx(32.0F));

    auto macos = nk::Theme::make_macos_26(nk::ColorScheme::Light);
    REQUIRE(resolved_metric(*macos, {"button"}, "corner-radius") == Catch::Approx(6.0F));

    // A family without overrides keeps the shared radius.
    auto gnome = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);
    REQUIRE(resolved_metric(*gnome, {"button"}, "corner-radius") == Catch::Approx(10.0F));
}

TEST_CASE("High contrast swaps the palette to maximal-contrast system colors", "[theme]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Windows;

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::Windows11;
    selection.force_high_contrast = true;

    SECTION("dark scheme yields High Contrast Black") {
        prefs.color_scheme = nk::ColorScheme::Dark;
        selection.color_scheme_override = nk::ColorScheme::Dark;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.high_contrast);

        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(string_token(*theme, "contrast-mode") == "high");

        const auto bg = color_token(*theme, "window-bg");
        const auto text = color_token(*theme, "text-primary");
        REQUIRE(channel(bg.r) == 0);
        REQUIRE(channel(bg.g) == 0);
        REQUIRE(channel(bg.b) == 0);
        REQUIRE(channel(text.r) == 255);
        REQUIRE(channel(text.g) == 255);
        REQUIRE(channel(text.b) == 255);
        // Borders must stay visible against the black background.
        REQUIRE(channel(color_token(*theme, "border-subtle").r) == 255);
        // Cyan highlight with black foreground.
        REQUIRE(channel(color_token(*theme, "accent").b) == 255);
        REQUIRE(channel(color_token(*theme, "accent-contrast").r) == 0);
    }

    SECTION("light scheme yields High Contrast White") {
        prefs.color_scheme = nk::ColorScheme::Light;
        selection.color_scheme_override = nk::ColorScheme::Light;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);

        auto theme = nk::make_theme(resolved, prefs);
        const auto bg = color_token(*theme, "window-bg");
        const auto text = color_token(*theme, "text-primary");
        REQUIRE(channel(bg.r) == 255);
        REQUIRE(channel(text.r) == 0);
        REQUIRE(channel(color_token(*theme, "border-subtle").r) == 0);
        REQUIRE(channel(color_token(*theme, "accent-contrast").r) == 255);
    }
}

TEST_CASE("High contrast ignores a custom accent override", "[theme]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Windows;
    prefs.color_scheme = nk::ColorScheme::Dark;

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::Windows11;
    selection.color_scheme_override = nk::ColorScheme::Dark;
    selection.force_high_contrast = true;
    selection.accent_color_override = nk::Color::from_rgb(255, 0, 0);

    const auto resolved = nk::resolve_theme_selection(selection, prefs);
    auto theme = nk::make_theme(resolved, prefs);

    // The system high-contrast highlight must win over the red override.
    const auto accent = color_token(*theme, "accent");
    REQUIRE(channel(accent.r) == 38);
    REQUIRE(channel(accent.b) == 255);
    REQUIRE(string_token(*theme, "accent-source") == "theme");
}

TEST_CASE("make_theme builds the Windows family with density applied", "[theme][windows]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Windows;
    prefs.color_scheme = nk::ColorScheme::Dark;

    REQUIRE(nk::default_theme_family_for(prefs) == nk::ThemeFamily::Windows11);

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::SystemDefault;
    selection.density = nk::ThemeDensity::Compact;

    const auto resolved = nk::resolve_theme_selection(selection, prefs);
    REQUIRE(resolved.family == nk::ThemeFamily::Windows11);
    REQUIRE(resolved.color_scheme == nk::ColorScheme::Dark);

    auto theme = nk::make_theme(resolved, prefs);
    REQUIRE(theme != nullptr);
    REQUIRE(string_token(*theme, "theme-family") == "windows-11");
    REQUIRE(string_token(*theme, "density") == "compact");
    REQUIRE(metric_token(*theme, "control-height") == Catch::Approx(30.0F));
}
