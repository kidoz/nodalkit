# Consume the installed SDK

This guide shows how to link an application against an installed NodalKit
SDK through `pkg-config`, and how to verify the install.

NodalKit 0.x ships a single distribution surface: a `pkg-config` file named
`nodalkit.pc` installed alongside the library and headers. There is no CMake
package config module in 0.x. If you need CMake integration, invoke
`pkg-config` from your `CMakeLists.txt`.

## Requirements

- Meson 1.11 or newer.
- A C++23 compiler.
- `pkg-config` or `pkgconf` for downstream SDK consumption.
- On Windows: a Windows SDK with Win32, DWM, Shell, DirectWrite, D3D11,
  D3DCompiler, DXGI, and COM development libraries. The current Windows
  development host uses `clang++` with `lld-link`, but consumers should rely
  on the compiler and linker flags exported by `nodalkit.pc`.
- Optional Vulkan support requires a Vulkan SDK or system package discoverable
  by Meson plus `glslangValidator` at NodalKit build time. If either is
  missing, the Windows build still installs the Win32, DirectWrite, and D3D11
  SDK surface.

Per-platform library names and transitive link requirements are listed in
[Platform support](../reference/platform-support.md).

## Install to a prefix

```bash
meson setup buildDir --prefix="/your/prefix" --libdir=lib
meson compile -C buildDir
meson install -C buildDir
```

## Point pkg-config at the install

After `meson install`, point `PKG_CONFIG_PATH` at the install prefix and use
`pkg-config` like any other dependency:

```bash
export PKG_CONFIG_PATH="/your/prefix/lib/pkgconfig"
pkg-config --cflags --libs nodalkit
# -I/your/prefix/include -L/your/prefix/lib -lNodalKit
```

## Depend on it from Meson

`dependency('nodalkit')` resolves through the same file:

```meson
nodalkit_dep = dependency('nodalkit', method : 'pkg-config')

executable('my_app', 'main.cpp', dependencies : nodalkit_dep)
```

On Windows, NodalKit is a static library and `nodalkit.pc` publishes the
transitive Win32 import libraries on its link line. Use `pkg-config` rather
than spelling the library list by hand.

## Verify the installed SDK

NodalKit's downstream sample checks that every public module is reachable
through the installed headers and library:

```bash
meson compile -C buildDir install-smoke
```

That target configures a separate staged install, validates the generated
`nodalkit.pc`, then builds and runs `tests/install_smoke` against the
installed headers and library instead of the source tree.

## Related

- [Your first NodalKit application](../tutorials/your-first-application.md)
  walks through this from an empty directory.
- [Integrate NodalKit into a Meson build](integrate-with-meson.md) covers
  the subproject alternative and mixed C++ standards.
