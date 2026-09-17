# Platform support

This page records the 0.x support policy, what NodalKit installs on each
platform, and what a consumer must have available at link time. For the
Windows backend in detail, see the
[Windows support matrix](windows-support-matrix.md).

## Support tiers (0.x)

| Platform | Tier | Meaning |
| -------- | ---- | ------- |
| Linux Wayland | Primary release target | Regressions block a release. |
| macOS | Secondary target | Supported while CI stays green. |
| Windows | Experimental | Win32, D3D11, DirectWrite/GDI. Actively developed; CI builds and tests it, but a Windows regression does not block a release. |
| X11 | Not a release target | Not supported yet. |

## Distribution surface

NodalKit 0.x ships one distribution surface: a `pkg-config` file named
`nodalkit.pc` installed alongside the library and headers. There is no CMake
package config module in 0.x.

## Installed artifacts and link requirements

| Platform | Installed library | Linkage | Consumer must provide at link time |
| -------- | ----------------- | ------- | ---------------------------------- |
| Linux Wayland | `libNodalKit.so` | Shared | `wayland-client`, `xkbcommon`, `freetype2`, `fontconfig`, `harfbuzz`, `gio-2.0`. On most distributions these come in the matching `-dev` packages. |
| macOS | `libNodalKit.dylib` | Shared | Nothing extra. The dylib links the Cocoa, CoreGraphics, CoreText, Metal, QuartzCore, and UniformTypeIdentifiers frameworks, and downstream applications pick them up automatically. |
| Windows | Static library; the exact filename is toolchain-dependent and the current clang-based build emits `libNodalKit.a` | Static | Nothing extra when consuming through `pkg-config`. The generated `nodalkit.pc` includes `user32`, `gdi32`, `advapi32`, `dwmapi`, `ole32`, `shcore`, `dwrite`, `d3d11`, `d3dcompiler`, and `dxgi`. Optional Vulkan link flags are present only when Vulkan support was detected while building NodalKit. |

The static/shared decision on Windows will be revisited before Windows
graduates from experimental status.

## Build requirements

| Requirement | Notes |
| ----------- | ----- |
| Meson | 1.11 or newer. |
| Compiler | C++23. |
| `pkg-config` or `pkgconf` | Required for downstream SDK consumption. |
| Windows SDK | Win32, DWM, Shell, DirectWrite, D3D11, D3DCompiler, DXGI, and COM development libraries. The current Windows host uses `clang++` with `lld-link`. |
| Vulkan (optional) | A Vulkan SDK or system package discoverable by Meson plus `glslangValidator` at NodalKit build time. When missing, the Windows build still installs the Win32, DirectWrite, and D3D11 surface. |

## Render backends

| Backend | Platforms |
| ------- | --------- |
| Software | All. |
| D3D11 | Windows. |
| Metal | macOS. |
| Vulkan | Linux and Windows, experimental and optional. |

## Accessibility bridges

The accessibility model under `nk/accessibility` is populated on every
platform. Only the Linux AT-SPI bridge is wired today; per-widget
accessibility elements on macOS are an interim regression documented in the
[changelog](../../CHANGELOG.md), and Windows has no UI Automation provider
yet.
