/// @file widget_views_test.cpp
/// @brief Headless unit tests for navigation, container, and display widgets:
/// item/selection management, expand/split state, severity, and round-trip
/// properties. No Window/event loop required.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <nk/foundation/types.h>
#include <nk/widgets/avatar.h>
#include <nk/widgets/badge.h>
#include <nk/widgets/breadcrumb.h>
#include <nk/widgets/context_menu.h>
#include <nk/widgets/expander.h>
#include <nk/widgets/info_bar.h>
#include <nk/widgets/label.h>
#include <nk/widgets/popover.h>
#include <nk/widgets/progress_bar.h>
#include <nk/widgets/separator.h>
#include <nk/widgets/spinner.h>
#include <nk/widgets/split_view.h>
#include <nk/widgets/tab_bar.h>
#include <nk/widgets/tooltip.h>
#include <nk/widgets/visual_effect_view.h>

TEST_CASE("TabBar manages tabs and selection", "[widgets][tab_bar]") {
    auto bar = nk::TabBar::create();
    REQUIRE(bar->tab_count() == 0);
    REQUIRE(bar->selected_index() < 0);

    bar->append_tab("One");
    // The first appended tab becomes selected.
    REQUIRE(bar->tab_count() == 1);
    REQUIRE(bar->selected_index() == 0);

    bar->append_tab("Two");
    bar->append_tab("Three");
    REQUIRE(bar->tab_count() == 3);
    REQUIRE(bar->tab_label(2) == "Three");

    int changes = 0;
    int last = -1;
    auto conn = bar->on_selection_changed().connect([&](int index) {
        ++changes;
        last = index;
    });

    bar->set_selected_index(2);
    REQUIRE(bar->selected_index() == 2);
    REQUIRE(changes == 1);
    REQUIRE(last == 2);
    // Selecting the current tab does not re-emit.
    bar->set_selected_index(2);
    REQUIRE(changes == 1);

    bar->set_tab_label(0, "First");
    REQUIRE(bar->tab_label(0) == "First");

    bar->remove_tab(0);
    REQUIRE(bar->tab_count() == 2);

    bar->clear_tabs();
    REQUIRE(bar->tab_count() == 0);
    conn.disconnect();
}

TEST_CASE("TabBar throws on out-of-range selection", "[widgets][tab_bar]") {
    auto bar = nk::TabBar::create();
    bar->append_tab("Only");
    REQUIRE_THROWS_AS(bar->set_selected_index(5), std::out_of_range);
}

TEST_CASE("Expander toggles and emits on change only", "[widgets][expander]") {
    auto expander = nk::Expander::create("Details");
    REQUIRE(expander->title() == "Details");
    REQUIRE_FALSE(expander->is_expanded());

    int changes = 0;
    bool last = false;
    auto conn = expander->on_expanded_changed().connect([&](bool expanded) {
        ++changes;
        last = expanded;
    });

    expander->set_expanded(true);
    REQUIRE(expander->is_expanded());
    REQUIRE(changes == 1);
    REQUIRE(last);
    expander->set_expanded(true);
    REQUIRE(changes == 1);

    expander->set_child(nk::Label::create("Body"));
    expander->set_title("More");
    REQUIRE(expander->title() == "More");
    conn.disconnect();
}

TEST_CASE("SplitView clamps the divider position and emits on change", "[widgets][split_view]") {
    auto split = nk::SplitView::create(nk::Orientation::Vertical);
    REQUIRE(split->orientation() == nk::Orientation::Vertical);

    split->set_start_child(nk::Label::create("start"));
    split->set_end_child(nk::Label::create("end"));

    int changes = 0;
    auto conn = split->on_position_changed().connect([&](float) { ++changes; });

    split->set_position(0.25F);
    REQUIRE(split->position() == Catch::Approx(0.25F));
    REQUIRE(changes == 1);

    // Clamped into [0, 1].
    split->set_position(2.0F);
    REQUIRE(split->position() == Catch::Approx(1.0F));
    split->set_position(-1.0F);
    REQUIRE(split->position() == Catch::Approx(0.0F));
    conn.disconnect();
}

TEST_CASE("InfoBar round-trips message, severity, and closable", "[widgets][info_bar]") {
    auto bar = nk::InfoBar::create("Saved", nk::InfoBarSeverity::Success);
    REQUIRE(bar->message() == "Saved");
    REQUIRE(bar->severity() == nk::InfoBarSeverity::Success);
    REQUIRE(bar->is_closable());

    bar->set_message("Failed");
    bar->set_severity(nk::InfoBarSeverity::Error);
    bar->set_closable(false);
    REQUIRE(bar->message() == "Failed");
    REQUIRE(bar->severity() == nk::InfoBarSeverity::Error);
    REQUIRE_FALSE(bar->is_closable());
}

TEST_CASE("Breadcrumb builds, extends, and truncates its path", "[widgets][breadcrumb]") {
    auto crumb = nk::Breadcrumb::create();
    crumb->set_path({"Home", "Projects", "NodalKit"});
    REQUIRE(crumb->depth() == 3);
    REQUIRE(crumb->segment(0) == "Home");
    REQUIRE(crumb->segment(2) == "NodalKit");

    crumb->push("src");
    REQUIRE(crumb->depth() == 4);
    REQUIRE(crumb->segment(3) == "src");

    crumb->pop_to(2);
    REQUIRE(crumb->depth() == 2);
    REQUIRE(crumb->segment(1) == "Projects");
}

TEST_CASE("ContextMenu manages items and starts closed", "[widgets][context_menu]") {
    auto menu = nk::ContextMenu::create();
    REQUIRE(menu->item_count() == 0);
    REQUIRE_FALSE(menu->is_open());

    menu->add_item("Cut");
    menu->add_item("Copy");
    menu->add_separator();
    menu->add_item("Paste");
    REQUIRE(menu->item_count() == 4);

    menu->dismiss();
    REQUIRE_FALSE(menu->is_open());

    menu->clear();
    REQUIRE(menu->item_count() == 0);
}

TEST_CASE("Popover starts closed and accepts content", "[widgets][popover]") {
    auto popover = nk::Popover::create();
    REQUIRE_FALSE(popover->is_open());
    popover->set_child(nk::Label::create("content"));
    popover->dismiss();
    REQUIRE_FALSE(popover->is_open());
}

TEST_CASE("ProgressBar round-trips fraction including indeterminate", "[widgets][progress_bar]") {
    auto bar = nk::ProgressBar::create();
    bar->set_fraction(0.5F);
    REQUIRE(bar->fraction() == Catch::Approx(0.5F));

    // A negative fraction marks the bar indeterminate.
    bar->set_fraction(-1.0F);
    REQUIRE(bar->fraction() < 0.0F);

    // pulse() advances the indeterminate animation without crashing.
    bar->pulse();
}

TEST_CASE("Spinner round-trips spinning state and diameter", "[widgets][spinner]") {
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    REQUIRE(spinner->is_spinning());
    spinner->set_spinning(false);
    REQUIRE_FALSE(spinner->is_spinning());

    spinner->set_diameter(48.0F);
    REQUIRE(spinner->diameter() == Catch::Approx(48.0F));
}

TEST_CASE("Badge and Tooltip round-trip their text", "[widgets][badge][tooltip]") {
    auto badge = nk::Badge::create("3");
    REQUIRE(badge->text() == "3");
    badge->set_text("99+");
    REQUIRE(badge->text() == "99+");

    auto tooltip = nk::Tooltip::create("Hint");
    REQUIRE(tooltip->text() == "Hint");
    tooltip->set_text("Updated hint");
    REQUIRE(tooltip->text() == "Updated hint");
}

TEST_CASE("Avatar round-trips initials, diameter, and image", "[widgets][avatar]") {
    auto avatar = nk::Avatar::create("AB");
    REQUIRE(avatar->initials() == "AB");
    avatar->set_initials("CD");
    REQUIRE(avatar->initials() == "CD");

    avatar->set_diameter(64.0F);
    REQUIRE(avatar->diameter() == Catch::Approx(64.0F));

    // A small ARGB8888 image is accepted and can be cleared without crashing.
    const std::array<std::uint32_t, 4> pixels{0xFFFFFFFFU, 0xFF000000U, 0xFFFF0000U, 0xFF00FF00U};
    avatar->set_image(pixels.data(), 2, 2);
    avatar->clear_image();
}

TEST_CASE("Separator reports its orientation", "[widgets][separator]") {
    auto horizontal = nk::Separator::create();
    REQUIRE(horizontal->orientation() == nk::Orientation::Horizontal);
    auto vertical = nk::Separator::create(nk::Orientation::Vertical);
    REQUIRE(vertical->orientation() == nk::Orientation::Vertical);
}

TEST_CASE("VisualEffectView round-trips material, radius, and child", "[widgets][visual_effect_view]") {
    auto effect = nk::VisualEffectView::create();
    REQUIRE(effect->material() == nk::VisualEffectMaterial::Sidebar);

    effect->set_material(nk::VisualEffectMaterial::Menu);
    REQUIRE(effect->material() == nk::VisualEffectMaterial::Menu);

    effect->set_corner_radius(8.0F);
    REQUIRE(effect->corner_radius() == Catch::Approx(8.0F));

    auto child = nk::Label::create("inner");
    effect->set_child(child);
    REQUIRE(effect->child() == child.get());

    effect->set_fallback_tint(nk::Color{0.1F, 0.2F, 0.3F, 1.0F});
    effect->clear_fallback_tint();
}
