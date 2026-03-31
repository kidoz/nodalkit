/// @file basic_app_test.cpp
/// @brief Smoke test: create Application, Window, widgets, and quit.

#include <catch2/catch_test_macros.hpp>
#include <nk/layout/box_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/text_field.h>

#include <string>

TEST_CASE("Application lifecycle can enter and exit the event loop", "[app]") {
    nk::Application app(0, nullptr);
    app.event_loop().post([&app] { app.quit(0); });
    int code = app.run();
    REQUIRE(code == 0);
}

TEST_CASE("Window and basic widgets support smoke-test interactions", "[app]") {
    nk::Window window({.title = "Test", .width = 320, .height = 240});

    auto label = nk::Label::create("Hello");
    REQUIRE(label->text() == "Hello");

    auto button = nk::Button::create("Click");
    int clicked = 0;
    auto conn = button->on_clicked().connect([&] { ++clicked; });
    (void)conn;

    button->on_clicked().emit();
    REQUIRE(clicked == 1);

    auto field = nk::TextField::create("initial");
    REQUIRE(field->text() == "initial");
    field->set_text("changed");
    REQUIRE(field->text() == "changed");

    window.set_child(label);
    window.present();
    REQUIRE(window.is_visible());
    window.hide();
    REQUIRE_FALSE(window.is_visible());
}

TEST_CASE("StringListModel supports append and remove", "[app]") {
    nk::StringListModel model({"Alpha", "Beta", "Gamma"});
    REQUIRE(model.row_count() == 3);
    REQUIRE(model.display_text(0) == "Alpha");
    REQUIRE(model.display_text(2) == "Gamma");

    model.append("Delta");
    REQUIRE(model.row_count() == 4);

    model.remove(1);
    REQUIRE(model.row_count() == 3);
    REQUIRE(model.display_text(1) == "Gamma");
}

TEST_CASE("SelectionModel single mode deselects previous rows", "[app]") {
    nk::SelectionModel sel(nk::SelectionMode::Single);
    sel.select(2);
    REQUIRE(sel.is_selected(2));
    REQUIRE_FALSE(sel.is_selected(0));

    sel.select(5);
    REQUIRE(sel.is_selected(5));
    REQUIRE_FALSE(sel.is_selected(2));
}
