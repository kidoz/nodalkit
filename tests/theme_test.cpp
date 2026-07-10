/// @file theme_test.cpp
/// @brief Regression tests for theme-family token sets and cascade resolution.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <nk/platform/system_preferences.h>
#include <nk/style/gtk_palette.h>
#include <nk/style/theme.h>
#include <nk/style/theme_defaults.h>
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

// Chase token aliases the way resolve_widget_rule does, so tests observe the
// same value a widget would.
const nk::StyleValue* deref_aliases(const nk::Theme& theme, const nk::StyleValue* value) {
    int depth = 0;
    while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
        value = theme.token(std::get<std::string>(*value));
        ++depth;
    }
    return value;
}

float resolved_metric(const nk::Theme& theme,
                      const std::vector<std::string>& classes,
                      std::string_view property) {
    const auto* value =
        deref_aliases(theme, theme.resolve("", classes, nk::StateFlags::None, property));
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<float>(*value));
    return std::get<float>(*value);
}

nk::Color resolved_color(const nk::Theme& theme,
                         const std::vector<std::string>& classes,
                         std::string_view property) {
    const auto* value =
        deref_aliases(theme, theme.resolve("", classes, nk::StateFlags::None, property));
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<nk::Color>(*value));
    return std::get<nk::Color>(*value);
}

nk::Color aliased_color_token(const nk::Theme& theme, std::string_view name) {
    const auto* value = deref_aliases(theme, theme.token(name));
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<nk::Color>(*value));
    return std::get<nk::Color>(*value);
}

// WCAG 2.2 relative luminance and contrast ratio.
float linear_channel(float component) {
    return component <= 0.04045F ? component / 12.92F
                                 : std::pow((component + 0.055F) / 1.055F, 2.4F);
}

float relative_luminance(nk::Color color) {
    return (0.2126F * linear_channel(color.r)) + (0.7152F * linear_channel(color.g)) +
           (0.0722F * linear_channel(color.b));
}

float contrast_ratio(nk::Color a, nk::Color b) {
    const float la = relative_luminance(a);
    const float lb = relative_luminance(b);
    const float lighter = std::max(la, lb);
    const float darker = std::min(la, lb);
    return (lighter + 0.05F) / (darker + 0.05F);
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

TEST_CASE("GTK palette parsing validates literals and honors later overrides",
          "[theme][gtk-palette]") {
    const auto palette = nk::parse_gtk_palette_css(R"css(
        @define-color window_bg_color #102030;
        @define-color window_bg_color rgb(64, 80, 96);
        @define-color window_fg_color rgba(240, 241, 242, 0.75);
        @define-color accent_bg_color #3584e4;
    )css");

    REQUIRE(palette.has_value());
    REQUIRE(palette->has_window_bg);
    REQUIRE(palette->has_window_fg);
    CHECK(palette->window_bg.r == Catch::Approx(64.0F / 255.0F));
    CHECK(palette->window_bg.g == Catch::Approx(80.0F / 255.0F));
    CHECK(palette->window_bg.b == Catch::Approx(96.0F / 255.0F));
    CHECK(palette->window_fg.a == Catch::Approx(0.75F));

    const auto invalid = nk::parse_gtk_palette_css(R"css(
        @define-color window_bg_color rgb(999, 0, 0);
        @define-color accent_bg_color #3584e4;
    )css");
    REQUIRE(invalid.has_value());
    CHECK_FALSE(invalid->has_window_bg);
    CHECK(invalid->has_accent_bg);
}

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

TEST_CASE("GNOME theme exposes Libadwaita semantic surfaces and adaptive metrics",
          "[theme][gnome]") {
    for (const auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        auto theme = nk::Theme::make_linux_gnome(scheme);
        for (const auto* token : {"window-fg",
                                  "view-bg",
                                  "view-fg",
                                  "headerbar-bg",
                                  "headerbar-fg",
                                  "headerbar-backdrop",
                                  "headerbar-border",
                                  "headerbar-shade",
                                  "sidebar-bg",
                                  "sidebar-fg",
                                  "sidebar-backdrop",
                                  "sidebar-border",
                                  "secondary-sidebar-bg",
                                  "secondary-sidebar-fg",
                                  "card-bg",
                                  "card-fg",
                                  "dialog-bg",
                                  "dialog-fg",
                                  "popover-bg",
                                  "popover-fg"}) {
            INFO("missing semantic token: " << token);
            REQUIRE(theme->token(token) != nullptr);
            REQUIRE(deref_aliases(*theme, theme->token(token)) != nullptr);
        }
        CHECK(metric_token(*theme, "headerbar-height") == Catch::Approx(46.0F));
        CHECK(metric_token(*theme, "headerbar-control-target") == Catch::Approx(46.0F));
        CHECK(metric_token(*theme, "clamp-maximum-size") == Catch::Approx(720.0F));
        CHECK(metric_token(*theme, "adaptive-sidebar-width") == Catch::Approx(280.0F));
    }
}

TEST_CASE("Toast overlay keeps page content foreground while its surface uses OSD colors",
          "[theme][toast]") {
    auto theme = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);

    REQUIRE(resolved_color(*theme, {"toast-overlay"}, "text-color") ==
            color_token(*theme, "text-primary"));
    REQUIRE(resolved_color(*theme, {"toast-surface"}, "text-color") ==
            color_token(*theme, "text-osd"));
}

TEST_CASE("GNOME theme publishes platform UI, document, and monospace font families",
          "[theme][gnome][fonts]") {
    nk::SystemPreferences preferences;
    preferences.platform_family = nk::PlatformFamily::Linux;
    preferences.desktop_environment = nk::DesktopEnvironment::Gnome;
    preferences.system_font_name = "Adwaita Sans 11";
    preferences.system_document_font_name = "Cantarell 12";
    preferences.system_monospace_font_name = "Adwaita Mono 11";

    nk::ThemeSelection selection;
    const auto resolved = nk::resolve_theme_selection(selection, preferences);
    auto theme = nk::make_theme(resolved, preferences);
    CHECK(string_token(*theme, "font-family-ui") == "Adwaita Sans 11");
    CHECK(string_token(*theme, "font-family-document") == "Cantarell 12");
    CHECK(string_token(*theme, "font-family-monospace") == "Adwaita Mono 11");
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

TEST_CASE("Family geometry personality lives in radius-role tokens", "[theme]") {
    // Control geometry is no longer duplicated in per-family override rules:
    // the shared rules alias radius-role tokens and each family sets its own
    // values, so rounded-vs-squared is a token decision.
    auto windows = nk::Theme::make_windows_11(nk::ColorScheme::Light);
    REQUIRE(resolved_metric(*windows, {"button"}, "corner-radius") == Catch::Approx(4.0F));
    REQUIRE(resolved_metric(*windows, {"button"}, "min-height") == Catch::Approx(32.0F));
    REQUIRE(resolved_metric(*windows, {"button"}, "min-width") == Catch::Approx(90.0F));
    REQUIRE(resolved_metric(*windows, {"button"}, "padding-x") == Catch::Approx(12.0F));

    auto macos = nk::Theme::make_macos_26(nk::ColorScheme::Light);
    REQUIRE(resolved_metric(*macos, {"button"}, "corner-radius") == Catch::Approx(6.0F));
    REQUIRE(resolved_metric(*macos, {"button"}, "min-height") == Catch::Approx(28.0F));
    REQUIRE(resolved_metric(*macos, {"combo-box"}, "popup-radius") == Catch::Approx(10.0F));

    auto gnome = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);
    REQUIRE(resolved_metric(*gnome, {"button"}, "corner-radius") == Catch::Approx(10.0F));
    REQUIRE(resolved_metric(*gnome, {"button"}, "min-width") == Catch::Approx(82.0F));

    // Widgets that previously had no per-family override rule now follow the
    // family automatically instead of cloning the GNOME geometry.
    REQUIRE(resolved_metric(*windows, {"list-view"}, "corner-radius") == Catch::Approx(8.0F));
    REQUIRE(resolved_metric(*windows, {"list-view"}, "selection-radius") == Catch::Approx(4.0F));
    REQUIRE(resolved_metric(*macos, {"list-view"}, "corner-radius") == Catch::Approx(10.0F));
    REQUIRE(resolved_metric(*macos, {"image-view"}, "content-radius") == Catch::Approx(8.0F));
}

TEST_CASE("Shared rules resolve through the radius roles and metric tokens", "[theme]") {
    auto gnome_light = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);
    auto gnome_dark = nk::Theme::make_linux_gnome(nk::ColorScheme::Dark);

    // The radius roles are a single source of truth carried by every family.
    REQUIRE(metric_token(*gnome_light, "radius-control") == Catch::Approx(10.0F));
    REQUIRE(metric_token(*gnome_light, "radius-card") == Catch::Approx(12.0F));
    REQUIRE(metric_token(*gnome_light, "radius-popup") == Catch::Approx(12.0F));
    REQUIRE(metric_token(*gnome_light, "radius-selection") == Catch::Approx(8.0F));
    REQUIRE(metric_token(*gnome_light, "radius-segment") == Catch::Approx(12.0F));
    REQUIRE(metric_token(*gnome_light, "radius-image") == Catch::Approx(14.0F));
    REQUIRE(metric_token(*gnome_light, "radius-image-content") == Catch::Approx(12.0F));

    REQUIRE(resolved_metric(*gnome_light, {"button"}, "corner-radius") == Catch::Approx(10.0F));
    REQUIRE(resolved_metric(*gnome_light, {"list-view"}, "corner-radius") == Catch::Approx(12.0F));
    REQUIRE(resolved_metric(*gnome_light, {"image-view"}, "corner-radius") == Catch::Approx(14.0F));

    // Spacing-shaped rule values resolve through named metric tokens instead of
    // magic literals.
    REQUIRE(resolved_metric(*gnome_light, {"button"}, "padding-x") == Catch::Approx(16.0F));
    REQUIRE(resolved_metric(*gnome_light, {"button"}, "padding-y") == Catch::Approx(9.0F));
    REQUIRE(resolved_metric(*gnome_light, {"card"}, "padding") == Catch::Approx(18.0F));
    REQUIRE(resolved_metric(*gnome_light, {"page"}, "padding") == Catch::Approx(28.0F));
    REQUIRE(resolved_metric(*gnome_light, {"status-bar"}, "segment-gap") == Catch::Approx(18.0F));
    REQUIRE(resolved_metric(*gnome_light, {"text-field"}, "min-width") == Catch::Approx(240.0F));
    REQUIRE(resolved_metric(*gnome_light, {"segmented-control"}, "separator-inset") ==
            Catch::Approx(8.0F));

    // Control min-heights come from the control-height token, so the dark
    // family (34 px) no longer renders the light family's 36 px control height.
    REQUIRE(resolved_metric(*gnome_light, {"button"}, "min-height") == Catch::Approx(36.0F));
    REQUIRE(resolved_metric(*gnome_dark, {"button"}, "min-height") == Catch::Approx(34.0F));
    REQUIRE(resolved_metric(*gnome_dark, {"text-field"}, "min-height") == Catch::Approx(34.0F));
}

TEST_CASE("Deprecated surface token names alias their canonical replacements", "[theme]") {
    // C1 migration: `surface-*` for surfaces, `border-*` for borders, `*-bg`
    // reserved for shell layers. The old names keep resolving via aliases.
    for (auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        for (const auto& theme : {nk::Theme::make_linux_gnome(scheme),
                                  nk::Theme::make_windows_11(scheme),
                                  nk::Theme::make_windows_10(scheme),
                                  nk::Theme::make_macos_26(scheme)}) {
            const auto canonical_field = color_token(*theme, "surface-field");
            const auto canonical_border = color_token(*theme, "border-field");
            REQUIRE(aliased_color_token(*theme, "field-bg") == canonical_field);
            REQUIRE(aliased_color_token(*theme, "field-border") == canonical_border);
            // The text-field rule consumes the canonical name.
            REQUIRE(resolved_color(*theme, {"text-field"}, "background") == canonical_field);
            REQUIRE(resolved_color(*theme, {"text-field"}, "border-color") == canonical_border);
        }
    }
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

TEST_CASE("Windows 10 fallback is a squared, opaque-accent family", "[theme][windows]") {
    auto win10 = nk::Theme::make_windows_10(nk::ColorScheme::Light);
    auto win11 = nk::Theme::make_windows_11(nk::ColorScheme::Light);

    REQUIRE(string_token(*win10, "theme-family") == "windows-10");

    // Windows 10 uses the classic #0078D7 accent, not the Windows 11 #0067C0.
    const auto accent = color_token(*win10, "accent");
    REQUIRE(channel(accent.r) == 0);
    REQUIRE(channel(accent.g) == 120);
    REQUIRE(channel(accent.b) == 215);

    // Near-square controls (2 px) versus the rounded Windows 11 family (4 px).
    REQUIRE(resolved_metric(*win10, {"button"}, "corner-radius") == Catch::Approx(2.0F));
    REQUIRE(resolved_metric(*win11, {"button"}, "corner-radius") == Catch::Approx(4.0F));

    // The shell layer vocabulary is shared with the Windows 11 family.
    REQUIRE(win10->token("layer-command-bg") != nullptr);
    REQUIRE(resolved_string(*win10, {"status-bar"}, "background") == "layer-status-bg");
}

TEST_CASE("OS build selects the Windows 11 or Windows 10 family", "[theme][windows]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Windows;

    SECTION("Windows 11 build") {
        prefs.os_version_build = 22631;
        REQUIRE(nk::default_theme_family_for(prefs) == nk::ThemeFamily::Windows11);
    }
    SECTION("Windows 10 build") {
        prefs.os_version_build = 19045;
        REQUIRE(nk::default_theme_family_for(prefs) == nk::ThemeFamily::Windows10);
    }
    SECTION("Unknown build assumes the modern family") {
        prefs.os_version_build = 0;
        REQUIRE(nk::default_theme_family_for(prefs) == nk::ThemeFamily::Windows11);
    }

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::SystemDefault;
    prefs.os_version_build = 19045;
    const auto resolved = nk::resolve_theme_selection(selection, prefs);
    REQUIRE(resolved.family == nk::ThemeFamily::Windows10);
    auto theme = nk::make_theme(resolved, prefs);
    REQUIRE(string_token(*theme, "theme-family") == "windows-10");
}

TEST_CASE("Backdrop capability resolves into a backdrop-mode token", "[theme]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Windows;
    prefs.os_version_build = 22631;

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::Windows11;

    SECTION("material-capable host with transparency allowed") {
        prefs.backdrop = nk::BackdropCapability::Material;
        prefs.transparency = nk::TransparencyPreference::Allowed;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.backdrop == nk::BackdropCapability::Material);
        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(string_token(*theme, "backdrop-mode") == "material");
    }

    SECTION("reduced transparency collapses material to opaque") {
        prefs.backdrop = nk::BackdropCapability::Material;
        prefs.transparency = nk::TransparencyPreference::Reduced;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.backdrop == nk::BackdropCapability::Opaque);
        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(string_token(*theme, "backdrop-mode") == "opaque");
    }

    SECTION("high contrast forces opaque even on a material host") {
        prefs.backdrop = nk::BackdropCapability::Material;
        selection.force_high_contrast = true;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.backdrop == nk::BackdropCapability::Opaque);
        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(string_token(*theme, "backdrop-mode") == "opaque");
    }

    SECTION("a host without backdrop support stays none") {
        prefs.backdrop = nk::BackdropCapability::None;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.backdrop == nk::BackdropCapability::None);
        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(string_token(*theme, "backdrop-mode") == "none");
    }
}

TEST_CASE("Text scale propagates into a token and control metrics", "[theme]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Windows;
    prefs.os_version_build = 22631;

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::Windows11;

    SECTION("125% scale grows control heights") {
        prefs.text_scale_factor = 1.25F;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.text_scale == Catch::Approx(1.25F));

        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(metric_token(*theme, "text-scale") == Catch::Approx(1.25F));
        // Windows 11 standard control height is 32 px.
        REQUIRE(metric_token(*theme, "control-height") == Catch::Approx(32.0F * 1.25F));
    }

    SECTION("default scale leaves metrics untouched") {
        prefs.text_scale_factor = 1.0F;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        auto theme = nk::make_theme(resolved, prefs);
        REQUIRE(metric_token(*theme, "text-scale") == Catch::Approx(1.0F));
        REQUIRE(metric_token(*theme, "control-height") == Catch::Approx(32.0F));
    }

    SECTION("scale is clamped to the Windows 225% ceiling") {
        prefs.text_scale_factor = 3.0F;
        const auto resolved = nk::resolve_theme_selection(selection, prefs);
        REQUIRE(resolved.text_scale == Catch::Approx(2.25F));
    }
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

TEST_CASE("Widget fallback defaults equal the light-theme tokens", "[theme]") {
    // The central fallback table (theme_defaults.h) is only a safety net; every
    // entry must stay byte-equal to the light-theme token it aliases so a
    // missing token degrades invisibly (consistency backlog C5).
    auto light = nk::Theme::make_light();
    for (const auto& entry : nk::ThemeColorDefaults) {
        INFO("fallback for property '" << entry.property << "' diverged from light token '"
                                       << entry.token << "'");
        REQUIRE(aliased_color_token(*light, entry.token) == entry.value);
    }
}

TEST_CASE("Every family defines the full token vocabulary", "[theme]") {
    static constexpr const char* CommonColorTokens[] = {
        "window-bg",
        "surface-panel",
        "surface-card",
        "surface-raised",
        "surface-hover",
        "surface-pressed",
        "surface-field",
        "border-field",
        "border-subtle",
        "border-strong",
        "text-primary",
        "text-secondary",
        "text-disabled",
        "accent",
        "accent-hover",
        "accent-pressed",
        "accent-soft",
        "accent-contrast",
        "focus-ring",
        "scrollbar-track",
        "scrollbar-thumb",
        "surface-osd",
        "text-osd",
        "field-bg",
        "field-border",
        "selection-active-bg",
        "selection-active-text",
        "selection-inactive-bg",
        "selection-inactive-text",
    };
    static constexpr const char* CommonMetricTokens[] = {
        "spacing-xs",
        "spacing-sm",
        "spacing-md",
        "spacing-lg",
        "spacing-xl",
        "control-height",
        "menu-height",
        "status-height",
        "radius-control",
        "radius-card",
        "radius-popup",
        "radius-selection",
        "radius-segment",
        "radius-image",
        "radius-image-content",
        "padding-control-x",
        "padding-control-y",
        "control-min-width",
        "segment-track-padding",
        "padding-card",
        "padding-page",
        "segment-gap",
        "field-min-width",
        "popup-item-height",
        "segment-min-width",
        "image-min-height",
        "font-size-title",
        "font-size-body",
        "font-size-caption",
        "font-size-value",
    };
    static constexpr const char* PolicyTokens[] = {
        "accent-source",
        "motion-mode",
        "transparency-mode",
        "density",
        "scrollbar-mode",
    };
    static constexpr const char* WindowsOnlyTokens[] = {
        "focus-visible",
        "layer-titlebar-bg",
        "layer-titlebar-text",
        "layer-titlebar-inactive-bg",
        "layer-titlebar-inactive-text",
        "layer-command-bg",
        "layer-navigation-bg",
        "layer-content-bg",
        "layer-status-bg",
    };

    struct Family {
        const char* name;
        std::unique_ptr<nk::Theme> theme;
        bool windows;
    };

    for (auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        Family families[] = {
            {"light/dark base",
             scheme == nk::ColorScheme::Dark ? nk::Theme::make_dark() : nk::Theme::make_light(),
             false},
            {"linux-gnome", nk::Theme::make_linux_gnome(scheme), false},
            {"windows-11", nk::Theme::make_windows_11(scheme), true},
            {"windows-10", nk::Theme::make_windows_10(scheme), true},
            {"macos-26", nk::Theme::make_macos_26(scheme), false},
        };
        for (const auto& family : families) {
            for (const auto* token : CommonColorTokens) {
                INFO(family.name << " is missing color token " << token);
                const auto* value = deref_aliases(*family.theme, family.theme->token(token));
                REQUIRE(value != nullptr);
                REQUIRE(std::holds_alternative<nk::Color>(*value));
            }
            for (const auto* token : CommonMetricTokens) {
                INFO(family.name << " is missing metric token " << token);
                const auto* value = family.theme->token(token);
                REQUIRE(value != nullptr);
                REQUIRE(std::holds_alternative<float>(*value));
            }
            for (const auto* token : PolicyTokens) {
                INFO(family.name << " is missing policy token " << token);
                REQUIRE(family.theme->token(token) != nullptr);
            }
            if (family.windows) {
                for (const auto* token : WindowsOnlyTokens) {
                    INFO(family.name << " is missing Windows token " << token);
                    REQUIRE(family.theme->token(token) != nullptr);
                }
            }
        }
    }
}

TEST_CASE("Text and focus colors meet WCAG 2.2 contrast in every palette", "[theme][contrast]") {
    static constexpr const char* RestingSurfaces[] = {
        "window-bg",
        "surface-panel",
        "surface-card",
        "surface-raised",
        "surface-field",
    };

    struct Palette {
        const char* name;
        std::unique_ptr<nk::Theme> theme;
    };

    for (auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        Palette palettes[] = {
            {"linux-gnome", nk::Theme::make_linux_gnome(scheme)},
            {"windows-11", nk::Theme::make_windows_11(scheme)},
            {"windows-10", nk::Theme::make_windows_10(scheme)},
            {"macos-26", nk::Theme::make_macos_26(scheme)},
        };
        for (const auto& palette : palettes) {
            const auto text_primary = color_token(*palette.theme, "text-primary");
            const auto text_secondary = color_token(*palette.theme, "text-secondary");
            const auto focus_ring = color_token(*palette.theme, "focus-ring");
            for (const auto* surface : RestingSurfaces) {
                const auto bg = aliased_color_token(*palette.theme, surface);
                INFO(palette.name << " text on " << surface);
                REQUIRE(contrast_ratio(text_primary, bg) >= 4.5F);
                REQUIRE(contrast_ratio(text_secondary, bg) >= 4.5F);
            }
            // Focus indicators are non-text UI: WCAG 2.2 requires >= 3:1.
            INFO(palette.name << " focus ring");
            REQUIRE(contrast_ratio(focus_ring, color_token(*palette.theme, "window-bg")) >= 3.0F);
            // Selection pairs: text on the active highlight matches the native
            // platform pair (macOS solid accent sits near the 3:1 UI-component
            // floor, like AppKit); the inactive pair is regular gray-on-gray
            // text and must hold the full 4.5:1.
            INFO(palette.name << " selection pairs");
            REQUIRE(contrast_ratio(aliased_color_token(*palette.theme, "selection-active-text"),
                                   aliased_color_token(*palette.theme, "selection-active-bg")) >=
                    3.0F);
            REQUIRE(contrast_ratio(aliased_color_token(*palette.theme, "selection-inactive-text"),
                                   aliased_color_token(*palette.theme, "selection-inactive-bg")) >=
                    4.5F);
            if (palette.theme->token("focus-visible") != nullptr) {
                REQUIRE(contrast_ratio(color_token(*palette.theme, "focus-visible"),
                                       color_token(*palette.theme, "window-bg")) >= 3.0F);
            }
        }

        // The high-contrast swap must hold the same guarantees with margin.
        nk::SystemPreferences prefs;
        prefs.platform_family = nk::PlatformFamily::Windows;
        prefs.color_scheme = scheme;
        nk::ThemeSelection selection;
        selection.family = nk::ThemeFamily::Windows11;
        selection.color_scheme_override = scheme;
        selection.force_high_contrast = true;
        auto hc = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        const auto hc_bg = color_token(*hc, "window-bg");
        REQUIRE(contrast_ratio(color_token(*hc, "text-primary"), hc_bg) >= 7.0F);
        REQUIRE(contrast_ratio(color_token(*hc, "text-secondary"), hc_bg) >= 7.0F);
        REQUIRE(contrast_ratio(color_token(*hc, "focus-ring"), hc_bg) >= 3.0F);
        REQUIRE(contrast_ratio(aliased_color_token(*hc, "surface-field"),
                               color_token(*hc, "text-primary")) >= 7.0F);
    }
}

TEST_CASE("Every resolved preference moves the theme it builds", "[theme][selection]") {
    nk::SystemPreferences prefs;
    prefs.platform_family = nk::PlatformFamily::Linux;

    nk::ThemeSelection selection;
    selection.family = nk::ThemeFamily::LinuxGnome;

    SECTION("color scheme swaps the palette") {
        selection.color_scheme_override = nk::ColorScheme::Light;
        auto light = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        selection.color_scheme_override = nk::ColorScheme::Dark;
        auto dark = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        REQUIRE(!(color_token(*light, "window-bg") == color_token(*dark, "window-bg")));
    }

    SECTION("density moves control metrics through the rules, not layout logic") {
        selection.density = nk::ThemeDensity::Compact;
        auto compact = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        REQUIRE(string_token(*compact, "density") == "compact");
        REQUIRE(metric_token(*compact, "control-height") == Catch::Approx(30.0F));
        // Rules alias the tokens, so the density change reaches resolved rule
        // values instead of going stale against a baked-in number.
        REQUIRE(resolved_metric(*compact, {"button"}, "min-height") == Catch::Approx(30.0F));
        REQUIRE(resolved_metric(*compact, {"segmented-control"}, "separator-inset") ==
                Catch::Approx(6.0F));

        selection.density = nk::ThemeDensity::Comfortable;
        auto comfy = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        REQUIRE(resolved_metric(*comfy, {"button"}, "min-height") == Catch::Approx(38.0F));
    }

    SECTION("text scale grows resolved control metrics") {
        prefs.text_scale_factor = 1.25F;
        auto scaled = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        REQUIRE(metric_token(*scaled, "text-scale") == Catch::Approx(1.25F));
        REQUIRE(resolved_metric(*scaled, {"button"}, "min-height") == Catch::Approx(36.0F * 1.25F));
        REQUIRE(resolved_metric(*scaled, {"menu-bar"}, "min-height") ==
                Catch::Approx(30.0F * 1.25F));
    }

    SECTION("reduced motion and transparency set their policy tokens") {
        selection.motion_policy = nk::MotionPolicy::Reduced;
        selection.transparency_policy = nk::TransparencyPolicy::Reduced;
        auto theme = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        REQUIRE(string_token(*theme, "motion-mode") == "reduced");
        REQUIRE(string_token(*theme, "transparency-mode") == "reduced");
    }

    SECTION("accent override recolors the accent ramp and resolved rules") {
        selection.accent_color_override = nk::Color::from_rgb(200, 40, 40);
        auto theme = nk::make_theme(nk::resolve_theme_selection(selection, prefs), prefs);
        REQUIRE(string_token(*theme, "accent-source") == "override");
        REQUIRE(color_token(*theme, "accent") == nk::Color::from_rgb(200, 40, 40));
        // Rules alias the accent token, so the override reaches suggested buttons.
        REQUIRE(resolved_color(*theme, {"button", "suggested"}, "background") ==
                nk::Color::from_rgb(200, 40, 40));
    }
}

TEST_CASE("No widget class leaks light-theme colors into the dark theme", "[theme]") {
    // Every widget style class must resolve its key color through the cascade
    // (class rule or base wildcard rule), so switching the scheme actually
    // changes what the widget draws. A class whose resolved color is identical
    // in both schemes is rendering a light-theme constant.
    struct ClassProbe {
        const char* style_class;
        const char* property;
    };

    static constexpr ClassProbe Probes[] = {
        {"avatar", "background"},
        {"badge", "background"},
        {"banner", "background"},
        {"breadcrumb", "link-color"},
        {"button", "background"},
        {"calendar", "background"},
        {"check-box", "background"},
        {"color-well", "border-color"},
        {"combo-box", "background"},
        {"command-palette", "background"},
        {"context-menu", "popup-background"},
        {"data-table", "background"},
        {"dialog", "dialog-background"},
        {"expander", "header-background"},
        {"grid-view", "background"},
        {"image-view", "background"},
        {"info-bar", "text-color"},
        {"label", "text-color"},
        {"list-view", "background"},
        {"menu-bar", "background"},
        {"popover", "background"},
        {"preferences-row", "background"},
        {"progress-bar", "track-color"},
        {"radio-button", "background"},
        {"scroll-area", "scrollbar-track-color"},
        {"search-field", "background"},
        {"segmented-control", "background"},
        {"separator", "color"},
        {"slider", "track-background"},
        {"spin-button", "background"},
        {"spinner", "color"},
        {"split-view", "divider-color"},
        {"status-bar", "background"},
        {"switch", "inactive-track-color"},
        {"tab-bar", "background"},
        {"text-area", "background"},
        {"text-field", "background"},
        {"toolbar", "background"},
        {"toast-overlay", "background"},
        {"tooltip", "background"},
        {"tree-view", "background"},
    };

    auto light = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);
    auto dark = nk::Theme::make_linux_gnome(nk::ColorScheme::Dark);
    for (const auto& probe : Probes) {
        INFO("style class '" << probe.style_class << "' property '" << probe.property
                             << "' resolves the same color in light and dark");
        const auto light_color = resolved_color(*light, {probe.style_class}, probe.property);
        const auto dark_color = resolved_color(*dark, {probe.style_class}, probe.property);
        REQUIRE(!(light_color == dark_color));
    }

    // The base wildcard rules cover generic properties for classes with no
    // dedicated rule block, and dim any disabled widget's text.
    const auto base_text = resolved_color(*dark, {"some-custom-widget"}, "text-color");
    REQUIRE(base_text == color_token(*dark, "text-primary"));
    const auto* disabled_text =
        dark->resolve("", {"some-custom-widget"}, nk::StateFlags::Disabled, "text-color");
    REQUIRE(disabled_text != nullptr);
    REQUIRE(std::holds_alternative<std::string>(*disabled_text));
    REQUIRE(std::get<std::string>(*disabled_text) == "text-disabled");
}

TEST_CASE("Selection, chevron, and scrollbar policy carry family personality",
          "[theme][selection]") {
    auto gnome = nk::Theme::make_linux_gnome(nk::ColorScheme::Light);
    auto win11 = nk::Theme::make_windows_11(nk::ColorScheme::Light);
    auto win10 = nk::Theme::make_windows_10(nk::ColorScheme::Light);
    auto macos = nk::Theme::make_macos_26(nk::ColorScheme::Light);

    SECTION("macOS active selection is the solid accent pair; others keep the soft wash") {
        REQUIRE(aliased_color_token(*macos, "selection-active-bg") ==
                color_token(*macos, "accent"));
        REQUIRE(aliased_color_token(*macos, "selection-active-text") ==
                color_token(*macos, "accent-contrast"));
        for (const auto* theme : {gnome.get(), win11.get(), win10.get()}) {
            REQUIRE(aliased_color_token(*theme, "selection-active-bg") ==
                    color_token(*theme, "accent-soft"));
            REQUIRE(aliased_color_token(*theme, "selection-active-text") ==
                    color_token(*theme, "text-primary"));
        }
    }

    SECTION("inactive-window selection collapses to the muted pair in every family") {
        for (const auto* theme : {gnome.get(), win11.get(), win10.get(), macos.get()}) {
            REQUIRE(aliased_color_token(*theme, "selection-inactive-bg") ==
                    color_token(*theme, "surface-pressed"));
            REQUIRE(aliased_color_token(*theme, "selection-inactive-text") ==
                    color_token(*theme, "text-primary"));
        }
    }

    SECTION("list rules expose both selection pairs to widgets") {
        for (const auto* theme : {gnome.get(), macos.get()}) {
            REQUIRE(resolved_color(*theme, {"list-view"}, "selected-background") ==
                    aliased_color_token(*theme, "selection-active-bg"));
            REQUIRE(resolved_color(*theme, {"list-view"}, "inactive-selected-background") ==
                    aliased_color_token(*theme, "selection-inactive-bg"));
        }
    }

    SECTION("the list focus ring is keyboard-only: FocusVisible resolves it, Focused does not") {
        const auto* visible_ring =
            gnome->resolve("",
                           {"list-view"},
                           nk::StateFlags::Focused | nk::StateFlags::FocusVisible,
                           "border-color");
        REQUIRE(visible_ring != nullptr);
        REQUIRE(std::holds_alternative<std::string>(*visible_ring));
        REQUIRE(std::get<std::string>(*visible_ring) == "focus-ring");

        const auto* pointer_ring =
            gnome->resolve("", {"list-view"}, nk::StateFlags::Focused, "border-color");
        REQUIRE(pointer_ring != nullptr);
        REQUIRE(std::holds_alternative<std::string>(*pointer_ring));
        REQUIRE(std::get<std::string>(*pointer_ring) == "border-subtle");
    }

    SECTION("macOS renders the combo chevron as a capsule; other families keep the divider") {
        REQUIRE(resolved_string(*macos, {"combo-box"}, "chevron-style") == "capsule");
        const auto* capsule_glyph =
            macos->resolve("", {"combo-box"}, nk::StateFlags::None, "chevron-color");
        REQUIRE(capsule_glyph != nullptr);
        REQUIRE(std::holds_alternative<std::string>(*capsule_glyph));
        REQUIRE(std::get<std::string>(*capsule_glyph) == "accent-contrast");
        for (const auto* theme : {gnome.get(), win11.get(), win10.get()}) {
            REQUIRE(resolved_string(*theme, {"combo-box"}, "chevron-style") == "divided");
        }
    }

    SECTION("scrollbar mode is overlay on GNOME/macOS and persistent on Windows") {
        REQUIRE(string_token(*gnome, "scrollbar-mode") == "overlay");
        REQUIRE(string_token(*macos, "scrollbar-mode") == "overlay");
        REQUIRE(string_token(*win11, "scrollbar-mode") == "persistent");
        REQUIRE(string_token(*win10, "scrollbar-mode") == "persistent");
    }
}
