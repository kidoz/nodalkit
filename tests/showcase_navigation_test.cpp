/// @file showcase_navigation_test.cpp
/// @brief Regression coverage for showcase category callback ownership.

#include "examples/showcase/showcase_widgets.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/accessibility/accessible.h>
#include <nk/ui_core/state_flags.h>
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
