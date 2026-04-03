/// @file accessibility_probe.cpp
/// @brief Accessibility-focused example with a polished desktop validation surface.

#include "showcase/showcase_widgets.h"

#include <nk/accessibility/accessible.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/style/theme_selection.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

void set_static_accessible_text(const std::shared_ptr<nk::Widget>& widget,
                                std::string name,
                                std::string description = {}) {
    auto& accessible = widget->ensure_accessible();
    accessible.set_role(nk::AccessibleRole::Label);
    accessible.set_name(std::move(name));
    if (!description.empty()) {
        accessible.set_description(std::move(description));
    }
}

std::string visible_query_text(std::string_view text) {
    return text.empty() ? std::string("(empty)") : std::string(text);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    nk::Application app(
        {.app_id = "org.nodalkit.accessibility_probe", .app_name = "NodalKit Accessibility Probe"});

    nk::ThemeSelection theme_selection;
    theme_selection.family = nk::default_theme_family_for(app.system_preferences());
    theme_selection.density = nk::ThemeDensity::Comfortable;
    theme_selection.accent_color_override = nk::Color{0.15F, 0.48F, 0.47F, 1.0F};
    app.set_theme_selection(theme_selection);

    nk::Window window({
        .title = "Accessibility Probe",
        .width = 860,
        .height = 560,
    });

    auto status_bar = nk::StatusBar::create();
    status_bar->set_segments({"Ready", "Battle City", "NTSC"});

    auto hero_status_pill = StatusPill::create("Ready", true);
    auto hero_region_pill = StatusPill::create("NTSC");
    auto hero_accessibility_pill = StatusPill::create("AX / AT-SPI");

    auto hero = HeroBanner::create(
        "Accessibility Probe",
        "A compact validation surface for labels, text values, focus order, activate actions, "
        "and status feedback across assistive tools.",
        {hero_status_pill, hero_region_pill, hero_accessibility_pill});
    hero->set_debug_name("probe-hero");
    set_static_accessible_text(hero,
                               "Accessibility Probe",
                               "Validation surface for assistive technology and keyboard flow.");

    auto search_title = SectionTitle::create("Search Request");
    set_static_accessible_text(search_title, "Search Request");
    auto search_subtitle = SecondaryText::create(
        "Primary validation path: a labelled text field, an explicit activate button, and "
        "live state feedback that mirrors into the status bar.");
    set_static_accessible_text(search_subtitle,
                               "Search request guidance",
                               "Use the field or button to trigger status changes.");

    auto query_label = FieldLabel::create("Search");
    query_label->set_debug_name("search-label");
    set_static_accessible_text(query_label, "Search");

    auto query_field = nk::TextField::create("Battle City");
    query_field->set_debug_name("search-field");
    query_field->set_placeholder("Type a game name");
    query_field->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    query_field->ensure_accessible().set_relation(nk::AccessibleRelationKind::LabelledBy,
                                                  "search-label");

    auto search_hint = SecondaryText::create(
        "Press Enter in the field to activate in place, or move to Open Search and press Enter "
        "there. Both paths should announce a new status.");
    set_static_accessible_text(
        search_hint,
        "Search activation guidance",
        "Both the field and the button should trigger a visible state change.");

    auto open_button = nk::Button::create("Open Search");
    open_button->set_debug_name("open-button");
    open_button->add_style_class("suggested");
    open_button->ensure_accessible().set_relation(nk::AccessibleRelationKind::Controls,
                                                  "search-field");
    open_button->ensure_accessible().set_relation(nk::AccessibleRelationKind::DescribedBy,
                                                  "search-label");

    auto open_actions = Box::horizontal(10.0F);
    open_actions->append(open_button);
    open_actions->append(Spacer::create());

    auto search_group = Box::vertical(10.0F);
    search_group->append(query_label);
    search_group->append(query_field);
    search_group->append(search_hint);
    search_group->append(open_actions);
    auto search_stage = InsetStage::create(search_group, 152.0F, 158.0F, 14.0F);
    search_stage->set_debug_name("search-stage");

    auto region_label = FieldLabel::create("Region");
    region_label->set_debug_name("region-label");
    set_static_accessible_text(region_label, "Region");

    auto region_combo = nk::ComboBox::create();
    region_combo->set_debug_name("region-combo");
    region_combo->set_items({"NTSC", "PAL", "Dendy"});
    region_combo->set_selected_index(0);
    region_combo->ensure_accessible().set_relation(nk::AccessibleRelationKind::LabelledBy,
                                                   "region-label");

    auto region_hint = SecondaryText::create(
        "The selected region should appear as the combo value and stay mirrored in both the hero "
        "summary and the bottom status bar.");
    set_static_accessible_text(
        region_hint, "Region guidance", "Selection changes should update the visible summaries.");

    auto region_group = Box::vertical(10.0F);
    region_group->append(region_label);
    region_group->append(region_combo);
    region_group->append(region_hint);
    auto region_stage = InsetStage::create(region_group, 112.0F, 116.0F, 14.0F);
    region_stage->set_debug_name("region-stage");

    auto left_card_content = Box::vertical(14.0F);
    left_card_content->append(search_title);
    left_card_content->append(search_subtitle);
    left_card_content->append(search_stage);
    left_card_content->append(region_stage);
    auto left_card = SurfacePanel::card(left_card_content);
    left_card->set_debug_name("probe-primary-card");

    auto notes_title = SectionTitle::create("Validation Surface");
    set_static_accessible_text(notes_title, "Validation Surface");
    auto notes_subtitle = SecondaryText::create(
        "Use VoiceOver, Orca, or the runtime inspector to confirm that names, values, actions, "
        "and keyboard traversal stay aligned with what the screen shows.");
    set_static_accessible_text(notes_subtitle,
                               "Validation surface guidance",
                               "Inspect names, values, actions, and keyboard traversal.");

    auto latest_event_label = FieldLabel::create("Latest event");
    set_static_accessible_text(latest_event_label, "Latest event");
    auto latest_event_value = ValueText::create("Ready");
    set_static_accessible_text(latest_event_value, "Ready");
    auto latest_event_detail = SecondaryText::create("Waiting for an activation or region change.");
    set_static_accessible_text(
        latest_event_detail, "Latest event detail", "Waiting for an activation or region change.");

    auto live_query_label = FieldLabel::create("Current query");
    set_static_accessible_text(live_query_label, "Current query");
    auto live_query_value = SecondaryText::create("Battle City");
    set_static_accessible_text(live_query_value, "Battle City");

    auto live_region_label = FieldLabel::create("Current region");
    set_static_accessible_text(live_region_label, "Current region");
    auto live_region_value = ValueText::create("NTSC");
    set_static_accessible_text(live_region_value, "NTSC");

    auto live_group = Box::vertical(8.0F);
    live_group->append(latest_event_label);
    live_group->append(latest_event_value);
    live_group->append(latest_event_detail);
    live_group->append(live_query_label);
    live_group->append(live_query_value);
    live_group->append(live_region_label);
    live_group->append(live_region_value);
    auto live_stage = InsetStage::create(live_group, 194.0F, 198.0F, 14.0F);

    auto semantics_label = FieldLabel::create("Expected semantics");
    set_static_accessible_text(semantics_label, "Expected semantics");
    auto semantics_copy = SecondaryText::create(
        "Search labels the text field. Open Search exposes an activate action and a controls "
        "relation to the field. Region exposes the current selection as its value.");
    set_static_accessible_text(semantics_copy,
                               "Expected semantics detail",
                               "Search labels the text field. Open Search controls the field. "
                               "Region exposes its current value.");

    auto keyboard_label = FieldLabel::create("Keyboard flow");
    set_static_accessible_text(keyboard_label, "Keyboard flow");
    auto keyboard_copy = SecondaryText::create(
        "Tab should move through Search, Open Search, and Region in that order. Shift+Tab should "
        "reverse it, and Enter should trigger the focused action without ambiguity.");
    set_static_accessible_text(keyboard_copy,
                               "Keyboard flow detail",
                               "Tab moves through search field, button, and region combo box.");

    auto semantics_group = Box::vertical(8.0F);
    semantics_group->append(semantics_label);
    semantics_group->append(semantics_copy);
    semantics_group->append(keyboard_label);
    semantics_group->append(keyboard_copy);
    auto semantics_stage = InsetStage::create(semantics_group, 172.0F, 176.0F, 14.0F);

    auto right_card_content = Box::vertical(14.0F);
    right_card_content->append(notes_title);
    right_card_content->append(notes_subtitle);
    right_card_content->append(live_stage);
    right_card_content->append(semantics_stage);
    auto right_card = SurfacePanel::card(right_card_content);
    right_card->set_debug_name("probe-notes-card");

    auto columns = SplitColumns::create(left_card, right_card, 0.57F, 18.0F);
    columns->set_debug_name("probe-columns");

    auto body_content = Box::vertical(18.0F);
    body_content->append(hero);
    body_content->append(columns);
    auto body_page = SurfacePanel::page(body_content);
    body_page->set_debug_name("probe-page");

    auto shell = ShowcaseShell::create(nullptr, body_page, status_bar);

    auto sync_live_values = [&] {
        const auto query = visible_query_text(query_field->text());
        const auto region = std::string(region_combo->selected_text());
        live_query_value->set_text(query);
        live_query_value->ensure_accessible().set_name(query);
        live_region_value->set_text(region);
        live_region_value->ensure_accessible().set_name(region);
        hero_region_pill->set_text(region);
    };

    auto update_status = [&](std::string event, std::string detail) {
        const auto query = visible_query_text(query_field->text());
        const auto region = std::string(region_combo->selected_text());
        status_bar->set_segments({event, query, region});
        latest_event_value->set_text(event);
        latest_event_value->ensure_accessible().set_name(event);
        latest_event_detail->set_text(detail);
        latest_event_detail->ensure_accessible().set_name(detail);
        hero_status_pill->set_text(event);
        sync_live_values();
    };

    sync_live_values();
    update_status("Ready", "Waiting for field activation, button press, or region change.");

    auto text_change_conn =
        query_field->on_text_changed().connect([&](std::string_view) { sync_live_values(); });
    auto open_conn = open_button->on_clicked().connect([&] {
        update_status("Open Search",
                      "Open Search activated for \"" + visible_query_text(query_field->text()) +
                          "\".");
    });
    auto activate_conn = query_field->on_activate().connect([&] {
        update_status("Field Activate",
                      "Search field activated with \"" + visible_query_text(query_field->text()) +
                          "\".");
    });
    auto change_conn = region_combo->on_selection_changed().connect([&](int) {
        update_status("Region changed",
                      "Region switched to " + std::string(region_combo->selected_text()) + ".");
    });
    (void)text_change_conn;
    (void)open_conn;
    (void)activate_conn;
    (void)change_conn;

    window.set_child(shell);
    window.present();

    return app.run();
}
