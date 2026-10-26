#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/accessibility/role.h>
#include <nk/actions/action.h>
#include <nk/actions/shortcut.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/window.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/command_palette.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/menu_bar.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
nk::ShortcutBinding save_binding(std::string name = "file.save") {
    return {{static_cast<int>(nk::KeyCode::S), nk::Modifiers::Ctrl}, std::move(name)};
}

nk::KeyEvent save_key(bool repeat = false) {
    return {.type = nk::KeyEvent::Type::Press,
            .key = nk::KeyCode::S,
            .modifiers = nk::Modifiers::Ctrl,
            .is_repeat = repeat};
}

void choose_first_menu_item(nk::MenuBar& menu) {
    const nk::KeyEvent down{.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down};
    CHECK(menu.handle_key_event(down));
    CHECK(menu.handle_key_event(down));
    CHECK(menu.handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
}
} // namespace

TEST_CASE("Action state is observable and checkable activation toggles before notification",
          "[actions]") {
    nk::Action action("view.details");
    int changes = 0;
    int activations = 0;
    auto changed = action.on_changed().connect([&] { ++changes; });
    auto activated = action.on_activated().connect([&] {
        ++activations;
        CHECK(action.is_checked());
    });
    action.set_enabled(true);
    action.set_checked(true);
    CHECK(changes == 0);
    CHECK_FALSE(action.is_checked());
    action.set_checkable(true);
    action.set_checkable(true);
    CHECK(changes == 1);
    action.activate();
    CHECK(changes == 2);
    CHECK(activations == 1);
    action.set_enabled(false);
    action.activate();
    CHECK(action.is_checked());
    CHECK(activations == 1);
    action.set_checkable(false);
    CHECK_FALSE(action.is_checked());
    CHECK(changes == 4);
}

TEST_CASE("Action groups follow replacement and safely retain an activating command",
          "[actions][lifetime]") {
    auto group = std::make_shared<nk::ActionGroup>();
    CHECK_THROWS_AS(group->add(nullptr), std::invalid_argument);
    std::vector<std::string> notifications;
    auto changed = group->on_changed().connect(
        [&](std::string_view name) { notifications.emplace_back(name); });
    auto old = std::make_shared<nk::Action>("file.save");
    group->add(old);
    auto replacement = std::make_shared<nk::Action>("file.save");
    group->add(replacement);
    old->set_enabled(false);
    REQUIRE(notifications.size() == 2);
    replacement->set_enabled(false);
    CHECK(notifications.size() == 3);
    replacement->set_enabled(true);
    auto activated = replacement->on_activated().connect([&] {
        group->remove("file.save");
        replacement.reset();
    });
    std::weak_ptr<nk::Action> weak = replacement;
    group->activate("file.save");
    CHECK(weak.expired());
    CHECK(group->lookup("file.save") == nullptr);
    CHECK(notifications.back() == "file.save");
    group->activate("missing");
}

TEST_CASE("Destroying a checkable action from a state observer cancels further delivery",
          "[actions][lifetime]") {
    auto action = std::make_unique<nk::Action>("view.details");
    action->set_checkable(true);
    int activations = 0;
    auto changed = action->on_changed().connect([&] { action.reset(); });
    auto activated = action->on_activated().connect([&] { ++activations; });
    action->activate();
    CHECK_FALSE(action);
    CHECK(activations == 0);
}

TEST_CASE("Menus toolbar buttons palette and shortcuts share one activation path",
          "[actions][window]") {
    nk::Window window({.title = "Shared commands"});
    auto actions = std::make_shared<nk::ActionGroup>();
    auto save = std::make_shared<nk::Action>("file.save");
    actions->add(save);
    int calls = 0;
    int legacy_calls = 0;
    auto activated = save->on_activated().connect([&] { ++calls; });
    auto toolbar = nk::Button::create("Save");
    toolbar->set_action(save);
    auto clicked = toolbar->on_clicked().connect([&] { ++legacy_calls; });
    auto menu = nk::MenuBar::create();
    menu->add_menu({"File", {nk::MenuItem::action("Save", "file.save")}});
    menu->set_action_group(actions);
    auto menu_activated = menu->on_action().connect([&](std::string_view) { ++legacy_calls; });
    auto palette = nk::CommandPalette::create();
    palette->set_commands({{.id = "file.save", .title = "Save", .subtitle = {}, .category = {}}});
    palette->set_action_group(actions);
    auto palette_activated =
        palette->on_command_activated().connect([&](std::string_view) { ++legacy_calls; });
    auto root = nk::Box::create();
    root->append(menu);
    root->append(toolbar);
    root->append(palette);
    window.set_child(root);
    window.set_action_group(actions);
    window.add_shortcut(save_binding());

    choose_first_menu_item(*menu);
    CHECK(calls == 1);
    toolbar->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    CHECK(calls == 2);
    palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    CHECK(calls == 3);
    toolbar->grab_focus();
    window.dispatch_key_event(save_key());
    CHECK(calls == 4);
    CHECK(legacy_calls == 0);

    save->set_enabled(false);
    CHECK_FALSE(toolbar->is_sensitive());
    REQUIRE_FALSE(palette->commands()[0].enabled);
    choose_first_menu_item(*menu);
    toolbar->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    window.dispatch_key_event(save_key());
    CHECK(calls == 4);

    save->set_enabled(true);
    save->set_checkable(true);
    save->set_checked(true);
    CHECK(nk::has_flag(toolbar->state_flags(), nk::StateFlags::Checked));
    CHECK(toolbar->ensure_accessible().role() == nk::AccessibleRole::ToggleButton);
    CHECK(palette->commands()[0].checked);
    window.dispatch_key_event(save_key());
    CHECK(calls == 5);
    CHECK_FALSE(nk::has_flag(toolbar->state_flags(), nk::StateFlags::Checked));
    CHECK_FALSE(palette->commands()[0].checked);
}

TEST_CASE("Bindings restore declared state and disconnect when rebound or destroyed",
          "[actions][lifetime]") {
    auto actions = std::make_shared<nk::ActionGroup>();
    auto action = std::make_shared<nk::Action>("save");
    action->set_enabled(false);
    actions->add(action);
    auto palette = nk::CommandPalette::create();
    palette->set_commands(
        {{.id = "save", .title = "Save", .subtitle = {}, .category = {}},
         {.id = "missing", .title = "Missing", .subtitle = {}, .category = {}},
         {.id = "save", .title = "Unavailable", .subtitle = {}, .category = {}, .enabled = false}});
    palette->set_action_group(actions);
    CHECK_FALSE(palette->commands()[0].enabled);
    action->set_enabled(true);
    CHECK(palette->commands()[0].enabled);
    CHECK_FALSE(palette->commands()[1].enabled);
    CHECK_FALSE(palette->commands()[2].enabled);
    actions->remove("save");
    CHECK_FALSE(palette->commands()[0].enabled);
    auto replacement = std::make_shared<nk::Action>("save");
    replacement->set_checkable(true);
    replacement->set_checked(true);
    actions->add(replacement);
    CHECK(palette->commands()[0].checked);
    CHECK(palette->commands()[0].enabled);
    palette->set_action_group(nullptr);
    CHECK(palette->commands()[0].enabled);
    CHECK(palette->commands()[1].enabled);
    CHECK_FALSE(palette->commands()[0].checked);
    CHECK_FALSE(palette->commands()[2].enabled);

    auto button = nk::Button::create();
    button->set_action(replacement);
    button->set_action(action);
    replacement->set_enabled(false);
    CHECK(button->is_sensitive());
    button->set_action(nullptr);
    action->set_enabled(false);
    CHECK(button->is_sensitive());
    button.reset();
    palette.reset();
    action->set_enabled(true);
    actions->add(replacement);
}

TEST_CASE("Shortcuts use exact modifiers translated key values and opt-in repeat", "[actions]") {
    nk::Window window({.title = "Shortcuts"});
    auto actions = std::make_shared<nk::ActionGroup>();
    auto save = std::make_shared<nk::Action>("file.save");
    actions->add(save);
    window.set_action_group(actions);
    int calls = 0;
    auto activated = save->on_activated().connect([&] { ++calls; });
    window.add_shortcut(save_binding());
    auto key = save_key();
    key.modifiers = nk::Modifiers::None;
    window.dispatch_key_event(key);
    key.modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift;
    window.dispatch_key_event(key);
    key = save_key();
    key.type = nk::KeyEvent::Type::Release;
    window.dispatch_key_event(key);
    window.dispatch_key_event(save_key(true));
    CHECK(calls == 0);
    window.dispatch_key_event(save_key());
    CHECK(calls == 1);
    window.clear_shortcuts();
    auto repeat = save_binding();
    repeat.allow_repeat = true;
    window.add_shortcut(repeat);
    window.dispatch_key_event(save_key(true));
    CHECK(calls == 2);
    window.clear_shortcuts();
    window.dispatch_key_event(save_key());
    CHECK(calls == 2);
}

TEST_CASE("Focus scopes choose nearest binding and suppress disabled-command fallback",
          "[actions][focus]") {
    nk::Window window({.title = "Scoped shortcuts"});
    auto actions = std::make_shared<nk::ActionGroup>();
    auto global = std::make_shared<nk::Action>("global");
    auto local = std::make_shared<nk::Action>("local");
    actions->add(global);
    actions->add(local);
    window.set_action_group(actions);
    int global_calls = 0;
    int local_calls = 0;
    auto global_activated = global->on_activated().connect([&] { ++global_calls; });
    auto local_activated = local->on_activated().connect([&] { ++local_calls; });
    auto root = nk::Box::create();
    auto editor = nk::Box::create();
    auto inside = nk::Button::create("Editor");
    auto outside = nk::Button::create("Sidebar");
    editor->append(inside);
    root->append(editor);
    root->append(outside);
    window.set_child(root);
    window.add_shortcut(save_binding("global"));
    window.add_shortcut(save_binding("global"), root);
    window.add_shortcut(save_binding("local"), editor);
    inside->grab_focus();
    window.dispatch_key_event(save_key());
    CHECK(local_calls == 1);
    CHECK(global_calls == 0);
    local->set_enabled(false);
    window.dispatch_key_event(save_key());
    CHECK(global_calls == 0);
    outside->grab_focus();
    window.dispatch_key_event(save_key());
    CHECK(global_calls == 1);
    local->set_enabled(true);
    inside->grab_focus();
    window.add_shortcut(save_binding("global"), editor);
    window.dispatch_key_event(save_key());
    CHECK(global_calls == 2);
    editor->remove(*inside);
    window.dispatch_key_event(save_key());
    CHECK(global_calls == 3);
    root->remove(*editor);
    editor.reset();
    window.dispatch_key_event(save_key());
    CHECK(global_calls == 4);
}

TEST_CASE("A modal blocks covered shortcuts but permits its own focused scope",
          "[actions][modal]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Modal commands"});
    auto actions = std::make_shared<nk::ActionGroup>();
    auto action = std::make_shared<nk::Action>("file.save");
    actions->add(action);
    window.set_action_group(actions);
    window.add_shortcut(save_binding());
    int calls = 0;
    auto activated = action->on_activated().connect([&] { ++calls; });
    window.dispatch_key_event(save_key());
    REQUIRE(calls == 1);
    auto dialog = nk::Dialog::create("Confirm", "Modal");
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    dialog->present(window);
    window.dispatch_key_event(save_key());
    CHECK(calls == 1);
    window.add_shortcut(save_binding(), dialog);
    window.dispatch_key_event(save_key());
    CHECK(calls == 2);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
    window.dispatch_key_event(save_key());
    CHECK(calls == 3);
}

TEST_CASE("Shortcut callbacks may clear bindings replace registries and destroy their window",
          "[actions][lifetime]") {
    auto window = std::make_unique<nk::Window>(nk::WindowConfig{.title = "Lifetime"});
    auto actions = std::make_shared<nk::ActionGroup>();
    auto action = std::make_shared<nk::Action>("file.save");
    actions->add(action);
    window->set_action_group(actions);
    window->add_shortcut(save_binding());
    int calls = 0;
    auto activated = action->on_activated().connect([&] {
        ++calls;
        window->clear_shortcuts();
        window->set_action_group(nullptr);
        actions->remove("file.save");
        window.reset();
    });
    window->dispatch_key_event(save_key());
    CHECK(calls == 1);
    CHECK_FALSE(window);
}

TEST_CASE("A matched shortcut consumes keys before focused widget activation", "[actions][focus]") {
    nk::Window window({.title = "Shortcut consumption"});
    auto button = nk::Button::create("Focused button");
    window.set_child(button);
    button->grab_focus();
    int clicks = 0;
    auto clicked = button->on_clicked().connect([&] { ++clicks; });
    auto actions = std::make_shared<nk::ActionGroup>();
    auto action = std::make_shared<nk::Action>("run");
    actions->add(action);
    window.set_action_group(actions);
    window.add_shortcut({{static_cast<int>(nk::KeyCode::Return), nk::Modifiers::None}, "run"});
    int calls = 0;
    auto activated = action->on_activated().connect([&] { ++calls; });
    const nk::KeyEvent enter{.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return};
    window.dispatch_key_event(enter);
    CHECK(calls == 1);
    CHECK(clicks == 0);
    action->set_enabled(false);
    window.dispatch_key_event(enter);
    actions->remove("run");
    window.dispatch_key_event(enter);
    CHECK(calls == 1);
    CHECK(clicks == 0);
    window.clear_shortcuts();
    window.dispatch_key_event(enter);
    CHECK(clicks == 1);
}

TEST_CASE("Bound controls tolerate destructive activation callbacks", "[actions][lifetime]") {
    auto actions = std::make_shared<nk::ActionGroup>();
    auto action = std::make_shared<nk::Action>("run");
    actions->add(action);
    int calls = 0;
    SECTION("Menu can replace its model and destroy itself") {
        auto menu = nk::MenuBar::create();
        menu->add_menu({"Commands", {nk::MenuItem::action("Run", "run")}});
        menu->set_action_group(actions);
        auto activated = action->on_activated().connect([&] {
            ++calls;
            menu->clear();
            menu.reset();
        });
        choose_first_menu_item(*menu);
        CHECK_FALSE(menu);
    }
    SECTION("Palette can replace its commands and destroy itself") {
        auto palette = nk::CommandPalette::create();
        palette->set_commands({{.id = "run", .title = "Run", .subtitle = {}, .category = {}}});
        palette->set_action_group(actions);
        auto activated = action->on_activated().connect([&] {
            ++calls;
            palette->set_commands({});
            palette.reset();
        });
        palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
        CHECK_FALSE(palette);
    }
    SECTION("Button can destroy itself") {
        auto button = nk::Button::create("Run");
        button->set_action(action);
        auto activated = action->on_activated().connect([&] {
            ++calls;
            button.reset();
        });
        button->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
        CHECK_FALSE(button);
    }
    CHECK(calls == 1);
}

TEST_CASE("Disabling a bound button cancels its armed pointer activation", "[actions][pointer]") {
    auto action = std::make_shared<nk::Action>("run");
    auto button = nk::Button::create("Run");
    button->allocate({0, 0, 100, 40});
    button->set_action(action);
    int calls = 0;
    auto activated = action->on_activated().connect([&] { ++calls; });
    REQUIRE(button->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 10, .y = 10, .button = 1}));
    action->set_enabled(false);
    action->set_enabled(true);
    button->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 10, .y = 10, .button = 1});
    CHECK(calls == 0);
}
