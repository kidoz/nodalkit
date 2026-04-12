# Building Applications With NodalKit

This guide is for application developers using NodalKit directly.

It focuses on three things:

1. how to structure a real application
2. what tends to work well with the toolkit
3. what to avoid

NodalKit is still `0.x`, so treat these as current best practices rather than a
frozen framework contract.

## Start With The Right Shape

The healthiest NodalKit applications use a simple ownership model:

- your domain logic lives outside the widget tree
- NodalKit owns the window, widgets, menus, dialogs, and interaction surfaces
- one controller/session object coordinates between the UI and the domain

Good high-level split:

- `core/`
  application logic, emulation, documents, models, data processing
- `app/`
  controller/session APIs that the UI can call
- `frontend/`
  NodalKit widgets, layout, dialogs, menu wiring, status surfaces

Avoid putting business logic directly into widget callbacks if that logic needs
to be reused or tested independently.

## Consuming the Installed SDK

NodalKit 0.x ships a single distribution surface: a `pkg-config` file named
`nodalkit.pc` installed alongside the library and headers. There is no CMake
package config module in 0.x — if you need CMake integration, invoke
`pkg-config` from your `CMakeLists.txt`.

After `meson install`, point `PKG_CONFIG_PATH` at the install prefix and use
pkg-config like any other dependency:

```bash
export PKG_CONFIG_PATH="/your/prefix/lib/pkgconfig"
pkg-config --cflags --libs nodalkit
# -I/your/prefix/include -L/your/prefix/lib -lNodalKit
```

For Meson consumers, `dependency('nodalkit')` resolves through the same file:

```meson
nodalkit_dep = dependency('nodalkit', method : 'pkg-config')

executable('my_app', 'main.cpp', dependencies : nodalkit_dep)
```

### Platform notes

- **Linux Wayland (primary target):** NodalKit installs as `libNodalKit.so`.
  Your application must also have the runtime dependencies available at link
  time: `wayland-client`, `xkbcommon`, `freetype2`, `fontconfig`, `harfbuzz`,
  `gio-2.0`. On most distributions these come in the matching `-dev` packages.
- **macOS (secondary target):** NodalKit installs as `libNodalKit.dylib` and
  links against the Cocoa, CoreGraphics, CoreText, Metal, QuartzCore, and
  UniformTypeIdentifiers Apple frameworks. Downstream applications pick these
  up automatically through the dylib.
- **Windows (experimental):** NodalKit currently builds as a **static** library
  (`NodalKit.lib`), not a DLL. Consumers must link statically and must also
  link the same Win32 system libraries NodalKit depends on (`user32`, `gdi32`,
  `comdlg32`, `advapi32`, `dwmapi`, `shcore`, `dwrite`, `d3d11`, `d3dcompiler`,
  `dxgi`). This is a deliberate choice for the experimental phase — the
  static/shared decision on Windows will be revisited before Windows graduates
  from experimental status.

## Minimal Application Skeleton

```cpp
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>

class CounterController {
  public:
    void increment() { ++count_; }
    [[nodiscard]] int count() const { return count_; }

  private:
    int count_ = 0;
};

int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::Window window({.title = "Example", .width = 480, .height = 240});

    CounterController controller;

    auto root = std::make_shared<nk::Widget>();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(12.0F);
    root->set_layout_manager(std::move(layout));

    auto label = nk::Label::create("0");
    auto button = nk::Button::create("Increment");

    (void)button->on_clicked().connect([&] {
        controller.increment();
        label->set_text(std::to_string(controller.count()));
    });

    root->append_child(label);
    root->append_child(button);

    window.set_child(root);
    window.present();
    return app.run();
}
```

The important part is not the counter. The important part is the separation:

- the window owns the root widget tree
- the widgets render and emit signals
- the controller owns state changes

## Recommended Application Pattern

### 1. Build the shell first

Start with:

- one `Application`
- one `Window`
- one root surface
- one obvious primary content area

Only add menus, dialogs, settings, and secondary surfaces after the shell is
coherent.

### 2. Keep command handling shared

If the same action can be triggered from:

- a menu item
- a button
- a keyboard shortcut
- a native app menu

then all of them should route into the same handler.

Good:

```cpp
void handle_action(std::string_view action) {
    if (action == "file.open") { ... }
    if (action == "settings.open") { ... }
}
```

Bad:

- menu callback implements one version
- button callback implements another
- shortcut callback implements a third

That always drifts.

### 3. Keep widget identity stable

Prefer updating the smallest subtree that changed.

Good:

- keep a settings dialog alive
- swap only the page body when the selected tab changes
- update labels, models, and image buffers in place

Bad:

- rebuild the whole dialog every time the user clicks a tab
- replace the entire window content tree for a small state change

Stable widget identity helps:

- focus behavior
- redraw efficiency
- accessibility
- state continuity

### 4. Use dialogs narrowly

Dialogs are good for:

- short settings flows
- destructive confirmation
- small interruptive tasks

Dialogs are a poor default for:

- long multi-step workflows
- large editors
- navigation-heavy flows

If the user needs to stay in context, prefer in-window UI instead.

### 5. Validate keyboard and focus early

Before polishing visuals, check:

- can the whole flow work with keyboard only?
- does focus move in visual order?
- when menus or dialogs close, does focus return somewhere sensible?

If this is broken late in development, the fix is usually more expensive.

## Good Practices

- Keep domain logic independent from widgets.
- Use one action path for every command.
- Reuse toolkit primitives instead of app-specific copies when the pattern is general.
- Use style classes and theme tokens instead of hardcoded one-off styling.
- Prefer stable containers and local updates over full-tree replacement.
- Treat accessibility names, roles, and keyboard behavior as part of the feature, not follow-up work.
- Use the built-in diagnostics before adding temporary debug code.

## Bad Practices

- Letting widgets become the domain model.
- Rebuilding whole dialogs or windows for page switches.
- Copying menu structures into multiple incompatible representations.
- Hardcoding layout and color tweaks everywhere in app code.
- Assuming redraw, focus, or accessibility will work without checking.
- Optimizing without measuring.

## When To Add Something To NodalKit

Add a primitive to NodalKit when:

- you need the same interaction pattern more than once
- the behavior belongs to widget/runtime contracts
- multiple apps would benefit from the same solution

Keep it in app code when:

- it is domain-specific
- it is only one page’s content
- it does not generalize cleanly

Rule of thumb:

- reusable interaction = toolkit
- application-specific workflow = app

## Debugging And Performance

Before adding ad hoc logging, inspect what the toolkit already gives you:

- widget tree dumps
- frame diagnostics
- render snapshots
- trace export
- diagnostics bundles

If a UI bug smells like layout, redraw, focus, or damage tracking, start there.

## Current Constraints

NodalKit is still early. Some parts are already strong, but others are still
moving. Before building a large app, verify the current state of:

- platform maturity
- text input behavior
- accessibility backend coverage
- renderer/backend support
- support matrix for your target OS

Use the `showcase` example as the fastest broad tour of current capabilities.

## A Good Stopping Point

Your application is using NodalKit well when:

- the domain core still makes sense without the UI
- the UI tree is stable and easy to reason about
- commands are routed through shared handlers
- keyboard/focus/accessibility are not an afterthought
- diagnostics can explain what the UI is doing
