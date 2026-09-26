#include "../src/accessibility/accessibility_tree.h"
#include "../src/accessibility/atspi_tree_snapshot.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>
#include <nk/widgets/check_box.h>
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

TEST_CASE("AT-SPI paths embed stable identities as the tree changes", "[accessibility][atspi]") {
    nk::Window window({.title = "Paths", .width = 320, .height = 240});
    auto root = Container::create();
    auto first = nk::Button::create("First");
    auto second = nk::Button::create("Second");
    root->append(first);
    root->append(second);
    window.set_child(root);

    nk::detail::AccessibilityTree tree(window);
    const auto path_of = [&](std::string_view name) {
        for (const auto& entry : nk::detail::build_atspi_tree_nodes(
                 tree, "/org/a11y/atspi/accessible/root", "window0", "Paths", {0, 0, 320, 240})) {
            if (entry.node.name == name) {
                return entry.node.object_path;
            }
        }
        return std::string{};
    };
    const auto second_path = path_of("Second");
    REQUIRE_FALSE(second_path.empty());

    // Removing an earlier sibling must not shift the path of a later one.
    root->remove(*first);
    first.reset();
    CHECK(path_of("Second") == second_path);
    CHECK(path_of("First").empty());

    const auto nodes = nk::detail::build_atspi_tree_nodes(
        tree, "/org/a11y/atspi/accessible/root", "window0", "Paths", {0, 0, 320, 240});
    REQUIRE(nodes.size() == 2);
    CHECK(nodes[0].id == 0);
    CHECK(nodes[0].node.role_name == "frame");
    CHECK(nodes[0].node.parent_path == "/org/a11y/atspi/accessible/root");
    CHECK(nodes[0].node.child_paths == std::vector{second_path});
    CHECK(nodes[1].node.parent_path == nodes[0].node.object_path);
    CHECK(nodes[1].node.role_name == "push button");
    CHECK(nodes[1].node.action_names == std::vector<std::string>{"activate", "focus"});
}

TEST_CASE("AT-SPI snapshots follow modality, enabled state, and text interfaces",
          "[accessibility][atspi]") {
    nk::Window window({.title = "State", .width = 320, .height = 240});
    auto root = Container::create();
    auto save = nk::Button::create("Save");
    auto field = nk::TextField::create();
    root->append(save);
    root->append(field);
    window.set_child(root);
    save->set_sensitive(false);

    nk::detail::AccessibilityTree tree(window);
    auto nodes =
        nk::detail::build_atspi_tree_nodes(tree, "/root", "window0", "State", {0, 0, 320, 240});
    REQUIRE(nodes.size() == 3);
    CHECK_FALSE(nk::has_atspi_state(nodes[1].node.state, nk::AtspiStateBit::Enabled));
    CHECK(nk::has_atspi_state(nodes[1].node.state, nk::AtspiStateBit::Showing));
    CHECK(nk::has_atspi_state(nodes[2].node.state, nk::AtspiStateBit::Enabled));
    CHECK(std::ranges::find(nodes[2].node.interfaces, "org.a11y.atspi.Text") !=
          nodes[2].node.interfaces.end());

    auto dialog = nk::Dialog::create("Confirm");
    dialog->present(window);
    nodes = nk::detail::build_atspi_tree_nodes(tree, "/root", "window0", "State", {0, 0, 320, 240});
    REQUIRE(nodes.size() == 2);
    CHECK(nodes[1].node.role_name == "dialog");
    dialog->close();
}

TEST_CASE("AT-SPI text offsets count characters", "[accessibility][atspi][text]") {
    const std::string text = "caf\u00E9 \u043C\u0438\u0440 \U0001F600!";
    CHECK(nk::detail::atspi_character_count(text) == 11);
    CHECK(nk::detail::atspi_text_slice(text, 3, 4) == "\u00E9");
    CHECK(nk::detail::atspi_text_slice(text, 5, 8) == "\u043C\u0438\u0440");
    CHECK(nk::detail::atspi_text_slice(text, 9, -1) == "\U0001F600!");
    CHECK(nk::detail::atspi_text_slice(text, -5, 2) == "ca");
    CHECK(nk::detail::atspi_text_slice(text, 8, 99) == " \U0001F600!");
    CHECK(nk::detail::atspi_text_slice(text, 7, 3).empty());
    CHECK(nk::detail::atspi_action_from_name("toggle") == nk::AccessibleAction::Toggle);
    CHECK_FALSE(nk::detail::atspi_action_from_name("click").has_value());
}

TEST_CASE("AT-SPI nodes carry wire roles, state sets, and automation ids",
          "[accessibility][atspi]") {
    using nk::detail::AtspiRoleValue;
    using nk::detail::AtspiStateValue;
    using nk::detail::has_atspi_state;
    nk::Window window({.title = "Roles", .width = 320, .height = 240});
    auto root = Container::create();
    auto mute = nk::CheckBox::create("Mute");
    mute->set_debug_name("mute-check");
    auto field = nk::TextField::create("query");
    root->append(mute);
    root->append(field);
    window.set_child(root);
    mute->set_checked(true);

    nk::detail::AccessibilityTree tree(window);
    auto nodes = nk::detail::build_atspi_tree_nodes(
        tree, "/root", "window0", "Roles", {0, 0, 320, 240}, true);
    REQUIRE(nodes.size() == 3);
    CHECK(nodes[0].role == AtspiRoleValue::Frame);
    CHECK(has_atspi_state(nodes[0].states, AtspiStateValue::Active));
    CHECK(nodes[1].role == AtspiRoleValue::CheckBox);
    CHECK(nodes[1].accessible_id == "mute-check");
    CHECK(has_atspi_state(nodes[1].states, AtspiStateValue::Checkable));
    CHECK(has_atspi_state(nodes[1].states, AtspiStateValue::Checked));
    CHECK(has_atspi_state(nodes[1].states, AtspiStateValue::Enabled));
    CHECK(nodes[2].role == AtspiRoleValue::Entry);
    CHECK(nodes[2].node.role_name == "entry");
    CHECK(has_atspi_state(nodes[2].states, AtspiStateValue::Editable));
    CHECK(has_atspi_state(nodes[2].states, AtspiStateValue::SingleLine));
    CHECK_FALSE(has_atspi_state(nodes[2].states, AtspiStateValue::Focused));

    field->grab_focus();
    mute->set_checked(false);
    nodes = nk::detail::build_atspi_tree_nodes(tree, "/root", "window0", "Roles", {0, 0, 320, 240});
    CHECK_FALSE(has_atspi_state(nodes[0].states, AtspiStateValue::Active));
    CHECK_FALSE(has_atspi_state(nodes[1].states, AtspiStateValue::Checked));
    CHECK(has_atspi_state(nodes[2].states, AtspiStateValue::Focused));
    // Bit 41 lives in the second word of the set.
    CHECK(nodes[1].states[1] == 1U << 9U);
}

TEST_CASE("AT-SPI event diff reports focus, state, children, names, and windows",
          "[accessibility][atspi]") {
    using nk::detail::AtspiEvent;
    nk::Window window({.title = "Events", .width = 320, .height = 240});
    auto root = Container::create();
    auto first = nk::Button::create("First");
    auto second = nk::Button::create("Second");
    auto mute = nk::CheckBox::create("Mute");
    root->append(first);
    root->append(second);
    root->append(mute);
    window.set_child(root);
    first->grab_focus();

    nk::detail::AccessibilityTree tree(window);
    const auto build = [&](bool active) {
        return nk::detail::build_atspi_tree_nodes(
            tree, "/root", "window0", "Events", {0, 0, 320, 240}, active);
    };
    const auto before = build(false);
    const auto first_path = before[1].node.object_path;
    const auto second_path = before[2].node.object_path;
    const auto mute_path = before[3].node.object_path;
    CHECK(nk::detail::diff_atspi_nodes(before, before).empty());

    second->grab_focus();
    mute->set_checked(true);
    first->set_label("Renamed");
    auto events = nk::detail::diff_atspi_nodes(before, build(true));
    const auto index_of = [&](std::string_view path, std::string_view detail, int value) {
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (events[i].object_path == path && events[i].detail == detail &&
                events[i].detail1 == value) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    REQUIRE(index_of(first_path, "focused", 0) >= 0);
    REQUIRE(index_of(second_path, "focused", 1) >= 0);
    CHECK(index_of(first_path, "focused", 0) < index_of(second_path, "focused", 1));
    CHECK(index_of(mute_path, "checked", 1) >= 0);
    CHECK(index_of(first_path, "accessible-name", 0) >= 0);
    CHECK(events[static_cast<std::size_t>(index_of(first_path, "accessible-name", 0))].data_value ==
          "Renamed");
    CHECK(index_of("/root/window0", "active", 1) >= 0);
    CHECK(std::ranges::any_of(events, [](const AtspiEvent& event) {
        return event.interface_name == "org.a11y.atspi.Event.Window" &&
               event.member == "Activate" && event.object_path == "/root/window0";
    }));

    const auto with_second = build(true);
    root->remove(*second);
    second.reset();
    auto added = nk::Button::create("Added");
    root->append(added);
    events = nk::detail::diff_atspi_nodes(with_second, build(true));
    REQUIRE(events.size() >= 2);
    CHECK(events[0].member == "ChildrenChanged");
    CHECK(events[0].detail == "remove");
    CHECK(events[0].detail1 == 1);
    CHECK(events[0].data == AtspiEvent::Data::Object);
    CHECK(events[0].data_value == second_path);
    CHECK(events[1].detail == "add");
    CHECK(events[1].detail1 == 2);
    CHECK(events[1].object_path == "/root/window0");
}

TEST_CASE("AT-SPI text events carry only the changed characters", "[accessibility][atspi][text]") {
    using nk::detail::AtspiEvent;
    nk::Window window({.title = "Text", .width = 320, .height = 240});
    auto root = Container::create();
    auto field = nk::TextField::create("caf");
    root->append(field);
    window.set_child(root);
    nk::detail::AccessibilityTree tree(window);
    const auto build = [&] {
        return nk::detail::build_atspi_tree_nodes(
            tree, "/root", "window0", "Text", {0, 0, 320, 240});
    };

    auto before = build();
    field->set_text("caf\u00E9");
    auto events = nk::detail::diff_atspi_nodes(before, build());
    REQUIRE(events.size() == 1);
    CHECK(events[0].member == "TextChanged");
    CHECK(events[0].detail == "insert");
    CHECK(events[0].detail1 == 3);
    CHECK(events[0].detail2 == 1);
    CHECK(events[0].data == AtspiEvent::Data::Text);
    CHECK(events[0].data_value == "\u00E9");

    before = build();
    field->set_text("c\u00E9");
    events = nk::detail::diff_atspi_nodes(before, build());
    REQUIRE(events.size() == 1);
    CHECK(events[0].detail == "delete");
    CHECK(events[0].detail1 == 1);
    CHECK(events[0].detail2 == 2);
    CHECK(events[0].data_value == "af");

    before = build();
    field->set_text("x\u00E9");
    events = nk::detail::diff_atspi_nodes(before, build());
    REQUIRE(events.size() == 2);
    CHECK(events[0].detail == "delete");
    CHECK(events[0].data_value == "c");
    CHECK(events[1].detail == "insert");
    CHECK(events[1].detail1 == 0);
    CHECK(events[1].data_value == "x");
}
