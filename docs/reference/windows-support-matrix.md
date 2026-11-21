# Windows support matrix

Windows is a secondary supported target (like macOS: supported while CI stays
green; Windows regressions block a release). This page makes the support
granular so consumers (e.g. a 32-bit emulator frontend) can see exactly what
is covered and what is still open.

Status legend: ✅ implemented and CI-built · 🧪 implemented, experimental /
lightly tested · ❌ not implemented.

## Architectures

| Architecture | Status | Notes |
| ------------ | ------ | ----- |
| x86-64 (x64) | ✅ | Built in CI (debug + release). |
| i686 (x86)   | 🧪 | Cross-built in CI via [`ci/windows-i686-clang.ini`](../../ci/windows-i686-clang.ini). The import libraries are arch-independent; the 32-bit CRT/SDK comes from the toolchain. This is the target a classic 32-bit HLE emulator process needs. |
| ARM64        | ❌ | Not built or tested. |

## Toolchains

| Toolchain | Status | Notes |
| --------- | ------ | ----- |
| clang / clang++ + lld-link | ✅ | The primary Windows toolchain; the dev host and CI both use it. |
| MSVC (`cl`) | 🧪 | Builds and passes the full suite under `/W4 /WX` (verified locally with VS 2026, `cl` 19.52; the codebase needed no source changes — the historical `-Wno-c2y-extensions` claim was stale, that flag is meson-gated to clang). CI runs an advisory lane; floor is VS 2022 17.2+ (C++20 designated initializers). |
| clang-cl | 🧪 | Should work (same front end); not a CI path. |
| MinGW | ❌ | Not tested. |

## Render backends

| Backend | Status | Notes |
| ------- | ------ | ----- |
| Software | ✅ | Always available; GDI blit path. |
| D3D11 | 🧪 | Hardware GPU rendering, damage-aware redraw, native readback; `src/render/directx_renderer.cpp`. |
| D3D12 | 🧪 | D3D12 device, queue and swap chain; D3D11-on-12 drawing, fence synchronization, native readback. |
| Vulkan | 🧪 | Optional; requires the Vulkan SDK and `glslangValidator` at build time. |

See [DirectX rendering](platform-support.md#directx-rendering) for selection,
GPU coverage, software fallback limits, and validation commands.

## Native integrations

| Integration | Status | Notes |
| ----------- | ------ | ----- |
| File / save dialogs | ✅ | Common Item Dialog, owned by the active window (app-modal). |
| Clipboard | ✅ | Win32 clipboard, `CF_UNICODETEXT` text both ways. |
| Drag & drop | ✅ | Both directions. Receive side: OLE `IDropTarget`; external drops (files via `CF_HDROP`, text via `CF_UNICODETEXT`) are translated to `DragPayload` and delivered through `Window::dispatch_drag_drop_event`. Source side: `Window::start_drag` runs a native `DoDragDrop` for text and file payloads so drags leave the process; in-process application data keeps the toolkit's synthetic simulation. |
| App / menu bar | ✅ | Native menu bar built per window from the app-global `NativeMenu` model (`SetMenu` / `AppendMenuW`); `WM_COMMAND` dispatches `action_name` through the shared `NativeMenuActionHandler`. A `HACCEL` accelerator table is installed per window, so menu shortcuts (Ctrl+O, ...) actually trigger, translated before toolkit key dispatch so they cannot double-activate. Native roles (Undo/Redo/Cut/Copy/Paste/Select All/Minimize) are enabled and dispatched by replaying their conventional shortcut. |
| High-DPI | ✅ | Per-Monitor V2 DPI awareness; scale tracked per window; `WM_DPICHANGED` applies the suggested rect. |
| Dark-mode chrome | ✅ | Title bar and menu bar follow the system color scheme (`DWMWA_USE_IMMERSIVE_DARK_MODE` with the legacy attribute fallback) and re-apply on theme change. |
| System preferences | ✅ | Push-based observation via `WM_SETTINGCHANGE` / `WM_THEMECHANGE` on a hidden utility window (no polling fallback); color scheme, contrast, motion, transparency, text scale, and accent color are read from the OS. |
| IME / text input | 🧪 | IMM32 composition: preedit rendered inline by the toolkit (`Preedit`/`ClearPreedit` events), commits through `GCS_RESULTSTR` exactly once, and composition + candidate windows anchored to the focused editor's caret. Focus loss cancels an in-flight composition, secure (password) editors cancel composition and close the IME, and input-language changes are acknowledged. Covered by message-level window tests (`WM_IME_*`, `WM_CHAR`) plus Wine-style IMM composition-string synthesis; live validation across real keyboards is still maturing. |
| Fullscreen | ✅ | Borderless-on-monitor toggle. |
| Text shaping | ✅ | DirectWrite, with a GDI fallback shaper. |
| Native window handle | ✅ | `HWND` / `HINSTANCE` via [`windows_interop.h`](../../include/nk/platform/windows_interop.h). See the [Native handle contract](native-handles.md). |
| Taskbar identity | ✅ | The first window sets the process AppUserModelID from `WindowConfig::app_id` (seeded from `Application::app_id()`), so taskbar grouping and jump lists are stable. |
| Accessibility | 🧪 | UI Automation provider: fragment tree over the shared accessibility model with WM_GETOBJECT wiring, navigation, core properties (name/control type/automation id/bounds/help text/focus/enabled/password), Invoke/Toggle/Value/Selection/SelectionItem/ExpandCollapse/Window patterns, real numeric ranges with programmatic value setting (sliders, spin buttons, read-only progress), and property/structure/focus/invoke/selection event raising gated by client listeners — all verified through the IUIAutomation client API in `tests/win32_uia_test.cpp`. Not yet implemented: the Text pattern, live regions, and screen-reader validation. |
| Spell checking | ✅ | Windows Spell Checking API (`ISpellCheckerFactory` / `ISpellChecker`) in `src/platform/windows/win32_spell_checker.cpp`. Lazily picks the first supported language; returns empty results (no crash) when no spell-check language is installed. Returned `SpellCheckRange` offsets are UTF-8 bytes. |

## Release gating

Windows is a release gate: the blocking `build-windows` CI job (x64 debug and
release) must stay green, and a Windows regression blocks a release the same
way a Linux one does. The i686 cross-build and the sanitizer build remain
advisory signal while their coverage matures. Treat remaining 🧪 rows as
"works in our testing, please file issues."

## Local verification

Run the Windows host checks from a terminal with Clang and the Windows SDK
available:

```powershell
$env:CC = "clang"
$env:CXX = "clang++"
meson setup build-windows --buildtype=debug
meson compile -C build-windows
meson test -C build-windows --print-errorlogs --num-processes 1
```

For AddressSanitizer and UndefinedBehaviorSanitizer, use the supplied native
file. It keeps debug symbols and selects the release CRT to avoid debug-heap
startup failures with the Clang runtime:

```powershell
meson setup build-windows-asan --native-file ci/windows-asan-clang.ini
meson compile -C build-windows-asan
meson test -C build-windows-asan --print-errorlogs --num-processes 1
```

Meson adds Clang's sanitizer DLL directory to the test environment. Running an
instrumented executable directly also requires that directory on `PATH`.
Renderer suites report a skip when every case lacks the required native GPU
backend. Set `NK_REQUIRE_DIRECTX=1` or `NK_REQUIRE_VULKAN=1` to require that
backend and treat its absence as a failure.

With the Vulkan SDK and its validation layer installed, run:

```powershell
python tools/run_vulkan_validation.py meson test -C build-windows -v vulkan_renderer
```

The script locates SDK layers through `VULKAN_SDK` when `VK_LAYER_PATH` is not
already set. It fails on validation warnings or errors.

## Packaging

- On Windows, NodalKit builds as a static library. `nodalkit.pc` publishes
  the transitive Win32 import libraries (`user32`, `gdi32`, `d3d11`, `d3d12`,
  `dwrite`, …) on its link line, so a `pkg-config` consumer links cleanly
  with no manual `-l` flags. See
  [Integrate NodalKit into a Meson build](../how-to/integrate-with-meson.md).
- Building a C++23 GUI target alongside a C++17 emulator core in one process
  is supported; see the same guide and
  [Mixing C++ standards in one process](../explanation/mixing-cpp-standards.md).
