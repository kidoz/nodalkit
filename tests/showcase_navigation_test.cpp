/// @file showcase_navigation_test.cpp
/// @brief Regression coverage for showcase category callback ownership.

#include "examples/showcase/showcase_widgets.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/accessibility/accessible.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/headerbar.h>
#include <nk/widgets/navigation_split_view.h>
#include <string>
#include <vector>

TEST_CASE("Showcase navigation survives setup scope destruction", "[showcase][navigation]") {
    auto page_stack = PageStack::create();
    page_stack->add_page(Box::vertical());
    page_stack->add_page(Box::vertical());

    auto split_view = nk::NavigationSplitView::create();
    split_view->set_content(page_stack);
    split_view->set_collapsed(true);
    split_view->set_show_content(false);

    auto headerbar = nk::Headerbar::create("NodalKit Showcase");
    std::shared_ptr<NavigationRow> controls_row;
    std::shared_ptr<NavigationRow> preview_row;
    {
        controls_row = NavigationRow::create("Controls");
        controls_row->set_selected(true);
        preview_row = NavigationRow::create("Preview");
        std::vector<std::shared_ptr<NavigationRow>> rows = {controls_row, preview_row};
        auto controller =
            ShowcaseNavigationController::create(page_stack,
                                                 split_view,
                                                 headerbar,
                                                 std::vector<std::string>{"Controls", "Preview"},
                                                 rows);
        REQUIRE(controller != nullptr);
    }

    REQUIRE(preview_row->accessible() != nullptr);
    REQUIRE(preview_row->accessible()->perform_action(nk::AccessibleAction::Activate));
    CHECK(page_stack->visible_page() == 1);
    CHECK(split_view->shows_content());
    CHECK(headerbar->title() == "Preview");
    CHECK(headerbar->shows_back_button());
    CHECK_FALSE(nk::has_flag(controls_row->state_flags(), nk::StateFlags::Selected));
    CHECK(nk::has_flag(preview_row->state_flags(), nk::StateFlags::Selected));
}

TEST_CASE("Showcase inset columns reserve wrapped height at their allocated widths",
          "[showcase][layout]") {
    auto property = nk::Box::create(nk::Orientation::Vertical, 8.0F);
    property->append(FieldLabel::create("Property binding"));
    property->append(ValueText::create("Source: 99, Target: 99"));
    property->append(SecondaryText::create("Shared state updates the footer and live status."));
    auto button = nk::Button::create("Set Source = 99");
    property->append(button);
    auto property_stage = InsetStage::create(property, 124.0F, 132.0F, 16.0F, false);
    auto dialog = nk::Box::create(nk::Orientation::Vertical, 8.0F);
    dialog->append(
        SecondaryText::create("Open a window-attached sheet with explicit minimum width."));
    auto dialog_button = nk::Button::create("Show Preferences Sheet");
    dialog->append(dialog_button);
    auto dialog_stage = InsetStage::create(dialog, 124.0F, 132.0F, 16.0F, false);
    auto columns = SplitColumns::create(property_stage, dialog_stage, 0.5F, 18.0F);
    auto card = SurfacePanel::card(columns);
    card->set_padding(16.0F);
    for (const float width : {300.0F, 392.0F, 630.0F}) {
        const auto request = card->measure({.max_width = width});
        card->allocate({0, 0, width, request.natural_height});
        CAPTURE(width);
        CHECK(button->allocation().y >= property_stage->allocation().y);
        CHECK(button->allocation().bottom() <= property_stage->allocation().bottom() - 16.0F);
        CHECK(dialog_button->allocation().bottom() <= dialog_stage->allocation().bottom() - 16.0F);
        CHECK(property_stage->allocation().bottom() <= card->allocation().bottom());
    }
}
