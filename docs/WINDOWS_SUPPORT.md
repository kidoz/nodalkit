# Windows support matrix

Windows is an **experimental, actively developed** target. This page makes the
"experimental" label granular so consumers (e.g. a 32-bit emulator frontend) can
see exactly what is covered.

Status legend: ✅ implemented and CI-built · 🧪 implemented, experimental /
lightly tested · ❌ not implemented.

## Architectures

| Architecture | Status | Notes |
| ------------ | ------ | ----- |
| x86-64 (x64) | ✅ | Built in CI (debug + release). |
| i686 (x86)   | 🧪 | Cross-built in CI via `ci/windows-i686-clang.ini`. The import libraries are arch-independent; the 32-bit CRT/SDK comes from the toolchain. This is the target a classic 32-bit HLE emulator process needs. |
| ARM64        | ❌ | Not built or tested. |

## Toolchains

| Toolchain | Status | Notes |
| --------- | ------ | ----- |
| clang / clang++ + lld-link | ✅ | The supported Windows toolchain; the dev host and CI both use it. |
| clang-cl | 🧪 | Should work (same front end); not the primary CI path. |
| MSVC (`cl`) | ❌ | Not supported: the sources use clang-only flags such as `-Wno-c2y-extensions`. |
| MinGW | ❌ | Not tested. |

## Render backends

| Backend | Status | Notes |
| ------- | ------ | ----- |
| Software | ✅ | Always available; GDI blit path. |
| D3D11 | 🧪 | `src/render/d3d11_renderer.cpp`. |
| Vulkan | 🧪 | Optional; requires the Vulkan SDK and `glslangValidator` at build time. |

## Native integrations

| Integration | Status | Notes |
| ----------- | ------ | ----- |
| File / save dialogs | 🧪 | Common Item Dialog. |
| Clipboard | 🧪 | Win32 clipboard. |
| Drag & drop | 🧪 | OLE `IDropTarget` / `RegisterDragDrop`. |
| App / menu bar | 🧪 | Native menu integration. |
| High-DPI | 🧪 | Per-monitor DPI awareness; scale tracked per window. |
| Fullscreen | 🧪 | Borderless-on-monitor toggle. |
| Text shaping | 🧪 | DirectWrite, with a GDI fallback shaper. |
| Native window handle | ✅ | `HWND` / `HINSTANCE` via [`windows_interop.h`](../include/nk/platform/windows_interop.h). See [NATIVE_INTEROP.md](NATIVE_INTEROP.md). |
| Accessibility | ❌ | No UI Automation provider yet. The accessibility model (`nk/accessibility`) is populated, but only the Linux AT-SPI bridge is wired. |

## Release gating

Windows is **not** a release gate today: CI builds and tests it, but a Windows
regression does not block a release the way a Linux one does. Treat the 🧪 rows
as "works in our testing, please file issues," not "guaranteed."

## Packaging notes

- On Windows, NodalKit builds as a **static library**. `nodalkit.pc` publishes
  the transitive Win32 import libraries (`user32`, `gdi32`, `d3d11`, `dwrite`,
  …) on its link line, so a pkg-config consumer links cleanly with no manual
  `-l` flags. See [MESON_INTEGRATION.md](MESON_INTEGRATION.md).
- Building a C++23 GUI target alongside a C++17 emulator core in one process is
  supported; see the mixed-standard section of [MESON_INTEGRATION.md](MESON_INTEGRATION.md).
