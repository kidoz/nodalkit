# Validate accessibility and keyboard behavior

Dense tool UIs such as settings dialogs, binding lists, and log panels are
where accessibility regressions hide. NodalKit populates an accessibility
model ([`nk/accessibility/accessible.h`](../../include/nk/accessibility/accessible.h),
roles in [`role.h`](../../include/nk/accessibility/role.h)) and bridges it
to AT-SPI on Linux and NSAccessibility on macOS. Use this checklist to
validate a screen before shipping it.

See [`examples/accessibility_probe.cpp`](../../examples/accessibility_probe.cpp)
for a worked example of labels, relations, and focus order.

## Menus and shortcuts

- [ ] Every menu command has a keyboard shortcut or is reachable by menu
      navigation alone.
- [ ] Shortcuts are shown in the menu item text.
- [ ] Accelerators do not collide within a menu.

## Dialog focus order

- [ ] Opening a dialog moves focus into it (to the first field or a sensible
      default).
- [ ] `Tab` / `Shift+Tab` walk controls in visual order.
- [ ] The default and cancel actions are keyboard-triggerable (`Enter` /
      `Esc`).
- [ ] Closing the dialog returns focus to the control that opened it.

## Tree, table, and list navigation

- [ ] Arrow keys move the selection; `Home`/`End` jump to ends; `PageUp`/
      `PageDown` page.
- [ ] The focused row/cell is visually distinct from the selected one.
- [ ] Expand/collapse (trees) works from the keyboard.
- [ ] Virtualized views (e.g. `ListView`, `LogView`) keep the accessible node
      for the focused row present even when scrolled.

## Accessible names for custom controls

- [ ] Every interactive widget has a non-empty accessible name
      (`ensure_accessible().set_name(...)`), not just visible text.
- [ ] Icon-only buttons set an explicit name.
- [ ] Inputs are tied to their labels with a `LabelledBy` relation.
- [ ] Controls that drive another region use a `Controls` relation.
- [ ] Custom controls set an appropriate `AccessibleRole` (see `role.h`).

## Status and log regions

- [ ] Status bars and log drawers expose their text to assistive tech.
- [ ] Important state changes (run started/stopped, error) are reflected in an
      accessible name/value, not only color.
- [ ] Severity in a `LogView` is conveyed by more than color (text/prefix), so
      it survives for low-vision users.

## Run the validation passes

- **Keyboard-only pass:** unplug the mouse and complete the primary flow.
- **Linux / AT-SPI:** run under Accerciser or Orca and confirm names, roles,
  and focus order match the checklist.
- **macOS:** inspect the window with Accessibility Inspector (Xcode → Open
  Developer Tool), then walk the flow with VoiceOver (`Cmd+F5`). Widget debug
  names appear as each element's identifier, which UI automation can target.
- **Debug dump:** `format_widget_debug_tree` (see
  [Diagnostics facilities](../reference/diagnostics.md)) prints each node's
  role, name, state, and relations. Diff it against expectations in a test.

Platform coverage of the accessibility bridges is recorded in
[Platform support](../reference/platform-support.md) and the
[Windows support matrix](../reference/windows-support-matrix.md).
