# Share actions between controls and shortcuts

Use one `nk::Action` for each command and an `nk::ActionGroup` to look it up
by name. Buttons, toolkit menus, command palettes, and window shortcuts can
share those actions. The runnable example is
[`examples/shared_actions.cpp`](../../examples/shared_actions.cpp):

```bash
meson compile -C buildDir shared_actions
./buildDir/examples/shared_actions
```

## Create and bind a command

The following code assumes an existing `nk::Window window` and retains the
activation connection for the command's lifetime:

```cpp
auto actions = std::make_shared<nk::ActionGroup>();
auto save = std::make_shared<nk::Action>("file.save");
actions->add(save);
auto saved = save->on_activated().connect([] { /* save the document */ });

auto button = nk::Button::create("Save");
button->set_action(save);

auto menu = nk::MenuBar::create();
menu->add_menu({"File", {nk::MenuItem::action("Save", "file.save")}});
menu->set_action_group(actions);

auto palette = nk::CommandPalette::create();
palette->set_commands({{.id = "file.save", .title = "Save",
                       .subtitle = {}, .category = "File"}});
palette->set_action_group(actions);

window.set_action_group(actions);
#ifdef __APPLE__
constexpr auto CommandModifier = nk::Modifiers::Super;
#else
constexpr auto CommandModifier = nk::Modifiers::Ctrl;
#endif
window.add_shortcut({{static_cast<int>(nk::KeyCode::S), CommandModifier}, "file.save"});
```

Include `nk/actions/action.h`, `nk/platform/window.h`, and the corresponding
widget headers. Attach the controls to the window's widget tree as in the
example. Button labels and palette titles remain application-owned.

Bound controls activate their action directly. They do **not** emit
`Button::on_clicked()`, `MenuBar::on_action()`, or
`CommandPalette::on_command_activated()`. Observe `Action::on_activated()`
for command behavior; use the control signals for unbound controls.

## Update shared state

`save->set_enabled(false)` immediately disables bound buttons and menu and
palette entries. The shortcut consumes its matching key without activating
the command. An item's own `enabled = false` also blocks activation, even
when its action is enabled. Missing registry entries disable named menu and
palette items.

For a toggle command, call `set_checkable(true)`. Activation toggles its
checked state before emitting `on_activated()`. `set_checked()` changes state
without activating; it has an effect only on checkable actions. Bound
buttons expose toggle-button accessibility semantics and checked state;
buttons, menus, and palette entries display a check mark when checked.

`Action::on_changed()` reports effective state changes.
`ActionGroup::on_changed()` reports the affected name after a state change,
addition, replacement, or removal. Perform registry and action mutations on
the UI thread. For worker results, post the update through the event loop.

Groups, controls, and windows retain shared ownership of their bindings.
`ActionGroup::add()` replaces an existing action with the same name;
`remove()` removes the registry entry. Name-based consumers use the current
entry. A button holds its specific action until `set_action()` rebinds it.
Passing null to a binding setter unbinds it; menu and palette entries regain
their declared state, and buttons regain sensitivity and clear checked state.

## Restrict shortcuts to a focus subtree

Pass a widget as the second argument to `window.add_shortcut(binding, scope)`
to activate only while focus is within that subtree. The scope must belong
to that window, and it and its ancestors must be visible and sensitive.
The window keeps a weak reference, so registration does not keep pages alive.

The closest scope to the focused widget wins. Equal scopes use the most
recent registration. A disabled or missing command in the winning binding
consumes the key rather than activating a less specific command.

A visible modal dialog blocks window-wide bindings and scopes outside its
subtree. A binding scoped to that dialog can still run. Use
`clear_shortcuts()` to remove all registrations.

## Key matching and dispatch

`Shortcut::key_code` is an `nk::KeyCode` converted to `int`, not an ASCII
value or native scan code. Shortcuts match the translated key and exact
modifier combination on key press. Release events do not activate commands.
Auto-repeat is suppressed by default; set `ShortcutBinding::allow_repeat`
to opt in.

Installed bindings run before widget key handlers. Built-in inspector
shortcuts take precedence. A consumed key does not also activate a focused
button or other widget handler.

This API binds toolkit menu and toolbar widgets. The native application-menu
and native toolbar bridge APIs have their own models and callbacks; assigning
a window action group does not bind those native models or install their
shortcut descriptors.
