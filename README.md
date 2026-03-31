# NodalKit

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Meson](https://img.shields.io/badge/Build-Meson-blueviolet.svg)](https://mesonbuild.com/)
[![Platform: Linux | macOS](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)]()

A C++-first GUI toolkit for modern desktop applications.

NodalKit is an MIT-licensed alternative for C++ developers who might otherwise
reach for Qt, offering a GTK4-inspired architecture with a productive,
modern C++23 API. No meta-object compiler, no code generation, no build magic.

## Status

**0.1.0** — Early scaffold. The module structure, public API headers, and a
buildable skeleton are in place. Core systems (signals, properties, widget tree,
layout, model/view) have stub implementations with passing tests. The software
renderer and platform backends are minimal.

## Quick Start

```bash
# Requirements: Meson >= 0.60, a C++23 compiler
meson setup buildDir
meson compile -C buildDir
meson test -C buildDir
```

Run the examples:

```bash
./buildDir/examples/hello_world
./buildDir/examples/counter
./buildDir/examples/showcase
```

## Architecture at a Glance

| Module | Responsibility |
|--------|----------------|
| **foundation** | Types, Result, Signal/Slot, Property, Logging |
| **runtime** | Event loop, timers, task posting |
| **platform** | Application, Window, input events |
| **render** | Render node tree, software renderer |
| **ui_core** | Widget base, tree, focus, state flags |
| **controllers** | Pointer, Keyboard, Focus controllers |
| **layout** | Measure/allocate pipeline, BoxLayout |
| **style** | CSS-like selectors, themes, tokens |
| **accessibility** | Roles, names, states (WAI-ARIA) |
| **model** | List models, selection, item factories |
| **widgets** | Label, Button, TextField, ScrollArea, ListView, Dialog, ComboBox, ImageView, MenuBar, StatusBar |
| **actions** | Named actions, shortcuts, action groups |

## Counter Example

```cpp
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>

int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::Window window({.title = "Counter", .width = 420, .height = 220});

    int count = 0;
    auto label = nk::Label::create("0");
    auto button = nk::Button::create("Increment");

    auto conn = button->on_clicked().connect([&] {
        label->set_text(std::to_string(++count));
    });

    auto root = /* vertical box container */;
    root->append(label);
    root->append(button);

    window.set_child(root);
    window.present();
    return app.run();
}
```

## Design Highlights

- **Type-safe signals** via C++23 templates — no MOC, no macros
- **Property binding** with change notification
- **GTK4-style layout** — measure/allocate with pluggable layout managers
- **CSS-like theming** — selectors, style classes, pseudo-states, tokens
- **Accessibility model** — WAI-ARIA roles and states (platform bridges planned)
- **Async dialogs** — non-blocking present(), response via signals (WIP)
- **Model/view** — AbstractListModel + SelectionModel + ItemFactory

## Author

**Aleksandr Pavlov** — [ckidoz@gmail.com](mailto:ckidoz@gmail.com)

## License

MIT. See [LICENSE](LICENSE).
