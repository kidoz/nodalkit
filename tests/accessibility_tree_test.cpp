#include "../src/accessibility/accessibility_tree.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/label.h>
#include <nk/widgets/text_field.h>
#include <vector>

namespace {

class Container : public nk::Widget {
public:
    static std::shared_ptr<Container> create(nk::AccessibleRole role = nk::AccessibleRole::None) {
        auto container = std::shared_ptr<Container>(new Container());
        if (role != nk::AccessibleRole::None) {
            container->ensure_accessible().set_role(role);
        }
        return container;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

    void remove(nk::Widget& child) { remove_child(child); }

private:
    Container() = default;
};

bool contains(const std::vector<nk::detail::AccessibleId>& ids, nk::detail::AccessibleId id) {
    return std::ranges::find(ids, id) != ids.end();
}

} // namespace

TEST_CASE("Accessibility identities stay stable and are never reused", "[accessibility]") {
    nk::Window window({.title = "Identity"});
    auto root = Container::create();
    auto first = nk::Button::create("First");
    auto second = nk::Button::create("Second");
    root->append(first);
    root->append(second);
    window.set_child(root);

    nk::detail::AccessibilityTree tree(window);
    const auto ids = tree.root_children();
    REQUIRE(ids.size() == 2);
    CHECK(tree.root_children() == ids);
    CHECK(ids[0] != ids[1]);
    CHECK(tree.info(ids[0])->name == "First");

    root->remove(*second);
    second.reset();
    CHECK_FALSE(tree.info(ids[1]).has_value());
    CHECK(tree.purge() == std::vector{ids[1]});
    CHECK(tree.purge().empty());

    auto third = nk::Button::create("Third");
    root->append(third);
    const auto after = tree.root_children();
    REQUIRE(after.size() == 2);
    CHECK(after[0] == ids[0]);
    CHECK(after[1] != ids[1]);
    CHECK(tree.info(after[1])->name == "Third");
}

TEST_CASE("Accessibility structure flattens containers and skips hidden subtrees",
          "[accessibility]") {
    nk::Window window({.title = "Structure"});
    auto root = Container::create();
    auto group = Container::create(nk::AccessibleRole::Group);
    auto label = nk::Label::create("Caption");
    auto hidden = Container::create();
    auto button = nk::Button::create("Inside");
    group->append(label);
    group->append(hidden);
    hidden->append(button);
    root->append(group);
    window.set_child(root);
    hidden->set_visible(false);

    nk::detail::AccessibilityTree tree(window);
    const auto roots = tree.root_children();
    REQUIRE(roots.size() == 1);
    const auto group_id = roots[0];
    CHECK(tree.info(group_id)->role == nk::AccessibleRole::Group);
    CHECK_FALSE(tree.parent(group_id).has_value());
    auto children = tree.children(group_id);
    REQUIRE(children.size() == 1);
    CHECK(tree.info(children[0])->name == "Caption");
    CHECK(tree.parent(children[0]) == group_id);

    hidden->set_visible(true);
    children = tree.children(group_id);
    REQUIRE(children.size() == 2);
    const auto button_id = children[1];
    CHECK(tree.parent(button_id) == group_id);
    hidden->set_visible(false);
    CHECK_FALSE(tree.is_exposed(button_id));
    CHECK_FALSE(tree.perform(button_id, nk::AccessibleAction::Activate));
}

TEST_CASE("A modal overlay is the only exposed subtree", "[accessibility]") {
    nk::Window window({.title = "Modal", .width = 480, .height = 320});
    auto root = Container::create();
    auto behind = nk::Button::create("Behind");
    int behind_clicks = 0;
    auto behind_connection = behind->on_clicked().connect([&] { ++behind_clicks; });
    root->append(behind);
    window.set_child(root);

    nk::detail::AccessibilityTree tree(window);
    const auto behind_id = tree.root_children().at(0);

    auto dialog = nk::Dialog::create("Confirm");
    auto content = Container::create();
    auto inside = nk::Button::create("Inside");
    int inside_clicks = 0;
    auto inside_connection = inside->on_clicked().connect([&] { ++inside_clicks; });
    content->append(inside);
    dialog->set_content(content);
    dialog->present(window);

    const auto roots = tree.root_children();
    REQUIRE(roots.size() == 1);
    CHECK(tree.info(roots[0])->role == nk::AccessibleRole::Dialog);
    CHECK_FALSE(contains(roots, behind_id));
    CHECK_FALSE(tree.is_exposed(behind_id));
    CHECK_FALSE(tree.perform(behind_id, nk::AccessibleAction::Activate));
    CHECK(behind_clicks == 0);

    const auto dialog_children = tree.children(roots[0]);
    REQUIRE(dialog_children.size() == 1);
    CHECK(tree.perform(dialog_children[0], nk::AccessibleAction::Activate));
    CHECK(inside_clicks == 1);

    dialog->close();
    CHECK(tree.root_children() == std::vector{behind_id});
    CHECK(tree.perform(behind_id, nk::AccessibleAction::Activate));
    CHECK(behind_clicks == 1);
    CHECK(behind_connection.connected());
    CHECK(inside_connection.connected());
}

TEST_CASE("Disabled controls reject accessible actions and focus", "[accessibility]") {
    nk::Window window({.title = "Disabled"});
    auto root = Container::create();
    auto group = Container::create(nk::AccessibleRole::Group);
    auto button = nk::Button::create("Save");
    int clicks = 0;
    auto connection = button->on_clicked().connect([&] { ++clicks; });
    group->append(button);
    root->append(group);
    window.set_child(root);

    nk::detail::AccessibilityTree tree(window);
    const auto button_id = tree.children(tree.root_children().at(0)).at(0);
    button->set_sensitive(false);
    CHECK_FALSE(tree.info(button_id)->enabled);
    CHECK_FALSE(tree.perform(button_id, nk::AccessibleAction::Activate));
    CHECK_FALSE(tree.focus(button_id));

    button->set_sensitive(true);
    group->set_sensitive(false);
    CHECK_FALSE(tree.info(button_id)->enabled);
    CHECK_FALSE(tree.perform(button_id, nk::AccessibleAction::Activate));
    CHECK(clicks == 0);

    group->set_sensitive(true);
    CHECK(tree.perform(button_id, nk::AccessibleAction::Activate));
    CHECK_FALSE(tree.perform(button_id, nk::AccessibleAction::Toggle));
    CHECK(clicks == 1);
    CHECK(connection.connected());
}

TEST_CASE("Accessibility reports focus, bounds, and hit-test targets", "[accessibility]") {
    nk::Window window({.title = "Geometry", .width = 400, .height = 300});
    auto root = Container::create();
    auto group = Container::create(nk::AccessibleRole::Group);
    auto button = nk::Button::create("Target");
    group->append(button);
    root->append(group);
    window.set_child(root);
    root->allocate({0, 0, 400, 300});
    group->allocate({20, 20, 200, 100});
    button->allocate({40, 40, 80, 30});

    nk::detail::AccessibilityTree tree(window);
    const auto group_id = tree.root_children().at(0);
    const auto button_id = tree.children(group_id).at(0);
    CHECK(tree.info(button_id)->bounds == nk::Rect{40, 40, 80, 30});
    CHECK(tree.hit_test({50, 50}) == button_id);
    CHECK(tree.hit_test({200, 100}) == group_id);
    CHECK_FALSE(tree.hit_test({300, 250}).has_value());

    CHECK_FALSE(tree.focused().has_value());
    CHECK(tree.focus(button_id));
    CHECK(tree.focused() == button_id);
    CHECK(tree.info(button_id)->focused);
}

TEST_CASE("Secure text fields never expose their text as an accessible value",
          "[accessibility][text]") {
    auto field = nk::TextField::create("hunter2");
    CHECK(field->accessible()->value() == "hunter2");
    field->set_secure_text_entry(true);
    CHECK(field->accessible()->value() == "\u2022\u2022\u2022\u2022\u2022\u2022\u2022");
    field->set_text("pa\u00DF");
    CHECK(field->accessible()->value() == "\u2022\u2022\u2022");
    field->set_secure_text_entry(false);
    CHECK(field->accessible()->value() == "pa\u00DF");
}
