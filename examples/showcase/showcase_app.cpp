/// @file showcase_app.cpp
/// @brief Desktop-style showcase window assembly for NodalKit widgets and runtime features.

#include "showcase_app.h"

#include "showcase_pattern.h"
#include "showcase_profile.h"
#include "showcase_widgets.h"

#include <chrono>
#include <filesystem>
#include <nk/foundation/property.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/model/tree_model.h>
#include <nk/platform/application.h>
#include <nk/platform/native_toolbar.h>
#include <nk/platform/window.h>
#include <nk/style/theme_selection.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/command_palette.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/grid_view.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
#include <nk/widgets/tree_view.h>
#include <nk/widgets/visual_effect_view.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::string showcase_table_field(std::string_view row, std::size_t index) {
    std::size_t start = 0;
    std::size_t field = 0;
    while (start <= row.size()) {
        const auto next = row.find(" | ", start);
        const auto end = next == std::string_view::npos ? row.size() : next;
        if (field == index) {
            return std::string(row.substr(start, end - start));
        }
        if (next == std::string_view::npos) {
            break;
        }
        start = next + 3;
        ++field;
    }
    return {};
}

} // namespace

int run_showcase(int argc, char** argv) {
    nk::Application app(argc, argv);
    auto profile = make_showcase_profile(app.system_preferences());
    if (profile.flavor == ShowcasePlatformFlavor::MacOS) {
        // Roomier page padding to match Mac document-window conventions.
        profile.page_padding_top = 24.0F;
        profile.page_padding_right = 24.0F;
        profile.page_padding_bottom = 24.0F;
        profile.page_padding_left = 24.0F;
    }

    nk::ThemeSelection theme_selection;
    theme_selection.family = profile.theme_family;
    theme_selection.density = profile.density;
    theme_selection.accent_color_override = profile.accent_color;
    app.set_theme_selection(theme_selection);

    const bool is_macos = profile.flavor == ShowcasePlatformFlavor::MacOS;

    nk::Window window({
        .title = profile.window_title,
        .width = static_cast<int>(profile.window_width),
        .height = static_cast<int>(profile.window_height),
        .titlebar_style = is_macos ? nk::TitlebarStyle::Unified : nk::TitlebarStyle::Regular,
    });

    int counter = 0;
    int frame_number = 0;

    auto menus = build_showcase_menus(profile);
    auto menu_bar = nk::MenuBar::create();
    for (auto menu : menus) {
        menu_bar->add_menu(std::move(menu));
    }
    std::shared_ptr<nk::Widget> menu_surface = menu_bar;
    if (app.supports_native_app_menu()) {
        app.set_native_app_menu(menus);
        menu_surface.reset();
    }

    auto status_bar = nk::StatusBar::create();
    status_bar->set_segments({profile.ready_segment, "10 items", "Counter 0"});

    auto hero_counter_pill = StatusPill::create("Counter 0", true);
    auto hero_items_pill = StatusPill::create("10 items");
    auto hero_preview_pill = StatusPill::create(profile.platform_pill_text);
    std::shared_ptr<nk::Widget> hero_close_button;
    if (profile.show_hero_close_button) {
        auto close_button = HeaderActionButton::create("×");
        (void)close_button->on_clicked().connect([&] { app.quit(0); });
        hero_close_button = close_button;
    }

    auto counter_label = ValueText::create("Counter 0");
    auto increment_btn = nk::Button::create("Increment");
    increment_btn->add_style_class("suggested");
    auto decrement_btn = nk::Button::create("Decrement");
    auto reset_btn = nk::Button::create("Reset");

    (void)increment_btn->on_clicked().connect([&] {
        ++counter;
        counter_label->set_text("Counter " + std::to_string(counter));
        status_bar->set_segment(2, "Counter " + std::to_string(counter));
        hero_counter_pill->set_text("Counter " + std::to_string(counter));
    });

    (void)decrement_btn->on_clicked().connect([&] {
        --counter;
        counter_label->set_text("Counter " + std::to_string(counter));
        status_bar->set_segment(2, "Counter " + std::to_string(counter));
        hero_counter_pill->set_text("Counter " + std::to_string(counter));
    });

    (void)reset_btn->on_clicked().connect([&] {
        counter = 0;
        counter_label->set_text("Counter 0");
        status_bar->set_segment(2, "Counter 0");
        hero_counter_pill->set_text("Counter 0");
    });

    auto primary_counter_actions = Box::horizontal(8.0F);
    primary_counter_actions->append(increment_btn);
    primary_counter_actions->append(decrement_btn);
    primary_counter_actions->append(Spacer::create());
    primary_counter_actions->append(reset_btn);
    primary_counter_actions->set_horizontal_size_policy(nk::SizePolicy::Preferred);

    auto input_title = SectionTitle::create(profile.input_title);
    auto input_subtitle = SecondaryText::create(profile.input_subtitle);
    auto command_label = FieldLabel::create("Command");
    auto text_field = nk::TextField::create();
    text_field->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    text_field->set_placeholder("Type a short command...");
    auto echo_result = SecondaryText::create("Echo will appear here.");
    auto counter_caption = FieldLabel::create("Counter");
    auto combo_label = FieldLabel::create(profile.palette_label);
    auto combo = nk::ComboBox::create();
    combo->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    combo->set_items({"Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"});
    combo->set_selected_index(0);
    auto combo_result = SecondaryText::create(profile.palette_value_prefix + "Red");

    (void)text_field->on_text_changed().connect([&](std::string_view text) {
        echo_result->set_text(text.empty() ? "Echo will appear here."
                                           : "Echo: " + std::string(text));
    });

    (void)text_field->on_activate().connect([&] {
        echo_result->set_text("Submitted: " + std::string(text_field->text()));
        status_bar->set_segment(0, "Submitted command");
    });

    (void)combo->on_selection_changed().connect([&](int index) {
        if (index >= 0) {
            combo_result->set_text(profile.palette_value_prefix +
                                   std::string(combo->item(static_cast<std::size_t>(index))));
        } else {
            combo_result->set_text(profile.palette_value_prefix + "(none)");
        }
    });

    auto command_group = Box::vertical(8.0F);
    command_group->append(command_label);
    command_group->append(text_field);
    command_group->append(echo_result);

    auto counter_group = Box::vertical(12.0F);
    counter_group->append(counter_caption);
    counter_group->append(counter_label);
    counter_group->append(primary_counter_actions);

    auto accent_group = Box::vertical(8.0F);
    accent_group->append(combo_label);
    accent_group->append(combo);
    accent_group->append(combo_result);

    auto controls_content = Box::vertical(profile.controls_spacing);
    controls_content->append(input_title);
    controls_content->append(input_subtitle);
    controls_content->append(InsetStage::create(command_group,
                                                profile.command_stage_min_height,
                                                profile.command_stage_natural_height,
                                                profile.command_stage_padding));
    controls_content->append(InsetStage::create(counter_group,
                                                profile.counter_stage_min_height,
                                                profile.counter_stage_natural_height,
                                                profile.counter_stage_padding));
    controls_content->append(InsetStage::create(accent_group,
                                                profile.palette_stage_min_height,
                                                profile.palette_stage_natural_height,
                                                profile.palette_stage_padding));
    auto controls_card = SurfacePanel::card(controls_content);

    auto list_title = SectionTitle::create(profile.list_title);
    auto list_subtitle = SecondaryText::create(profile.list_subtitle);
    auto model = std::make_shared<nk::StringListModel>(std::vector<std::string>{
        "Alpha",
        "Bravo",
        "Charlie",
        "Delta",
        "Echo",
        "Foxtrot",
        "Golf",
        "Hotel",
        "India",
        "Juliet",
    });
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->select(0);
    auto list_view = nk::ListView::create();
    list_view->set_model(model);
    list_view->set_selection_model(selection);
    list_view->set_row_height(30.0F);
    auto list_stage = InsetStage::create(list_view,
                                         profile.list_stage_min_height,
                                         profile.list_stage_natural_height,
                                         profile.list_stage_padding);
    auto list_status = StatusPill::create("10 items");
    auto add_item_btn = nk::Button::create("Add Item");
    add_item_btn->add_style_class("suggested");

    (void)add_item_btn->on_clicked().connect([&] {
        const auto next = model->row_count() + 1;
        model->append("Item " + std::to_string(next));
        const auto count_text = std::to_string(model->row_count()) + " items";
        list_status->set_text(count_text);
        status_bar->set_segment(1, count_text);
        hero_items_pill->set_text(count_text);
    });

    auto list_footer = Box::horizontal(10.0F);
    list_footer->append(list_status);
    list_footer->append(add_item_btn);

    auto table_label = FieldLabel::create("Data table");
    auto table_model = std::make_shared<nk::StringListModel>(std::vector<std::string>{
        "NK-101 | DataTable | In Progress | High",
        "NK-108 | TreeView | Preview | Medium",
        "NK-114 | GridView | Preview | Medium",
        "NK-120 | ToastOverlay | Backlog | Low",
        "NK-131 | CommandPalette | Preview | High",
    });
    auto table_selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    table_selection->select(0);
    auto data_table = nk::DataTable::create();
    data_table->set_model(table_model);
    data_table->set_selection_model(table_selection);
    data_table->set_min_column_width(72.0F);
    data_table->set_columns({
        nk::DataTableColumn{
            .id = "id",
            .title = "ID",
            .width = 84.0F,
            .sortable = true,
            .text =
                [](const nk::AbstractListModel& model, std::size_t row) {
                    return showcase_table_field(model.display_text(row), 0);
                },
        },
        nk::DataTableColumn{
            .id = "widget",
            .title = "Widget",
            .width = 150.0F,
            .sortable = true,
            .text =
                [](const nk::AbstractListModel& model, std::size_t row) {
                    return showcase_table_field(model.display_text(row), 1);
                },
        },
        nk::DataTableColumn{
            .id = "status",
            .title = "Status",
            .width = 128.0F,
            .sortable = true,
            .text =
                [](const nk::AbstractListModel& model, std::size_t row) {
                    return showcase_table_field(model.display_text(row), 2);
                },
        },
        nk::DataTableColumn{
            .id = "priority",
            .title = "Priority",
            .width = 118.0F,
            .sortable = true,
            .text =
                [](const nk::AbstractListModel& model, std::size_t row) {
                    return showcase_table_field(model.display_text(row), 3);
                },
        },
    });
    data_table->sort_by_column(0, nk::DataTableSortDirection::Ascending);
    auto table_stage = InsetStage::create(data_table, 156.0F, 172.0F, profile.list_stage_padding);

    auto tree_label = FieldLabel::create("Tree view");
    auto tree_model = std::make_shared<nk::TreeModel>();
    const auto project_node = tree_model->add_root("NodalKit");
    const auto include_node = tree_model->append_child(project_node, "include/nk");
    (void)tree_model->append_child(include_node, "model");
    (void)tree_model->append_child(include_node, "widgets");
    const auto source_node = tree_model->append_child(project_node, "src");
    (void)tree_model->append_child(source_node, "model");
    (void)tree_model->append_child(source_node, "widgets");
    const auto docs_node = tree_model->add_root("docs");
    (void)tree_model->append_child(docs_node, "BUILDING_APPLICATIONS.md");
    auto tree_selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    tree_selection->select(project_node);
    tree_selection->set_current_row(project_node);
    auto tree_view = nk::TreeView::create();
    tree_view->set_model(tree_model);
    tree_view->set_selection_model(tree_selection);
    tree_view->set_row_height(26.0F);
    auto tree_stage = InsetStage::create(tree_view, 144.0F, 160.0F, profile.list_stage_padding);

    auto grid_label = FieldLabel::create("Grid view");
    auto grid_model = std::make_shared<nk::StringListModel>(std::vector<std::string>{
        "Button",
        "TextField",
        "ListView",
        "DataTable",
        "TreeView",
        "GridView",
    });
    auto grid_selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    grid_selection->select(0);
    grid_selection->set_current_row(0);
    auto grid_view = nk::GridView::create();
    grid_view->set_model(grid_model);
    grid_view->set_selection_model(grid_selection);
    grid_view->set_cell_width(96.0F);
    grid_view->set_cell_height(58.0F);
    grid_view->set_gap(8.0F);
    auto grid_stage = InsetStage::create(grid_view, 144.0F, 160.0F, profile.list_stage_padding);

    auto list_content = Box::vertical(12.0F);
    list_content->append(list_title);
    list_content->append(list_subtitle);
    list_content->append(list_stage);
    list_content->append(list_footer);
    list_content->append(table_label);
    list_content->append(table_stage);
    list_content->append(tree_label);
    list_content->append(tree_stage);
    list_content->append(grid_label);
    list_content->append(grid_stage);
    std::shared_ptr<nk::Widget> list_card;
    if (is_macos) {
        // Native-Mac sidebar: edge-to-edge vibrancy with no card chrome so the
        // material reads as the surface itself, not a floating rounded panel.
        list_content->set_padding(nk::Insets::uniform(16.0F));
        auto vibrancy = nk::VisualEffectView::create(nk::VisualEffectMaterial::Sidebar);
        vibrancy->set_corner_radius(0.0F);
        vibrancy->set_child(list_content);
        list_card = vibrancy;
    } else {
        list_card = SurfacePanel::card(list_content);
    }

    auto preview_title = SectionTitle::create(profile.preview_title);
    auto preview_subtitle = SecondaryText::create(profile.preview_subtitle);
    auto image_label = FieldLabel::create("Live image");
    auto image_meta = SecondaryText::create("128 x 96 source");
    auto image_detail = SecondaryText::create("Animated sample with explicit display scaling.");
    auto preview_canvas = PreviewCanvas::create();
    auto preview_stage = InsetStage::create(preview_canvas,
                                            profile.preview_stage_min_height,
                                            profile.preview_stage_natural_height,
                                            profile.preview_stage_padding);
    auto scale_label = FieldLabel::create("Scale mode");
    auto scale_combo = nk::ComboBox::create();
    scale_combo->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    scale_combo->set_items({"Nearest Neighbor", "Bilinear"});
    scale_combo->set_selected_index(0);
    auto scale_state = SecondaryText::create("Nearest-neighbor preview active.");

    constexpr int kImageWidth = 128;
    constexpr int kImageHeight = 96;
    auto pixels = generate_test_pattern(kImageWidth, kImageHeight, 0);
    preview_canvas->update_pixel_buffer(pixels.data(), kImageWidth, kImageHeight);

    (void)scale_combo->on_selection_changed().connect([&](int index) {
        preview_canvas->set_scale_mode(index == 0 ? nk::ScaleMode::NearestNeighbor
                                                  : nk::ScaleMode::Bilinear);
        scale_state->set_text(index == 0 ? "Nearest-neighbor preview active."
                                         : "Bilinear preview active.");
        hero_preview_pill->set_text(index == 0 ? profile.platform_pill_text
                                               : profile.platform_pill_text + " Bilinear");
    });

    std::shared_ptr<nk::Widget> preview_display;
    if (profile.preview_layout_mode == ShowcasePreviewLayoutMode::Stacked) {
        auto preview_meta_row = Box::horizontal(12.0F);
        preview_meta_row->append(image_label);
        preview_meta_row->append(image_meta);
        preview_meta_row->append(Spacer::create());

        auto preview_controls = Box::vertical(6.0F);
        preview_controls->append(scale_label);
        preview_controls->append(scale_combo);
        preview_controls->append(scale_state);

        auto preview_stack = Box::vertical(profile.preview_section_spacing);
        preview_stack->append(preview_meta_row);
        preview_stack->append(image_detail);
        preview_stack->append(preview_stage);
        preview_stack->append(preview_controls);
        preview_display = preview_stack;
    } else {
        auto preview_meta = Box::vertical(6.0F);
        preview_meta->append(image_label);
        preview_meta->append(image_meta);
        preview_meta->append(image_detail);

        auto preview_controls = Box::vertical(8.0F);
        preview_controls->append(scale_label);
        preview_controls->append(scale_combo);
        preview_controls->append(scale_state);

        auto preview_info = Box::vertical(20.0F);
        preview_info->set_horizontal_size_policy(nk::SizePolicy::Preferred);
        preview_info->append(preview_meta);
        preview_info->append(preview_controls);
        preview_info->append(Spacer::create());

        preview_display = SplitColumns::create(preview_stage,
                                               preview_info,
                                               profile.preview_split_ratio,
                                               profile.preview_section_spacing);
    }

    auto preview_content = Box::vertical(profile.preview_section_spacing);
    preview_content->append(preview_title);
    preview_content->append(preview_subtitle);
    preview_content->append(preview_display);
    auto preview_card = SurfacePanel::card(preview_content);

    auto actions_title = SectionTitle::create(profile.actions_title);
    auto actions_subtitle = SecondaryText::create(profile.actions_subtitle);
    auto prop_label = FieldLabel::create("Property binding");
    nk::Property<int> source_prop{42};
    nk::Property<int> target_prop{0};
    [[maybe_unused]] auto binding = target_prop.bind_to(source_prop);
    auto prop_value_label =
        ValueText::create("Source: 42, Target: " + std::to_string(target_prop.get()));
    auto prop_detail = SecondaryText::create("Shared state updates the footer and live status.");
    auto prop_btn = nk::Button::create(
        profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact ? "Sync Source = 99"
                                                                          : "Set Source = 99");
    prop_btn->set_horizontal_size_policy(nk::SizePolicy::Preferred);
    auto dialog_label = FieldLabel::create("Sheet dialog");
    auto dialog_detail =
        SecondaryText::create("Open a window-attached sheet with explicit minimum width.");
    auto dialog_btn =
        nk::Button::create(profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact
                               ? "Show Sheet"
                               : "Show Preferences Sheet");
    dialog_btn->add_style_class("suggested");
    dialog_btn->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    auto runtime_status = ValueText::create("Waiting for an action.");
    auto runtime_status_detail = SecondaryText::create("No runtime action has fired yet.");

    auto export_debug_bundle = [&] {
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        const auto bundle_dir = std::filesystem::temp_directory_path() /
                                ("nodalkit-showcase-debug-bundle-" + std::to_string(timestamp));
        const auto result = window.save_debug_bundle(bundle_dir.string());
        if (result) {
            const auto bundle_label = bundle_dir.filename().string();
            status_bar->set_segment(0, "Diagnostics bundle exported");
            runtime_status->set_text("Diagnostics bundle exported.");
            runtime_status_detail->set_text("Saved to " + bundle_label + " in the temp directory.");

            auto dialog = nk::Dialog::create(
                "Diagnostics Bundle", "Exported showcase diagnostics to:\n" + bundle_dir.string());
            dialog->add_button("OK", nk::DialogResponse::Accept);
            dialog->present(window);
            return;
        }

        status_bar->set_segment(0, "Diagnostics export failed");
        runtime_status->set_text("Diagnostics export failed.");
        runtime_status_detail->set_text(std::string(result.error()));

        auto dialog =
            nk::Dialog::create("Diagnostics Bundle Export Failed", std::string(result.error()));
        dialog->add_button("OK", nk::DialogResponse::Accept);
        dialog->present(window);
    };

    (void)prop_btn->on_clicked().connect([&] {
        source_prop.set(99);
        prop_value_label->set_text("Source: " + std::to_string(source_prop.get()) +
                                   ", Target: " + std::to_string(target_prop.get()));
        runtime_status->set_text("Property binding updated.");
        runtime_status_detail->set_text("Source and target are now synchronized at 99.");
        status_bar->set_segment(0, "Property binding updated");
    });

    auto present_preferences_sheet = [&] {
        auto dialog = nk::Dialog::create(
            "Preferences",
            "Review the current showcase shell configuration before applying the change.");
        dialog->set_presentation_style(nk::DialogPresentationStyle::Sheet);
        dialog->set_minimum_panel_width(460.0F);
        dialog->add_button("Later", nk::DialogResponse::Cancel);
        dialog->add_button("Apply", nk::DialogResponse::Accept);
        (void)dialog->on_response().connect([&](nk::DialogResponse response) {
            if (response == nk::DialogResponse::Accept) {
                status_bar->set_segment(0, "Sheet: Applied");
                runtime_status->set_text("Preferences sheet applied.");
                runtime_status_detail->set_text(
                    "The top-attached sheet closed after an accepted response.");
            } else {
                status_bar->set_segment(0, "Sheet: Deferred");
                runtime_status->set_text("Preferences sheet deferred.");
                runtime_status_detail->set_text(
                    "The sheet was dismissed without applying any changes.");
            }
        });
        dialog->present(window);
    };

    (void)dialog_btn->on_clicked().connect(present_preferences_sheet);

    auto property_group = Box::vertical(
        profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact ? 6.0F : 8.0F);
    property_group->append(prop_label);
    if (profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact) {
        property_group->append(prop_btn);
    } else {
        property_group->append(prop_value_label);
        property_group->append(prop_detail);
        property_group->append(prop_btn);
    }
    auto property_panel = InsetStage::create(property_group,
                                             profile.runtime_property_min_height,
                                             profile.runtime_property_natural_height,
                                             profile.runtime_property_padding);

    auto dialog_group = Box::vertical(
        profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact ? 6.0F : 8.0F);
    dialog_group->append(dialog_label);
    if (profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact) {
        dialog_group->append(dialog_btn);
    } else {
        dialog_group->append(dialog_detail);
        dialog_group->append(dialog_btn);
    }
    auto dialog_panel = InsetStage::create(dialog_group,
                                           profile.runtime_dialog_min_height,
                                           profile.runtime_dialog_natural_height,
                                           profile.runtime_dialog_padding);

    auto status_label = FieldLabel::create("Latest result");
    auto status_group = Box::vertical(
        profile.actions_layout_mode == ShowcaseActionsLayoutMode::Compact ? 4.0F : 8.0F);
    status_group->append(status_label);
    status_group->append(runtime_status);
    status_group->append(runtime_status_detail);
    auto status_panel = InsetStage::create(status_group,
                                           profile.runtime_status_min_height,
                                           profile.runtime_status_natural_height,
                                           profile.runtime_status_padding);

    auto actions_row =
        SplitColumns::create(property_panel, dialog_panel, 0.5F, profile.actions_row_spacing);

    auto palette_label = FieldLabel::create("Command palette");
    auto command_palette = nk::CommandPalette::create();
    command_palette->set_commands({
        nk::CommandPaletteCommand{
            .id = "file.quit",
            .title = "Quit",
            .subtitle = "Close the showcase window",
            .category = "File",
        },
        nk::CommandPaletteCommand{
            .id = "debug.export_bundle",
            .title = "Export Diagnostics Bundle",
            .subtitle = "Save frame and widget diagnostics",
            .category = "Debug",
        },
        nk::CommandPaletteCommand{
            .id = "app.preferences",
            .title = "Show Preferences",
            .subtitle = "Open the preferences sheet",
            .category = "Application",
        },
        nk::CommandPaletteCommand{
            .id = "help.about",
            .title = "About NodalKit",
            .subtitle = "Show toolkit version information",
            .category = "Help",
        },
        nk::CommandPaletteCommand{
            .id = "edit.disabled",
            .title = "Disabled Command",
            .subtitle = "Visible but unavailable",
            .category = "Edit",
            .enabled = false,
        },
    });
    (void)command_palette->on_command_activated().connect([&](std::string_view command_id) {
        status_bar->set_segment(0, "Command: " + std::string(command_id));
        runtime_status->set_text("Command palette: " + std::string(command_id));
        runtime_status_detail->set_text("The command was activated from the searchable palette.");
    });
    auto palette_stage =
        InsetStage::create(command_palette, 186.0F, 220.0F, profile.runtime_status_padding);

    auto actions_content = Box::vertical(profile.preview_section_spacing);
    actions_content->append(actions_title);
    actions_content->append(actions_subtitle);
    actions_content->append(status_panel);
    actions_content->append(actions_row);
    actions_content->append(palette_label);
    actions_content->append(palette_stage);
    auto actions_card = SurfacePanel::card(actions_content);

    auto left_column = Box::vertical(profile.controls_spacing);
    left_column->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    left_column->append(controls_card);
    left_column->append(list_card);

    auto right_column = Box::vertical(profile.controls_spacing);
    right_column->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    right_column->append(preview_card);
    right_column->append(actions_card);

    auto content_row = SplitColumns::create(
        left_column, right_column, profile.main_split_ratio, profile.main_column_spacing);
    content_row->set_vertical_size_policy(nk::SizePolicy::Preferred);
    content_row->set_vertical_stretch(0);
    std::vector<std::shared_ptr<nk::Widget>> hero_pills = {
        hero_counter_pill,
        hero_items_pill,
        hero_preview_pill,
    };
    if (hero_close_button) {
        hero_pills.push_back(hero_close_button);
    }
    auto hero_banner =
        HeroBanner::create(profile.hero_title, profile.hero_subtitle, std::move(hero_pills));
    hero_banner->set_height(profile.hero_min_height, profile.hero_natural_height);

    auto body_content = Box::vertical(profile.section_spacing);
    // Mac apps put the window title + toolbar at the top; an inline hero
    // banner duplicates that chrome, so hide it on macOS.
    if (!is_macos) {
        body_content->append(hero_banner);
    }
    body_content->append(content_row);
    auto page_bottom_spacer = InsetStage::create(Spacer::create(),
                                                 profile.page_bottom_spacer_height,
                                                 profile.page_bottom_spacer_height,
                                                 0.0F);
    body_content->append(page_bottom_spacer);

    auto body_page = SurfacePanel::page(body_content);
    body_page->set_padding(profile.page_padding_top,
                           profile.page_padding_right + profile.scrollbar_safe_gutter,
                           profile.page_padding_bottom,
                           profile.page_padding_left);
    body_page->set_vertical_size_policy(nk::SizePolicy::Preferred);
    body_page->set_vertical_stretch(0);
    auto scroll_body = nk::ScrollArea::create();
    scroll_body->set_h_scroll_policy(nk::ScrollPolicy::Never);
    scroll_body->set_v_scroll_policy(nk::ScrollPolicy::Automatic);
    scroll_body->set_content(body_page);

    auto root = ShowcaseShell::create(menu_surface, scroll_body, status_bar);
    window.set_child(root);

    list_view->grab_focus();

    auto handle_menu_action = [&](std::string_view action) {
        if (action == "file.quit") {
            app.quit(0);
            return;
        }

        if (action == "debug.export_bundle") {
            export_debug_bundle();
            return;
        }

        if (action == "help.about") {
            auto dialog = nk::Dialog::create(
                "About", "NodalKit Showcase v0.1.0\nA C++23 desktop GUI toolkit");
            dialog->add_button("OK", nk::DialogResponse::Accept);
            dialog->present(window);
            return;
        }

        if (action == "app.preferences") {
            present_preferences_sheet();
            return;
        }

        status_bar->set_segment(0, "Action: " + std::string(action));
        runtime_status->set_text("Menu action: " + std::string(action));
        runtime_status_detail->set_text("The last runtime event came from the application menu.");
    };

    (void)menu_bar->on_action().connect(handle_menu_action);
    (void)app.on_native_app_menu_action().connect(handle_menu_action);

    (void)app.event_loop().set_interval(std::chrono::milliseconds(33), [&] {
        ++frame_number;
        auto frame = generate_test_pattern(kImageWidth, kImageHeight, frame_number);
        preview_canvas->update_pixel_buffer(frame.data(), kImageWidth, kImageHeight);
    });

    (void)window.on_close_requested().connect([&] { app.quit(0); });

    if (is_macos) {
        // Toolbar carries only window-scope actions (add, search, preferences).
        // Counter controls (Increment/Decrement/Reset) live with the counter
        // in the command card where they belong.
        nk::NativeToolbarConfig toolbar;
        toolbar.identifier = "com.nodalkit.showcase.toolbar";
        toolbar.allows_user_customization = true;

        nk::NativeToolbarItem add_item;
        add_item.kind = nk::NativeToolbarItem::Kind::Button;
        add_item.identifier = "add_item";
        add_item.label = "Add Item";
        add_item.tooltip = "Append a new item to the list";
        add_item.symbol_name = "plus";
        add_item.on_activate = [&] {
            const auto next = model->row_count() + 1;
            model->append("Item " + std::to_string(next));
            const auto count_text = std::to_string(model->row_count()) + " items";
            list_status->set_text(count_text);
            status_bar->set_segment(1, count_text);
            hero_items_pill->set_text(count_text);
        };
        toolbar.items.push_back(std::move(add_item));

        nk::NativeToolbarItem flex;
        flex.kind = nk::NativeToolbarItem::Kind::FlexibleSpace;
        flex.identifier = "flex";
        flex.label = "";
        toolbar.items.push_back(std::move(flex));

        nk::NativeToolbarItem search;
        search.kind = nk::NativeToolbarItem::Kind::SearchField;
        search.identifier = "search";
        search.label = "Search";
        search.tooltip = "Filter by name";
        search.on_search_submit = [&](std::string_view query) {
            const std::string text =
                query.empty() ? "Search cleared" : "Search: " + std::string(query);
            status_bar->set_segment(0, text);
        };
        toolbar.items.push_back(std::move(search));

        nk::NativeToolbarItem show_sheet;
        show_sheet.kind = nk::NativeToolbarItem::Kind::Button;
        show_sheet.identifier = "show_sheet";
        show_sheet.label = "Preferences";
        show_sheet.tooltip = "Show the preferences sheet";
        show_sheet.symbol_name = "gearshape";
        show_sheet.on_activate = present_preferences_sheet;
        toolbar.items.push_back(std::move(show_sheet));

        window.set_native_toolbar(std::move(toolbar));
    }

    window.present();
    return app.run();
}
