#pragma once

/// @file showcase_profile.h
/// @brief Platform-aware profile data for the showcase example.

#include <nk/style/theme_selection.h>
#include <nk/widgets/menu_bar.h>

#include <string>
#include <vector>

enum class ShowcasePlatformFlavor {
    LinuxWayland,
    LinuxX11,
    MacOS,
    Windows,
    Generic,
};

struct ShowcaseProfile {
    ShowcasePlatformFlavor flavor = ShowcasePlatformFlavor::Generic;
    nk::ThemeFamily theme_family = nk::ThemeFamily::LinuxGnome;
    nk::ThemeDensity density = nk::ThemeDensity::Comfortable;
    nk::Color accent_color = nk::Color::from_rgb(38, 132, 131);
    std::string window_title = "NodalKit Showcase";
    std::string hero_title = "NodalKit Desktop Showcase";
    std::string hero_subtitle =
        "A compact workspace for inputs, model/view state, and live raster output.";
    std::string input_title = "Command Workspace";
    std::string input_subtitle =
        "Primary actions, short command entry, and accent selection.";
    std::string list_title = "List & Selection";
    std::string list_subtitle =
        "A bounded viewport keeps selection and insertion behavior visible.";
    std::string preview_title = "Preview Stage";
    std::string preview_subtitle =
        "Live raster output with deliberate surface priority and explicit scaling.";
    std::string actions_title = "Runtime Actions";
    std::string actions_subtitle =
        "Status-first runtime hooks for shared state and modal flow.";
    std::string platform_pill_text = "Generic";
    std::string ready_segment = "Ready";
    float window_width = 1180.0F;
    float window_height = 760.0F;
    float main_split_ratio = 0.44F;
    float preview_split_ratio = 0.74F;
    float preview_stage_min_height = 324.0F;
    float preview_stage_natural_height = 340.0F;
    float controls_spacing = 16.0F;
    float section_spacing = 18.0F;
};

ShowcasePlatformFlavor detect_showcase_platform_flavor(
    nk::SystemPreferences const& system_preferences);

ShowcaseProfile make_showcase_profile(nk::SystemPreferences const& system_preferences);

std::vector<nk::Menu> build_showcase_menus(ShowcaseProfile const& profile);
