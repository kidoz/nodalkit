# Application architecture

This page explains how a NodalKit application is best shaped, what tends to
work well with the toolkit, and what to avoid. NodalKit is still `0.x`, so
treat these as current best practices rather than a frozen framework
contract. For the runnable starting point, see
[Your first NodalKit application](../tutorials/your-first-application.md).

## Start with the right shape

The healthiest NodalKit applications use a simple ownership model:

- your domain logic lives outside the widget tree
- NodalKit owns the window, widgets, menus, dialogs, and interaction surfaces
- one controller or session object coordinates between the UI and the domain

A good high-level split:

- `core/`: application logic, emulation, documents, models, data processing
- `app/`: controller and session APIs that the UI can call
- `frontend/`: NodalKit widgets, layout, dialogs, menu wiring, status surfaces

Avoid putting business logic directly into widget callbacks if that logic
needs to be reused or tested independently. In the tutorial's counter, the
important part is not the counter. The important part is the separation:
the window owns the root widget tree, the widgets render and emit signals,
and the controller owns state changes.

## Build the shell first

Start with one `Application`, one `Window`, one root surface, and one
obvious primary content area. Only add menus, dialogs, settings, and
secondary surfaces after the shell is coherent.

## Keep command handling shared

If the same action can be triggered from a menu item, a button, a keyboard
shortcut, or a native app menu, then all of them should route into the same
handler:

```cpp
void handle_action(std::string_view action) {
    if (action == "file.open") { /* ... */ }
    if (action == "settings.open") { /* ... */ }
}
```

The alternative, where the menu callback implements one version, the button
callback another, and the shortcut callback a third, always drifts.

## Keep widget identity stable

Prefer updating the smallest subtree that changed. Keep a settings dialog
alive and swap only the page body when the selected tab changes; update
labels, models, and image buffers in place. Do not rebuild the whole dialog
every time the user clicks a tab, and do not replace the entire window
content tree for a small state change.

Stable widget identity helps focus behavior, redraw efficiency,
accessibility, and state continuity.

## Use dialogs narrowly

Dialogs are good for short settings flows, destructive confirmation, and
small interruptive tasks. They are a poor default for long multi-step
workflows, large editors, and navigation-heavy flows. If the user needs to
stay in context, prefer in-window UI instead.

## Validate keyboard and focus early

Before polishing visuals, check whether the whole flow works with keyboard
only, whether focus moves in visual order, and whether focus returns
somewhere sensible when menus or dialogs close. If this is broken late in
development, the fix is usually more expensive. The concrete checks are in
[Validate accessibility and keyboard behavior](../how-to/validate-accessibility.md).

## Good practices

- Keep domain logic independent from widgets.
- Use one action path for every command.
- Reuse toolkit primitives instead of app-specific copies when the pattern
  is general.
- Use style classes and theme tokens instead of hardcoded one-off styling.
- Prefer stable containers and local updates over full-tree replacement.
- Treat accessibility names, roles, and keyboard behavior as part of the
  feature, not follow-up work.
- Use the built-in diagnostics before adding temporary debug code.

## Bad practices

- Letting widgets become the domain model.
- Rebuilding whole dialogs or windows for page switches.
- Copying menu structures into multiple incompatible representations.
- Hardcoding layout and color tweaks everywhere in app code.
- Assuming redraw, focus, or accessibility will work without checking.
- Optimizing without measuring.

## Debugging and performance

Before adding ad hoc logging, inspect what the toolkit already gives you:
widget tree dumps, frame diagnostics, render snapshots, trace export, and
diagnostics bundles. They are listed in
[Diagnostics facilities](../reference/diagnostics.md). If a UI bug smells
like layout, redraw, focus, or damage tracking, start there.

## Current constraints

NodalKit is still early. Some parts are already strong, but others are
still moving. Before building a large app, verify the current state of
platform maturity, text input behavior, accessibility backend coverage, and
renderer support for your target OS in
[Platform support](../reference/platform-support.md). Use the `showcase`
example as the fastest broad tour of current capabilities.

## A good stopping point

Your application is using NodalKit well when:

- the domain core still makes sense without the UI
- the UI tree is stable and easy to reason about
- commands are routed through shared handlers
- keyboard, focus, and accessibility are not an afterthought
- diagnostics can explain what the UI is doing

Where the line between application code and the toolkit itself should sit
is discussed in [Toolkit boundaries](toolkit-boundaries.md).
