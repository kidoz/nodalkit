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

enum class ShowcasePreviewLayoutMode {
    Split,
    Stacked,
};

enum class ShowcaseActionsLayoutMode {
    Expanded,
    Compact,
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
    std::string palette_label = "Accent preset";
    std::string palette_value_prefix = "Selected: ";
    ShowcasePreviewLayoutMode preview_layout_mode = ShowcasePreviewLayoutMode::Split;
    ShowcaseActionsLayoutMode actions_layout_mode = ShowcaseActionsLayoutMode::Expanded;
    bool show_hero_close_button = false;
    float window_width = 1180.0F;
    float window_height = 760.0F;
    float main_split_ratio = 0.44F;
    float preview_split_ratio = 0.74F;
    float main_column_spacing = 24.0F;
    float preview_section_spacing = 16.0F;
    float actions_row_spacing = 18.0F;
    float page_padding_top = 16.0F;
    float page_padding_right = 16.0F;
    float page_padding_bottom = 16.0F;
    float page_padding_left = 16.0F;
    float scrollbar_safe_gutter = 0.0F;
    float page_bottom_spacer_height = 34.0F;
    float hero_min_height = 120.0F;
    float hero_natural_height = 132.0F;
    float command_stage_min_height = 92.0F;
    float command_stage_natural_height = 96.0F;
    float command_stage_padding = 14.0F;
    float counter_stage_min_height = 122.0F;
    float counter_stage_natural_height = 126.0F;
    float counter_stage_padding = 14.0F;
    float palette_stage_min_height = 96.0F;
    float palette_stage_natural_height = 100.0F;
    float palette_stage_padding = 14.0F;
    float list_stage_min_height = 204.0F;
    float list_stage_natural_height = 208.0F;
    float list_stage_padding = 4.0F;
    float preview_stage_min_height = 324.0F;
    float preview_stage_natural_height = 340.0F;
    float preview_stage_padding = 4.0F;
    float runtime_status_min_height = 108.0F;
    float runtime_status_natural_height = 118.0F;
    float runtime_status_padding = 16.0F;
    float runtime_property_min_height = 124.0F;
    float runtime_property_natural_height = 132.0F;
    float runtime_property_padding = 16.0F;
    float runtime_dialog_min_height = 124.0F;
    float runtime_dialog_natural_height = 132.0F;
    float runtime_dialog_padding = 16.0F;
    float controls_spacing = 16.0F;
    float section_spacing = 18.0F;
};

ShowcasePlatformFlavor detect_showcase_platform_flavor(
    nk::SystemPreferences const& system_preferences);

ShowcaseProfile make_showcase_profile(nk::SystemPreferences const& system_preferences);

std::vector<nk::Menu> build_showcase_menus(ShowcaseProfile const& profile);
