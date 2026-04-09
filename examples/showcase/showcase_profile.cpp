/// @file showcase_profile.cpp
/// @brief Platform-aware profile data for the showcase example.

#include "showcase_profile.h"

#include <cstdlib>
#include <string_view>

ShowcasePlatformFlavor
detect_showcase_platform_flavor(const nk::SystemPreferences& system_preferences) {
    switch (system_preferences.platform_family) {
    case nk::PlatformFamily::MacOS:
        return ShowcasePlatformFlavor::MacOS;
    case nk::PlatformFamily::Windows:
        return ShowcasePlatformFlavor::Windows;
    case nk::PlatformFamily::Linux: {
        const char* session_type = std::getenv("XDG_SESSION_TYPE");
        if (session_type != nullptr) {
            const auto value = std::string_view(session_type);
            if (value == "x11") {
                return ShowcasePlatformFlavor::LinuxX11;
            }
        }
        return ShowcasePlatformFlavor::LinuxWayland;
    }
    case nk::PlatformFamily::Unknown:
        break;
    }

    return ShowcasePlatformFlavor::Generic;
}

ShowcaseProfile make_showcase_profile(const nk::SystemPreferences& system_preferences) {
    ShowcaseProfile profile;
    profile.theme_family = nk::default_theme_family_for(system_preferences);

    switch (detect_showcase_platform_flavor(system_preferences)) {
    case ShowcasePlatformFlavor::LinuxWayland:
        profile.flavor = ShowcasePlatformFlavor::LinuxWayland;
        profile.theme_family = nk::ThemeFamily::LinuxGnome;
        profile.density = nk::ThemeDensity::Comfortable;
        profile.accent_color = nk::Color::from_rgb(38, 132, 131);
        profile.window_title = "NodalKit Showcase";
        profile.hero_title = "NodalKit Desktop Showcase";
        profile.hero_subtitle =
            "A compact GNOME-first workspace for input, model/view state, and portal-friendly "
            "raster output.";
        profile.input_title = "Command Workspace";
        profile.input_subtitle =
            "Primary actions, short command entry, and a preview palette for the Wayland path.";
        profile.list_title = "List & Selection";
        profile.list_subtitle =
            "A bounded viewport keeps keyboard selection, insertion, and resize behavior visible.";
        profile.preview_title = "Preview Stage";
        profile.preview_subtitle =
            "Live raster output with explicit scaling, compact metadata, and a Linux-first stage.";
        profile.actions_title = "Runtime Actions";
        profile.actions_subtitle =
            "Status-first hooks for shared state, dialogs, and Wayland-era desktop flow.";
        profile.platform_pill_text = "Wayland";
        profile.ready_segment = "Wayland ready";
        profile.palette_label = "Preview palette";
        profile.palette_value_prefix = "Palette: ";
        profile.preview_layout_mode = ShowcasePreviewLayoutMode::Stacked;
        profile.actions_layout_mode = ShowcaseActionsLayoutMode::Compact;
        profile.show_hero_close_button = false;
        profile.window_width = 1240.0F;
        profile.window_height = 860.0F;
        profile.main_split_ratio = 0.42F;
        profile.preview_split_ratio = 0.62F;
        profile.main_column_spacing = 20.0F;
        profile.preview_section_spacing = 12.0F;
        profile.actions_row_spacing = 12.0F;
        profile.page_padding_top = 12.0F;
        profile.page_padding_right = 36.0F;
        profile.page_padding_bottom = 4.0F;
        profile.page_padding_left = 24.0F;
        profile.scrollbar_safe_gutter = 12.0F;
        profile.page_bottom_spacer_height = 0.0F;
        profile.hero_min_height = 104.0F;
        profile.hero_natural_height = 112.0F;
        profile.command_stage_min_height = 82.0F;
        profile.command_stage_natural_height = 86.0F;
        profile.counter_stage_min_height = 102.0F;
        profile.counter_stage_natural_height = 106.0F;
        profile.palette_stage_min_height = 78.0F;
        profile.palette_stage_natural_height = 82.0F;
        profile.list_stage_min_height = 148.0F;
        profile.list_stage_natural_height = 152.0F;
        profile.preview_stage_min_height = 252.0F;
        profile.preview_stage_natural_height = 268.0F;
        profile.preview_stage_padding = 8.0F;
        profile.runtime_status_min_height = 74.0F;
        profile.runtime_status_natural_height = 78.0F;
        profile.runtime_property_min_height = 62.0F;
        profile.runtime_property_natural_height = 66.0F;
        profile.runtime_dialog_min_height = 62.0F;
        profile.runtime_dialog_natural_height = 66.0F;
        profile.controls_spacing = 16.0F;
        profile.section_spacing = 20.0F;
        break;
    case ShowcasePlatformFlavor::LinuxX11:
        profile.flavor = ShowcasePlatformFlavor::LinuxX11;
        profile.theme_family = nk::ThemeFamily::LinuxGnome;
        profile.density = nk::ThemeDensity::Standard;
        profile.accent_color = nk::Color::from_rgb(57, 115, 196);
        profile.window_title = "NodalKit Showcase";
        profile.hero_title = "NodalKit Linux Showcase";
        profile.hero_subtitle =
            "A shared showcase shell tuned for a denser X11-style desktop presentation.";
        profile.input_title = "Command Workspace";
        profile.input_subtitle =
            "Primary actions and short-form controls in a denser Linux profile.";
        profile.list_title = "List & Selection";
        profile.list_subtitle =
            "Selection, insertion, and viewport behavior remain visible in the compact shell.";
        profile.preview_title = "Preview Stage";
        profile.preview_subtitle =
            "Live raster output with explicit scaling in the Linux compatibility profile.";
        profile.actions_title = "Runtime Actions";
        profile.actions_subtitle = "Shared runtime hooks and modal flow in a more compact shell.";
        profile.platform_pill_text = "X11";
        profile.ready_segment = "Linux ready";
        profile.show_hero_close_button = false;
        profile.window_width = 1200.0F;
        profile.window_height = 820.0F;
        profile.main_split_ratio = 0.45F;
        profile.preview_split_ratio = 0.68F;
        profile.main_column_spacing = 22.0F;
        profile.preview_stage_min_height = 316.0F;
        profile.preview_stage_natural_height = 336.0F;
        profile.controls_spacing = 16.0F;
        profile.section_spacing = 18.0F;
        break;
    case ShowcasePlatformFlavor::MacOS:
        profile.flavor = ShowcasePlatformFlavor::MacOS;
        profile.theme_family = nk::ThemeFamily::MacOS26;
        profile.density = nk::ThemeDensity::Standard;
        profile.accent_color = nk::Color::from_rgb(18, 116, 191);
        profile.window_title = "NodalKit Showcase";
        profile.hero_title = "NodalKit macOS Showcase";
        profile.hero_subtitle =
            "A calmer desktop shell for command flow, preview surfaces, and live runtime state.";
        profile.input_title = "Command Studio";
        profile.input_subtitle =
            "Primary actions, quick entry, and selection controls with a roomier shell.";
        profile.list_title = "List & Selection";
        profile.list_subtitle =
            "A document-style viewport keeps selection and insertion behavior visible.";
        profile.preview_title = "Preview Stage";
        profile.preview_subtitle =
            "Live raster output with a more spacious inspection column and explicit scaling.";
        profile.actions_title = "Runtime Actions";
        profile.actions_subtitle =
            "Status-first runtime hooks for shared state, dialogs, and desktop flow.";
        profile.platform_pill_text = "macOS";
        profile.ready_segment = "macOS ready";
        profile.show_hero_close_button = false;
        profile.window_width = 1280.0F;
        profile.window_height = 1020.0F;
        profile.main_split_ratio = 0.45F;
        profile.preview_split_ratio = 0.61F;
        profile.main_column_spacing = 24.0F;
        profile.preview_section_spacing = 16.0F;
        profile.preview_stage_min_height = 336.0F;
        profile.preview_stage_natural_height = 360.0F;
        profile.controls_spacing = 18.0F;
        profile.section_spacing = 20.0F;
        break;
    case ShowcasePlatformFlavor::Windows:
        profile.flavor = ShowcasePlatformFlavor::Windows;
        profile.theme_family = nk::ThemeFamily::Windows11;
        profile.density = nk::ThemeDensity::Standard;
        profile.accent_color = nk::Color::from_rgb(0, 95, 184);
        profile.window_title = "NodalKit Showcase";
        profile.hero_title = "NodalKit Windows Showcase";
        profile.hero_subtitle =
            "A Windows shell for commands, preview, and live runtime status.";
        profile.input_title = "Command Workspace";
        profile.input_subtitle =
            "Primary actions, command entry, and preset controls in a Windows shell.";
        profile.list_title = "List & Selection";
        profile.list_subtitle = "A bounded list viewport keeps keyboard selection visible.";
        profile.preview_title = "Preview Stage";
        profile.preview_subtitle =
            "Live raster output with explicit scaling and compact metadata.";
        profile.actions_title = "Runtime Actions";
        profile.actions_subtitle =
            "Shared runtime hooks for status changes, state, and dialogs.";
        profile.platform_pill_text = "Windows";
        profile.ready_segment = "Windows ready";
        profile.preview_layout_mode = ShowcasePreviewLayoutMode::Stacked;
        profile.show_hero_close_button = false;
        profile.window_width = 1320.0F;
        profile.window_height = 860.0F;
        profile.main_split_ratio = 0.45F;
        profile.preview_split_ratio = 0.62F;
        profile.main_column_spacing = 24.0F;
        profile.preview_section_spacing = 14.0F;
        profile.scrollbar_safe_gutter = 18.0F;
        profile.preview_stage_min_height = 330.0F;
        profile.preview_stage_natural_height = 350.0F;
        profile.controls_spacing = 18.0F;
        profile.section_spacing = 20.0F;
        break;
    case ShowcasePlatformFlavor::Generic:
        profile.flavor = ShowcasePlatformFlavor::Generic;
        profile.platform_pill_text = "Generic";
        profile.ready_segment = "Ready";
        break;
    }

    return profile;
}

std::vector<nk::Menu> build_showcase_menus(const ShowcaseProfile& profile) {
    using nk::KeyCode;
    using nk::NativeMenuModifier;
    using nk::NativeMenuShortcut;

    switch (profile.flavor) {
    case ShowcasePlatformFlavor::MacOS:
        return {
            {"NodalKit Showcase",
             {
                 nk::MenuItem::action("About NodalKit Showcase", "help.about"),
                 nk::MenuItem::action("Preferences...",
                                      "app.preferences",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Comma,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action("Quit NodalKit Showcase",
                                      "file.quit",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Q,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
             }},
            {"File",
             {
                 nk::MenuItem::action("New Workspace", "file.new"),
                 nk::MenuItem::action("Open...",
                                      "file.open",
                                      NativeMenuShortcut{
                                          .key = KeyCode::O,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action(
                     "Export Diagnostics Bundle",
                     "debug.export_bundle",
                     NativeMenuShortcut{
                         .key = KeyCode::E,
                         .modifiers = NativeMenuModifier::Super | NativeMenuModifier::Shift,
                     }),
             }},
            {"Edit",
             {
                 nk::MenuItem::action("Undo",
                                      "edit.undo",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Z,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
                 nk::MenuItem::action(
                     "Redo",
                     "edit.redo",
                     NativeMenuShortcut{
                         .key = KeyCode::Z,
                         .modifiers = NativeMenuModifier::Super | NativeMenuModifier::Shift,
                     }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action("Cut",
                                      "edit.cut",
                                      NativeMenuShortcut{
                                          .key = KeyCode::X,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
                 nk::MenuItem::action("Copy",
                                      "edit.copy",
                                      NativeMenuShortcut{
                                          .key = KeyCode::C,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
                 nk::MenuItem::action("Paste",
                                      "edit.paste",
                                      NativeMenuShortcut{
                                          .key = KeyCode::V,
                                          .modifiers = NativeMenuModifier::Super,
                                      }),
             }},
            {"Window",
             {
                 nk::MenuItem::action("Minimize", "window.minimize"),
                 nk::MenuItem::action("Bring All to Front", "window.bring_all_to_front"),
             }},
            {"Help",
             {
                 nk::MenuItem::action("About NodalKit Showcase", "help.about"),
             }},
        };
    case ShowcasePlatformFlavor::Windows:
        return {
            {"File",
             {
                 nk::MenuItem::action("New", "file.new"),
                 nk::MenuItem::action("Open...",
                                      "file.open",
                                      NativeMenuShortcut{
                                          .key = KeyCode::O,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action(
                     "Export Diagnostics Bundle",
                     "debug.export_bundle",
                     NativeMenuShortcut{
                         .key = KeyCode::E,
                         .modifiers = NativeMenuModifier::Ctrl | NativeMenuModifier::Shift,
                     }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action("Exit",
                                      "file.quit",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Q,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
             }},
            {"Edit",
             {
                 nk::MenuItem::action("Undo",
                                      "edit.undo",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Z,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::action("Redo",
                                      "edit.redo",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Y,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action("Cut",
                                      "edit.cut",
                                      NativeMenuShortcut{
                                          .key = KeyCode::X,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::action("Copy",
                                      "edit.copy",
                                      NativeMenuShortcut{
                                          .key = KeyCode::C,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::action("Paste",
                                      "edit.paste",
                                      NativeMenuShortcut{
                                          .key = KeyCode::V,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
             }},
            {"View",
             {
                 nk::MenuItem::action("Zoom In", "view.zoom_in"),
                 nk::MenuItem::action("Zoom Out", "view.zoom_out"),
                 nk::MenuItem::action("Reset Zoom", "view.zoom_reset"),
             }},
            {"Tools",
             {
                 nk::MenuItem::action("Preferences", "app.preferences"),
             }},
            {"Help",
             {
                 nk::MenuItem::action("About NodalKit...", "help.about"),
             }},
        };
    case ShowcasePlatformFlavor::LinuxWayland:
    case ShowcasePlatformFlavor::LinuxX11:
    case ShowcasePlatformFlavor::Generic:
        return {
            {"File",
             {
                 nk::MenuItem::action("New", "file.new"),
                 nk::MenuItem::action("Open...",
                                      "file.open",
                                      NativeMenuShortcut{
                                          .key = KeyCode::O,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action(
                     "Export Diagnostics Bundle",
                     "debug.export_bundle",
                     NativeMenuShortcut{
                         .key = KeyCode::E,
                         .modifiers = NativeMenuModifier::Ctrl | NativeMenuModifier::Shift,
                     }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action("Quit",
                                      "file.quit",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Q,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
             }},
            {"Edit",
             {
                 nk::MenuItem::action("Undo",
                                      "edit.undo",
                                      NativeMenuShortcut{
                                          .key = KeyCode::Z,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::action(
                     "Redo",
                     "edit.redo",
                     NativeMenuShortcut{
                         .key = KeyCode::Z,
                         .modifiers = NativeMenuModifier::Ctrl | NativeMenuModifier::Shift,
                     }),
                 nk::MenuItem::make_separator(),
                 nk::MenuItem::action("Cut",
                                      "edit.cut",
                                      NativeMenuShortcut{
                                          .key = KeyCode::X,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::action("Copy",
                                      "edit.copy",
                                      NativeMenuShortcut{
                                          .key = KeyCode::C,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
                 nk::MenuItem::action("Paste",
                                      "edit.paste",
                                      NativeMenuShortcut{
                                          .key = KeyCode::V,
                                          .modifiers = NativeMenuModifier::Ctrl,
                                      }),
             }},
            {"View",
             {
                 nk::MenuItem::action("Zoom In", "view.zoom_in"),
                 nk::MenuItem::action("Zoom Out", "view.zoom_out"),
                 nk::MenuItem::action("Reset Zoom", "view.zoom_reset"),
             }},
            {"Help",
             {
                 nk::MenuItem::action("About NodalKit...", "help.about"),
             }},
        };
    }

    return {};
}
