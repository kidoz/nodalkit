# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [0.5.0] - 2026-11-07

The fifth normal release of the NodalKit framework. 0.5.0 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.4.1 and adds a D3D12 rendering backend to the experimental
Windows target. D3D12 drives its own device, direct command queue, fence, and
flip-model swap chain through Microsoft's D3D11-on-12 runtime, while D3D11
keeps partial swap-chain copies; both share one device and presentation layer.
Windows gains hardware pixel-parity tests across both backends and multiple
scales, Clang AddressSanitizer and UndefinedBehaviorSanitizer builds, SDK smoke
checks for the new link surface, and renderer suites that report an unavailable
native GPU as a skip instead of a failure.

### Support Matrix for 0.5.0
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Added
*   **D3D12 Renderer:** `RendererBackend::D3D12` and `NK_RENDERER_BACKEND=d3d12` run Windows rendering on a D3D12 device, direct command queue, fence, and flip-model swap chain, submitting the shared drawing pipeline through Microsoft's D3D11-on-12 runtime. Wrapped swap-chain resources are acquired before copying and released and flushed before presentation, and resize and teardown drain the queue before releasing native buffers. Windows still prefers D3D11 automatically.
*   **DirectX Drawing:** both DirectX backends draw rectangles, rounded rectangles, borders, linear gradients, text, images, round-capped lines, distance-based shadows, paths of up to 128 points, and per-draw opacity on the GPU, with rounded clips up to eight levels. Affine transforms, larger paths, and deeper clipping rasterize the frame in software and upload it for presentation, and device loss recreates the device on a later frame or falls back to direct software presentation.
*   **Damage-Aware Presentation:** D3D11 copies partial swap-chain regions from dirty rectangles; D3D12 copies the complete retained scene into the current flip buffer so rotated buffers do not show stale pixels while scene drawing still respects damage. Frame diagnostics distinguish GPU draws, software uploads, and direct software presentation, and debug screenshots read back the rendered GPU texture, including uploaded fallback frames.
*   **DirectX Tests:** a Windows `directx_renderer` suite compares both backends with software at multiple scales across effects, paths, opacity, clipping, texture caches, fallback transitions, resize, and scale changes. `NK_REQUIRE_DIRECTX=1` requires hardware, and `NK_DIRECTX_DEBUG=1` enables the DirectX debug layers.
*   **Windows Sanitizers:** Clang builds accept `-Db_sanitize=address,undefined` on Windows through `ci/windows-asan-clang.ini`; Meson links the sanitizer runtime, rejects the debug CRT for AddressSanitizer, and adds the runtime directory to the test `PATH`.
*   **Renderer Skips:** `tools/run_renderer_test.py` maps a fully skipped Catch2 renderer suite to a Meson skip, so Vulkan, DirectX, and Metal suites report an unavailable native backend as skipped rather than failed. `NK_REQUIRE_VULKAN` and `NK_REQUIRE_DIRECTX` turn a missing backend into a failure.

### Changed
*   **Windows Backend Selection:** `RendererBackend` gains `D3D12`, `RendererBackendSupport` gains `d3d12`, and `NK_RENDERER_BACKEND` accepts `d3d12`, `dx12`, and `direct3d12`. `nodalkit.pc` publishes `d3d12`, the Windows SDK smoke check creates both DirectX renderers, and the trace-capture harness validates D3D12 output.
*   **DirectX Device Layer:** the D3D11 renderer moves from `d3d11_renderer.cpp` to `directx_renderer.cpp` and shares device creation and presentation with D3D12 through `directx_device.{h,cpp}`.
*   **Theme Changes:** windows relayout when the application theme selection changes, so density and font changes update geometry as well as pixels.
*   **Vulkan Validation on Windows:** `tools/run_vulkan_validation.py` loads `vulkan-1.dll`, locates SDK validation layers through `VULKAN_SDK` when `VK_LAYER_PATH` is unset, and joins layer names with the platform path separator.
*   **Documentation:** the platform support reference documents DirectX rendering, backend selection, GPU coverage, fallback limits, debug layers, and validation commands, and the Windows support matrix adds local verification and sanitizer instructions.
*   **Test Coverage:** the tooltip traversal test starts from an explicit control instead of relying on native activation focus.

### Fixed
*   **DirectX Image Draws:** image and text draws use a constant buffer sized for `ImageDrawConstants` instead of the larger path buffer, so `UpdateSubresource` copies the correct payload.
*   **Message Catalog Wide Paths:** catalog errors and load failures format `std::filesystem::path` through `path.string()`, so message catalogs compile and report wide native paths on Windows.
*   **DirectWrite Callbacks:** the DirectWrite text layout callbacks are marked `override` explicitly.

## [0.4.1] - 2026-11-07

The fifth patch release of the NodalKit framework. 0.4.1 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.4.0 and paces macOS redraws through a per-window display
clock. Redraws follow the window's display refresh rate, pause when the window
stops requesting frames, and resume on the next invalidation, while timers and
posted tasks keep their independent runtime pump. Metal presentation joins the
native layer transaction while the clock is active, window teardown cancels the
clock and queued frames, and the diagnostics reference documents the resulting
timing behavior.

### Support Matrix for 0.4.1
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Changed
*   **macOS Frame Pacing:** On macOS 14 and later, redraws coalesce through a display clock attached to each window. The clock follows the display's refresh rate, pauses after the window goes idle, resumes on the next invalidation, and falls back to the posted-frame path when a display link is unavailable or stalled. Window teardown cancels its clock and queued frames.
*   **Metal Presentation:** While a display clock is active, Metal commits its command buffer, waits until it is scheduled, presents the drawable, and flushes the Core Animation transaction so layer updates join the same frame. The command-buffer presentation path is unchanged for windows without a clock.
*   **Diagnostics:** `FrameDiagnostics::queue_delay_ms` includes display-clock waiting, and the diagnostics reference documents the clock, fallback, and presentation contract.
*   **Test Coverage:** macOS tests cover display-clock lifecycle under active, paused, unavailable, teardown, and idle-resume conditions, Metal presentation with and without transaction mode, and toolbar lifetime in occluded windows.

## [0.4.0] - 2026-11-07

The fourth normal release of the NodalKit framework. 0.4.0 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.3.4 and rounds out application chrome and navigation: window
titles, navigation and view stacks, search bars, revealers, popovers, tooltips,
overflowing toolbars and header bars, richer tabs, breadcrumbs, menus,
preference rows, status bars, feedback widgets, and keyboard scrolling. Model
and text paths cache visible rows, sort keys, line widths, content extents, and
CoreText metrics, while scrolling honors precise deltas and chains at viewport
boundaries. macOS and Metal object lifetimes are hardened, performance captures
are gated on reproducible work counters, and a simulated trading-terminal
example joins the tree.

### Support Matrix for 0.4.0
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Added
*   **Window and Headerbar Chrome:** `WindowTitle` renders a title with an optional subtitle, and headerbars can show a title widget, center titles with `HeaderbarCenteringPolicy::Loose` or `Strict`, show a contextual back button, and move trailing actions that do not fit into an overflow popover.
*   **Navigation Stack:** `NavigationStack` pushes and pops titled pages, reports depth changes, and binds a headerbar so its back button follows the stack.
*   **View Stack:** `ViewStack` switches peer pages and binds a `SegmentedControl` as its switcher, keeping segments and selection synchronized in both directions.
*   **Search Bar:** `SearchBar` starts as a button, reveals a `SearchField` on activation, and reports text changes, submissions, and revealed state; Escape or the cancel control collapses it and restores focus.
*   **Revealer and Banners:** `Revealer` animates a child's height with reduced-motion support, and `Banner` now collapses and expands through it.
*   **Anchored Popovers:** `Popover` anchors to a widget, flips above when the window lacks room, draws an arrow back to its anchor, takes focus while open, and dismisses on Escape or an outside press while returning focus.
*   **Tooltips:** widgets show a tooltip after a hover delay and immediately on keyboard focus, and store the text as the accessible description.
*   **Toolbar and Tab Overflow:** `Toolbar` moves items that do not fit into an overflow popover and moves focus with arrow, Home, and End keys; `TabBar` tabs accept symbolic icons, close buttons, and page widgets, and overflow into a menu.
*   **Breadcrumbs:** breadcrumbs support keyboard navigation, overflow for long paths, and `on_navigate()` with the chosen depth.
*   **Menus and Split Buttons:** `MenuButton` and `SplitButton` combine a primary action with a context menu; context menus accept native menu items with separators, one level of submenu, and shared action groups with checked and disabled state; `ShortcutLabel` draws accelerator text in menus.
*   **Button Content:** `ButtonContent` composes a label, symbolic icon, and chevron for buttons, and menu and split buttons reuse it.
*   **Preferences Rows:** `ActionRow` and `ExpanderRow` join `SwitchRow`, `ComboRow`, and `EntryRow`, and new `PasswordRow` and `SpinRow` cover secret and numeric settings.
*   **Status Bar Widgets:** status bar segments can hold widgets, trailing text is supported, and a resize grip resizes the bottom-trailing corner of resizable windows with client-side decorations.
*   **Feedback Widgets:** `LevelBar` reports a meter value with a meter accessibility role; `Badge` gains count, capped-count, dot, and status modes; `InfoBar` gains severity levels and a status role; `Avatar` draws a person mark and presence status; toasts carry an action label and pause their timeout while hovered.
*   **Busy Overlay:** `BusyOverlay` keeps a page visible under a scrim with a spinner and message while blocking its pointer and keyboard input.
*   **Link Button and About Logo:** `LinkButton` reports a target string on click, Return, or Space with a link role and cursor, and `AboutInfo::logo` centers a mark above the application identity.
*   **Dialog Buttons:** dialogs add custom response buttons and paint buttons as destructive actions.
*   **Keyboard Scrolling:** focused scroll areas scroll with arrow, Page Up, Page Down, Home, and End keys.
*   **Self-Driven Indeterminate Motion:** progress bars and spinners advance their indeterminate animation from internal timers while shown in a visible window, holding still under reduced motion, when hidden, or when detached.
*   **Sort Key Mode:** `SortListModel::set_sort_key()` sorts by a string extracted once per source row in ascending or descending order and preserves source order for equal keys.
*   **Trading Terminal Example:** a simulated market example with a candlestick chart, watchlist, order ticket, orders and positions tables, and a `just trading-terminal` recipe.

### Changed
*   **Scrolling:** `ScrollArea`, `ListView`, `DataTable`, `GridView`, and `TreeView` honor precise trackpad deltas in logical pixels, release unchanged scrolls so a nested viewport continues them, and cache measured content extents so scrolling skips layout.
*   **Model Refresh:** `TreeModel` caches visible rows with a reverse node-to-row index, and `DataTable` sorts from cached keys and requests layout only when the row count changes.
*   **Text Caches:** macOS `CoreTextShaper` caches font handles and measurements in bounded caches that clear on scale-factor or system-font changes, `TextArea` reuses line widths across edits and composition, and the showcase reuses wrapped text layouts.
*   **Backdrops:** `VisualEffectView` forces an opaque backdrop when the theme requests reduced transparency.
*   **Logging:** log messages record the source location of the call that produced them.
*   **Event Loop:** `EventLoop::post()` returns false when the loop no longer accepts tasks.
*   **Service Locator:** `ServiceLocator::get()` returns an empty pointer for a missing or mismatched service instead of throwing.
*   **Result Misuse:** `Result::value()` and `Result::error()` abort in debug and release builds when the wrong state is accessed.
*   **Desktop Palettes:** `GtkPalette` is renamed to `DesktopPalette` and loads `@define-color` entries from desktop theme stylesheets; theme names must be one path segment and combined stylesheets are limited to 1 MiB.
*   **Settings:** stores reject files larger than 1 MiB, require the application name to be one path segment, and write owner-only files that are flushed before the atomic replace.
*   **Performance Baselines:** baseline comparison lives in `tools/trace_capture_baseline.{h,cpp}` with selectable `timed` and `counters` policies; macOS runs text-, list-, and animation-heavy captures against software and Metal fixtures, gating frame counts, work counters, GPU uploads, draw calls, and shape-cache reuse while reporting wall-clock comparisons as advisory.
*   **Test Coverage:** the public API audit covers the new symbols, and macOS tests cover surface and toolbar lifetimes, Metal output against the software renderer, warm texture reuse, and a constrained drawable pool.

### Fixed
*   **macOS Window Lifetime:** surface teardown now releases the window, content view, delegate, tracking areas, and toolbar, including its delegate and search field, and toolbar replacement or removal detaches callbacks before releasing the previous toolbar.
*   **Metal Alpha Blending:** Metal now uses the software renderer's premultiplied-alpha blend factors, so translucent content matches CPU output.
*   **Secure Text Entry:** password fields mark their input state as secure, enable macOS secure event input while focused in the active key window, bypass IME composition for secure keyboard entry on macOS, and send Wayland sensitive-data hints with surrounding text bounded to 4000 bytes.
*   **AT-SPI Replies:** AT-SPI actions reply on the UI thread with the action result, and each request produces one exception-safe reply even when the task cannot be posted.
*   **Timed Animation:** stopping an animation freezes it at its current value, and sampling at or past the duration latches completion for later `value()` calls.
*   **Grid Captions:** grid view captions center horizontally within their tiles.

## [0.3.4] - 2026-11-06

The fourth patch release of the NodalKit framework. 0.3.4 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.3.3 and hardens the macOS Metal renderer. Metal resources are
owned and released deterministically, textures upload through shared memory on
Apple GPUs and through padded private staging buffers elsewhere, wrapped text
is shaped at its device-scaled width, and software recovery replays a scene the
renderer owns instead of borrowing the caller's tree. Fractional damage,
invalid framebuffers, and allocation or upload failures now recover without
stale or dropped frames, with a new Metal regression suite covering fallback,
uploads, damage, and teardown.

### Support Matrix for 0.3.4
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Fixed
*   **Metal Software Fallback:** Paths, affine transforms, opacity, gradients, and shadows previously disappeared on Metal; they now route to the software path. The renderer owns a recovery copy of the GPU-renderable tree, including borrowed image pixels, so fallback presentation still works after the caller releases the tree. Unsupported node kinds and failed deep copies fall back for the whole frame instead of drawing partially.
*   **Metal Frame Validation:** Zero, negative, NaN, and overflowing logical or scaled framebuffer dimensions are rejected before allocation, damage collection, or surface presentation. Damage regions are rounded outward to whole device pixels, so fractional damage no longer leaves stale edges.
*   **Metal Texture Failures:** Allocation or upload failures while encoding image and text commands now abandon GPU encoding and present the whole frame from software instead of silently dropping content. Failed drawables, command buffers, encoders, and fallback textures present the current software frame.
*   **Metal Resource Lifetime:** Texture cache entries retain and release their textures, and shader functions, pipeline descriptors, samplers, buffers, pipelines, the library, the command queue, the device, and scene textures are released, including when a scene texture is resized or a renderer is destroyed. Per-frame staging textures are released after GPU completion, so fallback presentation no longer leaks device allocations.

### Changed
*   **Metal Texture Uploads:** Textures use shared storage on Apple GPUs and a 256-byte-aligned private staging buffer with blit copies on other GPUs such as Intel and AMD Macs. Shader uniform structs are 16-byte aligned and size-checked at compile time, and shaped-text cache keys include the wrapping width.
*   **Metal Text Wrapping:** `TextNode::max_width()` is honored at the device scale with `TextShaper::shape_wrapped()`, matching the software renderer's wrapped layout.
*   **Metal Test Coverage:** `tests/metal_renderer_test.mm` runs on macOS with the Metal debug layer and isolates the inspected renderer from the class linked in `nk_dep`. It covers unsupported-node fallback, wrapping by width, fractional damage clearing, scene invalidation, deferred upload ownership, drawable failure, padded private uploads, texture-failure recovery, and cache and scene release.

## [0.3.3] - 2026-11-05

The third patch release of the NodalKit framework. 0.3.3 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.3.2 and moves Vulkan lines, gradients, shadows, and nested
opacity onto the GPU, adds application-managed image revisions and texture
diagnostics counters, and hardens Vulkan device selection, texture uploads,
synchronization, and recovery. Linux CI now checks synchronization and injected
frame failures with validation layers.

### Support Matrix for 0.3.3
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Added
*   **Streaming Image Revisions:** `ImageNode::set_content_revision()` and `content_revision()` let applications declare when mutable pixel buffers change while keeping their storage address. `SnapshotContext::add_image(dest, data, width, height, scale, revision)` supplies the revision at draw time, so GPU backends can skip content hashing; a revision must change with the pixels and must not repeat for different contents at the same address.
*   **Texture Diagnostics Counters:** `RenderHotspotCounters` reports `texture_allocation_count`, `texture_upload_byte_count`, `texture_upload_batch_count`, and `texture_cache_byte_count` for each frame. The fields round-trip through frame diagnostics JSON, and older artifacts load them as zero.

### Fixed
*   **Vulkan GPU Drawing:** Lines, dropdown chevrons, gradients, box shadows, and nested opacity render through the GPU pipeline instead of forcing a software upload. Paths and affine transforms use a working software-upload fallback instead of disappearing; software path joins blend once, and affine layers preserve negative source coordinates and clipping. Wrapped text is shaped at the device-scaled wrapping width.
*   **Vulkan Synchronization and Recovery:** Presentation semaphores follow swapchain images, resource mutation waits for submitted work, and the fence is reset immediately before submission. Failed recording, fence reset, and submission recover without hanging; software presentation tears down WSI before committing a `wl_shm` buffer instead of mixing explicit-sync state with software buffers. Render-pass and acquisition barriers cover their actual accesses.
*   **Vulkan Device Selection:** Candidate devices must support presentation, the swapchain extension, usable surface formats, required surface usage, and linear-filtered RGBA textures. Swapchain recreation selects a supported composite-alpha mode and rejects zero-sized or oversized extents instead of silently falling back.

### Changed
*   **Vulkan Uploads and Memory:** Texture uploads share a persistent staging buffer and are recorded into the frame submission instead of waiting on a separate submit. Streaming images reuse their GPU allocation, and the texture cache enforces a 96 MiB budget by evicting cold allocations before growing. Image dimensions are validated against device limits, and frame diagnostics expose allocation, upload, batch, and residency counters.
*   **Vulkan Validation:** `tests/vulkan_renderer_test.cpp` exercises texture batching, allocation reuse, wrapped text, and recovery at runtime. `tests/vulkan_fault_injection.cpp` injects recording, fence-reset, and submission failures through an `LD_PRELOAD` interposer, and `tools/run_vulkan_validation.py` turns synchronization and validation-layer messages into test failures. Linux CI runs the gate with Khronos validation layers.
*   **Documentation:** The platform-support reference documents the Vulkan GPU draw path, software fallbacks, the 96 MiB texture budget, revision rules, and the validation command. The diagnostics reference documents the new texture counters and `gpu_present_path`.
*   **C++ ABI:** `ImageNode` gained a content-revision member and `RenderHotspotCounters` gained four texture counters. Rebuild the library and all C++ consumers together: the class and aggregate layouts changed.

## [0.3.2] - 2026-11-05

The second patch release of the NodalKit framework. 0.3.2 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.3.1 and adds external file and text drops on Wayland,
accessible command children for menus and dropdowns, and an explicit safe
default response for dialogs. It also applies accessibility text scaling
consistently across measurement and snapshots, keeps focus and drops inside the
active modal, wraps dialog content in narrow panels, and preserves line strokes
in the Vulkan renderer.

### Support Matrix for 0.3.2
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Added
*   **Wayland File and Text Drops:** External `text/uri-list` and text offers from other Wayland clients are routed to widget drop targets with `external` set, including Enter, Motion, Leave, and Drop. Transfers are asynchronous and bounded to 16 MiB and two seconds, accept Copy only, decode percent-escaped local file URIs (including `localhost`), reject remote hosts and non-file URLs, and cancel on selection changes, surface teardown, or malformed payloads. File lists are limited to 4096 entries, and receiving a URI never downloads a file. `Window::start_drag()` remains an in-process source.
*   **Menu and Dropdown Accessibility:** `ContextMenu` and `MenuBar` expose their commands as menu-item children with stable identities across reopening, live enabled and checked state, and an Activate action that respects open, enabled, and modal conditions. `ComboBox` exposes its choices only while the popup is open, marks the selected option, and supports Activate on the field and each option. Dismissed, detached, disabled, and modal-covered commands reject activation. The live AT-SPI check now exercises menu and dropdown activation.
*   **Safe Dialog Defaults:** `Dialog::set_default_response()` chooses the response focused on presentation and used for unhandled Return/Space, so destructive confirmations can start on Cancel; the first button remains the fallback when that response has no button.

### Fixed
*   **Modal Focus and Drop Containment:** Programmatic focus stays inside the top visible modal, and external drops never reach content behind a modal even outside its bounds. Drag bubbling keeps its target alive when a handler detaches it, hovered drop state is cleared on detach, and focus restoration falls back to the first attached focusable control when the previous target is gone.
*   **Accessibility Text Scaling:** Widget measurement and snapshot text share one accessibility text scale clamped to the theme's `text-scale` token, so labels, preference rows, dialogs, and controls size from the same nominal logical fonts. TextField and CommandPalette no longer apply a duplicate font scale, Label invalidates cached height-for-width results when the scale changes, and menu, combo, and log line heights follow the scaled text.
*   **Dialog Layout:** Response buttons wrap inside narrow allocations and follow scaled control heights, while titles and messages wrap within the panel instead of overflowing.
*   **Vulkan Line Strokes:** Vulkan preserves line strokes, including dropdown chevrons, by handing them to the software-upload fallback instead of silently omitting them.

### Changed
*   **Documentation:** The new "Receive external files and text" how-to covers drop handlers, payload limits, URI rules, and modal routing. The accessibility how-to now documents shared text scaling, cache invalidation, command semantics, and safe dialog defaults. `tools/atspi_live_check.sh` also finds an AT-SPI bus launcher installed under `/usr/lib/at-spi-bus-launcher`.

## [0.3.1] - 2026-11-04

The first patch release of the NodalKit framework. 0.3.1 keeps the widget
tree, layout system, theming, diagnostics, accessibility bridges, and platform
integration of 0.3.0 and adds native menu roles for the standard editing and
window commands, list accessibility semantics, focus reveal inside nested
scroll areas, and line rendering in the Metal renderer. It also closes native
toolbar input while a modal overlay is up and makes macOS accessibility and
surface teardown safe when AppKit re-enters the toolkit.

### Support Matrix for 0.3.1
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Added
*   **Native Menu Roles:** `NativeMenuRole` and `NativeMenuItem::native_role` route Undo, Redo, Cut, Copy, Paste, Select All, Minimize, and Bring All to Front through the platform's active responder instead of the item's callback; a native role takes precedence, and platforms without native menus keep dispatching `action_name`. On macOS the editing roles reach toolkit editors and native toolbar search fields through the same key events as the application shortcuts, and they are disabled when no text input is active.
*   **Focus Reveal in Scroll Areas:** `ScrollArea::reveal_child()` scrolls a descendant into view with a four-pixel gutter, keeps an oversized descendant that already spans the viewport in place, and otherwise aligns it to the nearest edge. It re-allocates immediately so an enclosing viewport can reveal the descendant too. Keyboard focus reveals the focused widget through every enclosing scroll area; pointer focus does not scroll, so press and release stay over the clicked control.
*   **List Accessibility:** `ListView` exposes a list role whose value is the current row text with its position (for example "Alpha, item 1 of 2") and whose description carries the item and selection counts, with an Activate action that emits `on_row_activated()` for the current row. The value follows model resets, appends, clears, and selection changes.

### Fixed
*   **Native Toolbar During Modals:** macOS toolbar buttons and the search field are disabled while a toolkit modal overlay is visible, keyboard focus returns to the toolkit content, and toolbar activation and search-submit callbacks are filtered while a modal is up. Toolbar items also opt out of AppKit autovalidation so their enabled state follows the toolkit instead of the responder chain.
*   **Toast Overlay Accessibility:** The toast overlay marked its whole content subtree hidden from assistive technology whenever a toast appeared. The overlay is now a named group and the toast surface is a Status element exposed only while a toast is visible, so the content stays reachable.
*   **macOS Accessibility Teardown:** Accessibility elements are detached from the window tree before `NSAccessibilityUIElementDestroyedNotification` is posted, so AppKit cannot query an element that is already retiring, and `Window::accessibility_roots()` now returns nothing while a window is being destroyed.
*   **macOS Surface Teardown:** `MacosSurface` detaches its window delegate, view, and toolbar callbacks before removing the toolbar, because removal can synchronously resize the window and re-enter the retiring surface.
*   **Metal Line Rendering:** The Metal renderer draws `LineNode` segments, including zero-length points, with their thickness, antialiasing, and active clip regions; previously every segment a widget painted through `SnapshotContext::add_line()` was dropped on Metal.

### Changed
*   **Showcase Commands:** The showcase menu bar, native app menu, command palette, and Preferences button share one `ActionGroup`; each menu item is registered as an action and dispatches through a single command handler that also drives the standard editing keys and window commands. A disabled action demonstrates group-wide state.
*   **Showcase Layout:** Inset stages, padded stages, and split columns measure their content against the width they are allocated, minus padding and border, so long text wraps instead of overflowing as the window narrows.
*   **Build:** The lint recipes pass `-Wno-error=c2y-extensions` and `-Wno-unknown-warning-option` to clang-tidy, so newer clang releases do not fail the lint gate on C2y extension warnings from the C++23 headers.
*   **C++ ABI:** `NativeMenuItem` gained a `native_role` member, `PlatformBackend` gained the `set_native_toolbar_blocked()` virtual method, and `ScrollArea` gained `reveal_child()`. Rebuild the library and all C++ consumers together: the aggregate layout and the backend virtual interface changed.

## [0.3.0] - 2026-11-02

The third normal release of the NodalKit framework. 0.3.0 keeps the widget
tree, layout system, theming, diagnostics, and platform integration of 0.2.0
and adds shared actions and shortcuts, a public Box container, UTF-8 message
catalogs, asynchronous clipboard reads, safe cross-thread task posting, and
window state notifications. It also ships discoverable macOS and Linux
accessibility bridges, completes multiline editing, composition, selection,
and undo/redo across the built-in editors, and reworks the Linux showcase
around a full-height GNOME sidebar shell.

### Support Matrix for 0.3.0
*   **Linux Wayland:** Primary Release Target, CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** The macOS and Linux
    accessibility bridges now ship, but real-user validation for those and
    native IME paths is still ongoing.

### Added
*   **Navigation Split Sidebar Visibility:** `NavigationSplitView::set_show_sidebar()` hides the sidebar in the expanded layout. Collapsed navigation still selects its pane with `set_show_content()`.
*   **Asynchronous Clipboard Reads:** `Application::read_clipboard_text_async()` reads clipboard or primary-selection text without blocking the UI thread. Wayland transfers are bounded to 16 MiB and two seconds, yield between chunks, and cancel when the selection changes or the backend shuts down. TextField and TextArea use the asynchronous path for paste.
*   **Safe Cross-Thread Task Posting:** `EventLoop::task_poster()` returns a thread-safe handle that becomes a no-op after loop destruction, and `EventLoop::wake()` wakes the native loop when work arrives from another thread.
*   **Window State Notifications:** `Window::on_window_state_changed()` reports window-manager-confirmed state changes, whether requested by the application or initiated by the platform. `StatusBar` also supports secondary trailing text.
*   **Basic Localization:** `MessageCatalog` loads strict UTF-8 `.nkstrings` files and translates stable message IDs with named placeholders, escaped text, literal braces, visible missing-message fallbacks, and atomic reload errors. Applications choose catalogs and refresh widget text themselves; plural rules, locale detection, and locale-aware number/date formatting are outside this dependency-free API.
*   **Shared Actions and Shortcuts:** `Action` exposes enabled and checkable/checked state notifications; `ActionGroup` propagates changes and supports shared lookup and removal. Buttons, toolkit menus, and command palettes bind directly to shared commands. Window shortcuts use translated keys, exact modifiers, opt-in repeats, weak focus scopes, and modal isolation. The `shared_actions` example demonstrates all four activation paths.
*   **Public Box Container:** `nk::Box` provides rows and columns with ordered child management, mutable orientation, spacing, and homogeneous sizing. The counter example and first-application tutorial use it directly instead of defining a container subclass. Layout-managed widgets honor padding when measuring, allocating, and answering height-for-width queries; attaching null, already attached, or cyclic children is rejected.
*   **Live AT-SPI Check:** `tools/atspi_live_check.sh` runs an application on a private accessibility bus and drives it with libatspi, the library screen readers use. It checks discovery, the tree, roles, states, extents, text, actions, and focus and name-change events. Linux CI runs it against `accessibility_probe`.
*   **macOS Accessibility Bridge:** Widgets with an accessible role appear as NSAccessibility elements below the window's content view, with mapped roles and subroles, names, values, descriptions, enabled/focused/selected state, frames, hit testing, focus, press, and text-field length and selection. Each widget keeps one identity for its lifetime; proxies that AppKit still holds after their widget or window is gone answer as defunct elements. While a modal dialog is shown, only the dialog is exposed, and disabled or covered controls reject press and focus. Focus and focused-value changes are announced after toolkit events, and widget debug names are exposed as accessibility identifiers for UI automation.
*   **Input-Method Replacement Ranges:** `TextInputEvent::replacement_range` (a `TextInputRange` of committed UTF-8 byte offsets) lets a Commit or Preedit replace existing text instead of the selection, as accent pickers and reconversion require. TextField, SearchField, and TextArea apply a replacement commit as one undo step, widen ranges to whole code points, and restore the previous caret when a composition over existing text is cancelled.
*   **Composition State:** `WidgetTextInputState` and `WindowTextInputState` report `composing` so platform backends can drop native composition state after an editor ends it.
*   **Multiline Composition:** `TextArea` renders preedit text and its selection, reveals the composed caret across lines, and reports committed surrounding text through `text_input_state()`. Composition commits are undoable as one step; cancellation preserves committed text and selection.
*   **Multiline Selection and History:** `TextArea` supports Shift navigation, drag/Shift-click selection, double-click word selection, triple-click line selection, select-all, clipboard cut/copy/paste, primary-selection paste, and undo/redo. Public selection queries return UTF-8 byte offsets. The multiline example and installed-SDK smoke test exercise the expanded API.
*   **Multiline Viewport Example and Queries:** `multiline_input` demonstrates scrolling, pointer placement, navigation, and read-only behavior. `TextArea::cursor_position()` and `text_input_caret_rect()` expose the caret byte offset and scrolled window-coordinate bounds.
*   **Search Editing Example:** `search_input` demonstrates toolkit search editing, change notifications, and submission. Dedicated regressions cover selection, clipboard, undo/redo, composition events, caret scrolling, clear cancellation, and rendering bounds; the installed SDK smoke test now exercises search through `TextField`.
*   **Live Renderer Screenshots:** `Renderer::read_back_frame()` copies the last presented frame to CPU memory (software and Vulkan), and `WindowInspector::capture_debug_screenshot()` exposes it with the `source_backend` it came from. Debug screenshots and bundles now capture real Vulkan output instead of a software re-render; the bundle manifest records the source under `screenshot_source`.

### Fixed
*   **Secure Entry Clipboard:** Secure TextField no longer copies or cuts plaintext into the clipboard or primary selection.
*   **Cross-Thread Signal Delivery:** Queued signal arguments now own string-view text and const-span elements, and disconnected or destroyed signals cancel deliveries that have not started. Unsupported borrowed or non-copyable payloads are rejected instead of being queued unsafely.
*   **Object and Service Lifetimes:** Retained widgets detach safely when a parent or window is destroyed, replaced services are destroyed outside the ServiceLocator lock, image snapshots retain their pixels, and worker-thread image updates defer redraws to the UI thread.
*   **Atomic Settings Saves:** `Settings::save()` writes and closes a temporary file before atomically replacing the destination, preventing interrupted saves from truncating existing settings.
*   **Native Event-Loop Wakeups:** Posted work and completed Wayland file-dialog requests now wake the native event loop promptly on Linux, macOS, and Windows instead of waiting for unrelated platform activity.
*   **Invalid Render Dimensions:** Zero, negative, overflowing, and otherwise invalid bitmap or framebuffer dimensions are rejected before allocation or native surface use.
*   **Shared Editor Composition:** TextField, SearchField, and TextArea share preedit state and surrounding deletion. Preedit visually replaces the current selection, empty commits cancel it, and deletion preserves supported character clusters and hard-newline boundaries. Secure TextField also masks preedit during painting.
*   **macOS Marked Text:** Ordinary keys during composition now reach the input context before editor commands. Native marked/selected ranges convert between Cocoa UTF-16 units and toolkit UTF-8 bytes, including surrogate pairs. The Cocoa input client honors replacement ranges, answers substring queries from the committed text with the marked text in place, keeps marked text on `unmarkText` instead of discarding it, commits marked text before a click moves the caret, discards the input context's marked text when an editor ends the composition, and refreshes candidate placement after scrolling, resizing, or moving the window. Candidate windows still anchor to the caret rather than the requested range, point-to-character queries return no index, and live input-method validation remains outstanding.
*   **Secure Entry Input State:** Secure TextField no longer exposes its text, caret offset, or selection through `text_input_state()`; input methods receive caret geometry only. Previously the Wayland backend forwarded password text to the input method as surrounding text.
*   **Shared Editor Undo:** TextField, SearchField, and TextArea use a private buffer for text, caret/anchor, replacement, and history. Undo restores the pre-edit selection, changing edit kind starts a new undo group, and editing after undo discards stale redo. Word helpers and clipboard access are shared by the built-in editors.
*   **Pointer Drag Routing:** Window sends pointer motion to the pressed widget until release, allowing text selection to continue beyond widget bounds while hover tracking follows the pointer.
*   **TextArea Viewport:** Multiline content now scrolls vertically and horizontally, including precise deltas and Shift+wheel. Clicks place the caret at a complete character boundary in the visible line. Editing, navigation, focus, and resizing reveal the caret; pointer focus preserves the clicked viewport. Text and the empty-editor caret are clipped to the padded viewport. Read-only areas allow navigation and scrolling while rejecting edits.
*   **SearchField Editing:** Search now reuses the single-line editor for pointer and keyboard selection, clipboard shortcuts, undo/redo, composition events, and horizontal caret scrolling. Enter emits the current query once. Escape cancels composition before clearing the query; the clear button clears on release inside its bounds. Clearing is undoable.
*   **Single-Line Editor State:** Surrounding-text deletion preserves UTF-8 character boundaries and can be undone. Read-only editors reject undo/redo and preedit input, switching to read-only cancels composition, and undo/redo discard stale preedit. Programmatic text replacement reveals the caret, and text/selection painting stays inside editor bounds.
*   **Multiline Unicode Editing:** `TextArea` now shares `TextField`'s character-boundary helpers for deletion and horizontal navigation. Editing accented text, Cyrillic, CJK, combining marks, and supported emoji clusters no longer splits their UTF-8 bytes. Navigation after a trailing newline stays within the text instead of allowing a later insertion to throw.
*   **Disabled ComboBox Styling:** Disabled combo boxes use muted borders and chevrons across theme families. The macOS chevron capsule also loses its active accent fill in light and dark mode.
*   **Dangling Dirty Widgets:** A widget that queued a redraw and was destroyed before the next frame (for example a dialog closing inside key dispatch) stayed in the window's dirty list as a raw pointer and was dereferenced during damage collection. Widget destruction and detachment now purge the entry, and `Window` tears its widget tree and overlays down while its own state is still alive so the purge never touches a half-destroyed window.
*   **macOS Accessibility Crash:** Any assistive-technology query (VoiceOver, Accessibility Inspector) crashed with `SIGTRAP` in `NSAccessibilityChildren`: the bridge rebuilt ephemeral element objects on every query and never retained their child arrays, so AppKit's accessibility cache kept pointers to freed objects. The bridge now keeps one retained proxy per widget identity (see macOS Accessibility Bridge).
*   **Accessible Dialog Buttons:** Dialog response buttons are real `Button` widgets instead of painted rectangles, so VoiceOver, Orca, and keyboard users can reach and press them; before, a screen-reader user could not confirm or cancel a dialog. Presenting a dialog focuses its Accept button (else the first) and closing restores the previous focus. Escape and Return keep their response behavior, Tab stays inside the dialog, and buttons use the standard button style.
*   **Linux Screen Reader Discovery:** Applications now register with the AT-SPI registry, so Orca and other assistive technologies can find them; before, the accessibility objects existed but no client could discover the application. The D-Bus objects follow the at-spi2-core interface definitions: object references, two-word state sets, and numeric roles; child-at-index, index-in-parent, application, and attribute queries; extents by coordinate type, hit testing, and focus; action names with the default action first; character-based text; application properties including the registry-assigned id; and the bulk cache interface. Focus, state, name, children, text, and window-activation changes are announced as AT-SPI events, and the tree refreshes after every event-loop iteration that did work while a client is using it.
*   **Linux Accessibility Identities:** AT-SPI object paths embed each widget's lifetime identity instead of its position in the tree, so a path a client holds keeps naming the same widget, and actions no longer reach the wrong widget after the tree changes or inside dialogs. Actions pass the same visibility, modality, and enabled checks as on macOS. The snapshot refreshes after input events and whenever a client queries it, instead of only when a window is created. Text offsets count characters, so non-ASCII text no longer splits a character or produces invalid UTF-8.
*   **Secure Entry Accessible Value:** Secure TextField exposes one bullet per character as its accessible value instead of its plaintext, in the widget debug tree, the AT-SPI snapshot, and on macOS.
*   **Sanitizer Test Budget:** The `basic_app` suite carries an explicit Meson timeout so instrumented Vulkan drivers no longer trip the default 30s limit.
*   **Build:** The Vulkan renderer no longer redefines `WIN32_LEAN_AND_MEAN` and `NOMINMAX` on Windows, which failed every Vulkan-enabled Windows build under `-Werror` because the build already defines both on the command line.
*   **Build:** Removed the orphaned `src/platform/window_inspector.cpp`, whose contents had already moved into `window.cpp`.

### Changed
*   **Linux Showcase:** The header bar sits over the content, with the sidebar running the full height beside it. The Settings rows now show or hide that sidebar and apply Comfortable, Standard, or Compact density. Separate macOS and Linux screenshots are included, each collapsed and shown at a small width.
*   **Documentation:** `docs/` is reorganized along the Diátaxis framework into `tutorials/`, `how-to/`, `reference/`, and `explanation/` with an index at `docs/README.md`. The former guides are split by purpose; the application skeleton is now a tutorial whose code compiles against the current headers (the previous version constructed `nk::Widget` directly, which its protected constructor forbids), and the support-bundle snippet passes a string to `save_debug_bundle()` as its signature requires.
*   **Multiline Navigation:** Home/End move within the current line; Control or Command plus Home/End moves through the document. Up/Down and Page Up/Page Down use a retained horizontal position measured in pixels, including across short lines. Lines remain unwrapped and overflow horizontally. TextArea supports toolkit composition events; complete native IME integration and platform validation remain open.
*   **C++ ABI:** `SearchField` now derives from `TextField`, retaining its existing public methods and adding inherited editor operations. `TextField` exposes protected content-rendering and geometry hooks. `TextInputEvent`, `WidgetTextInputState`, and `WindowTextInputState` gained members. Rebuild the library and all C++ consumers together: the class layout and virtual interface changed.

## [0.2.0] - 2026-08-30

The second normal release of the NodalKit framework. 0.2.0 keeps the widget
tree, layout system, theming, accessibility scaffolds, and diagnostics stack
of 0.1.0 and adds cross-platform drag and drop, native save/open dialogs, a
persistent settings store, GNOME-style adaptive navigation widgets, native
Win32 integration, and a much broader automated test and CI surface.

### Support Matrix for 0.2.0
*   **Linux Wayland:** Primary Release Target, now CI-tested against a real
    headless Wayland compositor with a software Vulkan device.
*   **macOS:** Secondary Release Target (supported while CI remains green).
*   **Windows:** Support exists but is highly experimental. It must not be
    considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal remain experimental and
    earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** Real-user validation for these
    paths is still ongoing.

### Added
*   **ServiceLocator:** Application-level dependency injection container for registering and resolving service interfaces, with mock injection support for tests.
*   **StateStore:** Unidirectional data flow (MVI) state container template; views observe `state()` and mutate only through dispatched intents. Re-entrant dispatch from observers is supported.
*   **AbstractTableModel & AbstractTreeModel:** Abstract base classes with change-notification signals for 2D table and application-owned hierarchical data. These are forward contracts: `DataTable` and `TreeView` migrate to them during 0.x (see header docs for the roles of `TreeModel` vs `AbstractTreeModel`).
*   **Two-Way Property Binding:** `Property<T>::bind_bidirectional()` synchronizes two properties in both directions via a pair of `ScopedConnections`, complementing the existing one-way `bind_to()`.
*   **Drag and Drop:** Data transfer with widget-level drag signals and in-process drag sessions; macOS bridges external file drops into the same event path.
*   **Native File Dialogs:** Save-file dialogs across macOS, Windows, and Wayland, complementing the existing async open dialog.
*   **LogView:** Append-only virtualized widget for high-volume streaming logs.
*   **Settings Store:** Persistent key/value store with typed accessors, recent-file tracking, and window-geometry persistence.
*   **GNOME Desktop Integration:** Palette, font, color-scheme, accent, contrast, and text-scale preferences read from XDG portals with GSettings fallback; accent split into fill and standalone-text roles; a real Adwaita-style GNOME theme family.
*   **Adaptive Navigation Widgets:** `NavigationSplitView`, `OverlaySplitView`, `ToolbarView`, `PreferencesPage`, `StatusPage`, `ToastOverlay`, `ContextMenu`, `SearchField`, `Expander`, `TextArea`, `Popover`, `Breadcrumb`, `Calendar`, `ColorWell`, `Badge`, `Avatar`, and `Spinner`, plus boxed-list settings rows and a GNOME-style headerbar with title/subtitle and an About window.
*   **Command Palette Search:** Free-text filtering and keyboard-driven selection in the command palette.
*   **Keyboard Focus Modality:** Input-modality tracking with a `FocusVisible` state so keyboard-only focus rings appear only for keyboard navigation, honoring the desktop reduced-motion preference.
*   **Window Features:** Vetoable close-request path guarded by a close-policy predicate, typed HWND accessors with a documented native-window-handle contract, and a window backdrop capability that resolves to a none/opaque/material token.
*   **Windows Platform:** Native Win32 menus, file drops, and spell checking; DirectWrite emoji, symbol, and mixed-script font fallbacks; a Windows 10 fallback theme family selected by detected OS build; high-contrast palette swap for both color schemes.
*   **Wayland Protocols:** Fractional scaling (`wp_fractional_scale_v1` + viewporter), client-side decorations (`xdg-decoration`), cursor shape (`cursor-shape-v1`), and a `zwp_text_input_v3` IME bridge alongside compose-aware text input and client-side key repeat.
*   **Accessibility Tooling:** A standalone AT-SPI snapshot validation viewer under `tools/`, an `accessibility_probe` example, and live AT-SPI enumeration support on GNOME Wayland.
*   **Diagnostics:** GPU frame diagnostics, filterable offline viewers for traces, frame diagnostics, render snapshots, and diagnostics bundles, plus a menu action for discoverable bundle export.
*   **Build:** MSVC C++23 (`/std:c++latest`) support and an install-smoke target that builds a downstream sample against the installed SDK.
*   **Examples:** A long-task example for background work on the event loop and a process-launch example monitoring a child process off the UI thread.

### Changed
*   **Build:** Meson >= 1.11 is now required.
*   **Theming:** Widget styling converged on semantic tokens — radius roles (`radius-control/card/popup/...`), the `control-height` metric, named spacing metrics, per-family type-scale tokens, paired selection colors (active/inactive, focus-aware), a `scrollbar-mode` policy (overlay vs persistent), and a rule-driven combo chevron style per platform family.
*   **Linux Showcase:** Redesigned around GNOME navigation patterns with a sidebar, page stack, headerbar, status bar, and per-page PPM screenshot capture (`--screenshot-dir`); the macOS showcase places content under the native toolbar with an inset below native chrome.
*   **CI:** Linux jobs run the suite against a headless Weston compositor with Mesa's software Vulkan device; sanitizer, install-smoke, i686 cross, and MSVC jobs cover more of the matrix; clang-format is version-pinned and clang-tidy lints the compile database.
*   **Version:** The project version is generated as an installed `<nk_version.h>` header from `meson.project_version()`, replacing hardcoded release strings in code and packaging.

### Fixed
*   **NaN Binding Recursion:** `Property<T>::set()` now treats self-unequal values (e.g. floating-point NaN) as unchanged relative to each other, so two-way bindings terminate instead of recursing to a stack overflow.
*   **StateStore Deadlock:** Removed the internal mutex that was held while change signals emitted; an observer dispatching another intent no longer deadlocks. The store's single-thread (owning-thread) contract is now documented explicitly.
*   **Wayland:** SHM buffers are recreated on geometry change to avoid resize image corruption; clipboard and primary-selection mime types reset per data offer so external paste works; selection sources are destroyed when the compositor cancels them; input focus clears on surface destruction; quit flags are atomic for cross-thread `request_quit`; the viewport destination updates on fractional-scale configure.
*   **Event Loop:** Timers fire from a snapshot so callbacks can mutate the timer list safely.
*   **Window:** `Window::close` is guarded against a close handler destroying the window.
*   **Headerbar:** Fixed a crash in the subtitle color lookup when the property is missing.
*   **Software Renderer:** Pixel offsets computed in `size_t`, oversized text runs are refused instead of overflowing the bitmap byte count, nested rounded clips flatten correctly with wider Vulkan clip capacity, and overflowing text is elided in buttons, table cells, and command-palette rows instead of painting past bounds.
*   **Layout:** Box cross-axis minimums are tracked separately from naturals so splits stop starving siblings.
*   **VisualEffectView:** Backdrop materials resolve semantic surface tokens through the theme, so the material follows the active color scheme instead of painting a light-theme constant in dark mode.
*   **Application:** Destruction now clears the process-global active theme so a later application does not inherit stale preference state.

## [0.1.0] - Initial Stable Release

This marks the first normal `0.1.0` release of the NodalKit framework. NodalKit ships a working widget tree, layout system, CSS-like pseudo-states and theming, accessibility scaffolds, and a built-in diagnostic stack.

### Support Matrix for 0.1.0
*   **Linux Wayland:** Primary Release Target.
*   **macOS:** Secondary Release Target (supported while CI remains green).

### Known Unsupported Scenarios
*   **Windows:** Support exists but is highly experimental. It must not be considered a parity or release-supported target.
*   **X11 & OpenGL:** No support claims are made for X11 or OpenGL backends.
*   **GPU Parity:** Linux Vulkan and macOS Metal are experimental and earlier in correctness confidence than the software render paths.
*   **Production-grade IME & Accessibility:** Real-user validation for these paths is still ongoing.

### Added
*   **Core Foundation:** Type-safe C++23 signal/slot system, observable property bindings, and `nk::Result` error handling.
*   **Event Loop & Tasks:** Built-in timers and cross-thread task posting via `EventLoop::current()`.
*   **Async Dialogs:** Non-blocking `open_file_dialog_async` preventing main-loop starvation.
*   **Layout System:** GTK4-style measure/allocate layout engine featuring `BoxLayout`, `GridLayout`, and `StackLayout`.
*   **Widgets (20+):** `Button`, `Label`, `TextField`, `ListView`, `TreeView`, `DataTable`, `ComboBox`, `CommandPalette`, `ImageView`, `MenuBar`, `Dialog`, and more.
*   **Model/View API:** `AbstractListModel`, `TreeModel`, and `SelectionModel` with reactive item rendering.
*   **Styling:** CSS-like selectors, theme tokens, and pseudo-state styling (e.g. `Hovered`, `Pressed`).
*   **Diagnostics:** Built-in interactive widget tree inspector, trace export, and snapshot tools.
*   **Text Shaping:** FreeType (Linux), CoreText (macOS), and DirectWrite (Windows) shaper backends.
*   **Dependency Pinning:** Meson subproject wrappers (`nodalkit.wrap`) now correctly pin to release tags instead of HEAD.
