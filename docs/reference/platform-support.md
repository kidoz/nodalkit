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
| Windows | Experimental | Win32, D3D11/D3D12, DirectWrite/GDI. Actively developed; CI builds and tests it, but a Windows regression does not block a release. |
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
| Windows | Static library; the exact filename is toolchain-dependent and the current clang-based build emits `libNodalKit.a` | Static | Nothing extra when consuming through `pkg-config`. The generated `nodalkit.pc` includes `user32`, `gdi32`, `advapi32`, `dwmapi`, `ole32`, `shcore`, `dwrite`, `d3d11`, `d3d12`, `d3dcompiler`, and `dxgi`. Optional Vulkan link flags are present only when Vulkan support was detected while building NodalKit. |

## Build requirements

| Requirement | Notes |
| ----------- | ----- |
| Meson | 1.11 or newer. |
| Compiler | C++23. |
| `pkg-config` or `pkgconf` | Required for downstream SDK consumption. |
| Windows SDK | Win32, DWM, Shell, DirectWrite, D3D11, D3D12, D3DCompiler, DXGI, and COM development libraries. The current Windows host uses `clang++` with `lld-link`. |
| Vulkan (optional) | A Vulkan SDK or system package discoverable by Meson plus `glslangValidator` at NodalKit build time. When missing, the Windows build still installs the Win32, DirectWrite, D3D11, and D3D12 surface. |

## Render backends

| Backend | Platforms |
| ------- | --------- |
| Software | All. |
| D3D11 | Windows 10/11, hardware feature level 11.0 or later. |
| D3D12 | Windows 10/11, D3D12 hardware with the D3D11-on-12 runtime. |
| Metal | macOS. |
| Vulkan | Linux and Windows, experimental and optional. |

### DirectX rendering

Windows automatically prefers D3D11. Set `NK_RENDERER_BACKEND=d3d11` or
`NK_RENDERER_BACKEND=d3d12` to select a backend explicitly; `dx11`,
`direct3d11`, `dx12`, and `direct3d12` are also accepted. Query
`renderer_backend_available()` before requiring hardware. Both backends use
hardware devices; failure to create or present a frame falls back to software.

D3D12 creates its own D3D12 device, direct command queue, fence, and DXGI swap
chain. It submits the shared drawing pipeline through Microsoft's
[D3D11-on-12 runtime](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12).
Wrapped swap-chain resources are acquired before copying and released and
flushed before presentation. Resize and teardown drain the queue before
releasing the native buffers. This is an interop implementation, with the
runtime translating D3D11 draw calls into D3D12 command lists.

Both backends draw rectangles, rounded rectangles, borders, linear gradients,
text, images, round-capped lines, distance-based shadows, paths of up to 128
points, and per-draw opacity on the GPU. Rounded clips support eight levels.
Affine transforms, larger paths, and deeper clipping rasterize the complete
frame in software and upload it for DirectX presentation. Every render-node
kind preserves its semantics; none is silently discarded. Text is rasterized
with DirectWrite before texture upload. Device loss permits recreation on a
subsequent frame, with direct software presentation if recovery is unavailable.

The retained GPU scene supports damage-aware redraw. D3D11 uses partial
swap-chain copies and dirty rectangles. D3D12 copies the complete retained
scene into the current flip buffer to avoid stale pixels as buffers rotate;
scene drawing still respects damage. Frame diagnostics distinguish GPU draws,
software uploads, and direct software presentation. Debug screenshots read
back the actual rendered GPU texture, including uploaded fallback frames.

Native pixel regressions compare both backends with software at multiple
scales and cover effects, paths, opacity, clipping, texture caches, fallback
transitions, resize, and scale changes. To require hardware rather than skip:

```powershell
$env:NK_REQUIRE_DIRECTX = '1'
meson test -C build directx_renderer renderer basic_app --print-errorlogs
```

Set `NK_DIRECTX_DEBUG=1` to enable the D3D11 and D3D12 debug layers; this requires
the Windows Graphics Tools optional feature. Debug-layer errors invalidate the
GPU path and fail the native pixel checks. Windows remains an experimental
platform; this implementation does not change its release tier.

### Vulkan rendering

Vulkan uses a Vulkan 1.0 API baseline and negotiates a renderable BGRA8 or
RGBA8 UNORM surface with nonlinear sRGB presentation. Other surface formats
use software presentation. Its GPU draw path handles solid and
rounded rectangles, borders, text, images, round-capped lines, linear gradients,
distance-based box shadows, per-draw opacity, and nested affine transforms
(translation, scaling, rotation, reflection, and shear). Clips retain the
coordinate space where they were defined. Rounded clips support four active
regions after same-space flattening; lines and gradients support three. Deeper
clipping and paths render the frame in software and upload it for Vulkan
presentation. Singular and nonfinite transforms draw no pixels.

GPU transforms rasterize primitives directly and sample image/text textures;
software transforms use nearest sampling of bounded transparent layers. Edge
antialiasing can therefore differ between these paths. Transform data uses a
reusable dynamic uniform buffer while push constants remain within Vulkan 1.0's
guaranteed 128-byte minimum.

GPU image and text allocations share a 96 MiB cache budget. Cold allocations
are evicted before making room for new ones. Frames whose working set exceeds
the budget use software rasterization. Texture uploads are batched into the
frame submission, and changes to an image at the same storage address reuse
its allocation. The unversioned image API detects changes by hashing pixels.
`SnapshotContext::add_image(dest, data, width, height, scale, revision)` allows
applications to supply a revision instead; increment it whenever those pixels
change, including when recycling storage. A revision must not repeat for
different contents at the same address. Pixel storage must remain valid
through rendering.

`window.renderer_backend()` identifies the selected backend. The frame's
`render_hotspot_counters.gpu_present_path` distinguishes GPU drawing from
software upload or direct software presentation. A Vulkan screenshot source
can therefore contain a software-rasterized frame.

On Linux, native GPU and recovery checks can run with validation installed:

```sh
python3 tools/run_vulkan_validation.py meson test -C buildDir -v \
  vulkan_renderer vulkan_failure_submit vulkan_failure_reset-fence vulkan_failure_record
```

This command requires a Wayland session, a Vulkan device, and
`VK_LAYER_KHRONOS_validation`. It fails if Vulkan is unavailable or validation
reports errors or warnings. CI supplies headless Weston and Mesa's software
Vulkan implementation.

## Accessibility bridges

The accessibility model under `nk/accessibility` is populated on every
platform. Linux exposes it through AT-SPI, registering each application with
the desktop registry so screen readers such as Orca can find it, and macOS
through NSAccessibility elements below the window's content view. Both expose
one element per widget with a role. While a
modal dialog is open, macOS exposes only that dialog, and disabled or covered
controls reject accessibility actions. Windows has no UI Automation provider
yet.
