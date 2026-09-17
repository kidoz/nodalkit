# Your first NodalKit application

In this tutorial you build the NodalKit SDK, install it to a local prefix,
and write a small windowed application that consumes it. By the end you have
a working counter with its state kept outside the widget tree, which is the
shape every larger NodalKit application grows from.

You need about twenty minutes, a C++23 compiler, Meson 1.11 or newer, and
`pkg-config` or `pkgconf`. On Linux Wayland you also need the development
packages for `wayland-client`, `xkbcommon`, `freetype2`, `fontconfig`,
`harfbuzz`, and `gio-2.0`. See [Platform support](../reference/platform-support.md)
for the other platforms.

## 1. Build and install the SDK

Clone the repository, then configure a build that installs into a `.local`
directory next to it:

```bash
git clone <nodalkit-repository-url> nodalkit
cd nodalkit
meson setup buildDir --prefix="$PWD/.local" --libdir=lib
meson compile -C buildDir
meson install -C buildDir
```

The install step writes the headers, the library, and a `pkg-config` file
named `nodalkit.pc`. Tell `pkg-config` where to find it and confirm it
resolves:

```bash
export PKG_CONFIG_PATH="$PWD/.local/lib/pkgconfig"
pkg-config --cflags --libs nodalkit
```

You should see an include path, a library path, and `-lNodalKit`.

## 2. Create the project

Make a new directory outside the NodalKit tree and add a `meson.build`:

```meson
project('counter', 'cpp',
    default_options : ['cpp_std=c++23,c++latest', 'warning_level=3'])

nodalkit_dep = dependency('nodalkit', method : 'pkg-config')

executable('counter', 'main.cpp', dependencies : nodalkit_dep)
```

The ordered `c++23,c++latest` value picks the exact C++23 mode on compilers
that name it directly and falls back to `/std:c++latest` on MSVC-style
front ends. The dependency line resolves through the `nodalkit.pc` file you
just installed.

## 3. Write the application

Create `main.cpp` with the following contents:

```cpp
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>

#include <memory>
#include <string>

// Application state lives outside the widget tree.
class CounterController {
public:
    void increment() { ++count_; }
    [[nodiscard]] int count() const { return count_; }

private:
    int count_ = 0;
};

// nk::Widget's constructor is protected, so a container is a small subclass.
class Column : public nk::Widget {
public:
    static std::shared_ptr<Column> create(float spacing = 12.0F) {
        auto column = std::shared_ptr<Column>(new Column());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(spacing);
        column->set_layout_manager(std::move(layout));
        return column;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    Column() = default;
};

int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::Window window({.title = "Counter", .width = 420, .height = 220});

    CounterController controller;

    auto root = Column::create();
    auto label = nk::Label::create("0");
    auto button = nk::Button::create("Increment");

    auto connection = button->on_clicked().connect([&] {
        controller.increment();
        label->set_text(std::to_string(controller.count()));
    });
    (void)connection;

    root->append(label);
    root->append(button);

    window.set_child(root);
    window.present();
    return app.run();
}
```

Read it top to bottom once. Three things are happening:

- `CounterController` owns the state. It knows nothing about widgets.
- `Column` is the root container. It wraps a `BoxLayout` so children stack
  vertically. Widgets are created through `create()` factories and shared
  pointers because the tree owns them.
- The click handler is the only place the UI and the controller meet. It
  asks the controller to change state, then pushes the new value into the
  label.

## 4. Build and run

With `PKG_CONFIG_PATH` still set from step 1:

```bash
meson setup build
meson compile -C build
./build/counter
```

A window titled "Counter" opens with a label reading `0` and an
**Increment** button. Each click increases the number. Close the window to
exit; `app.run()` returns and the program ends.

## 5. What you learned

- The installed SDK is consumed through `pkg-config`; a Meson project needs
  one `dependency()` line.
- One `Application`, one `Window`, and one root widget are the whole shell.
- Widgets emit signals; a controller outside the tree owns state changes.

## Next steps

- [Consume the installed SDK](../how-to/consume-the-installed-sdk.md) covers
  the requirements per platform and how to verify an install.
- [Integrate NodalKit into a Meson build](../how-to/integrate-with-meson.md)
  shows how to vendor NodalKit as a subproject and keep C++23 confined to
  the GUI target.
- [Application architecture](../explanation/application-architecture.md)
  explains why the controller split matters as the application grows.
- The `showcase` example in the repository is the fastest broad tour of the
  widget surface: `./buildDir/examples/showcase`.
