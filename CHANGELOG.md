# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
*   **Multiline Selection and History:** `TextArea` supports Shift navigation, drag/Shift-click selection, double-click word selection, triple-click line selection, select-all, clipboard cut/copy/paste, primary-selection paste, and undo/redo. Public selection queries return UTF-8 byte offsets. The multiline example and installed-SDK smoke test exercise the expanded API.
*   **Multiline Viewport Example and Queries:** `multiline_input` demonstrates scrolling, pointer placement, navigation, and read-only behavior. `TextArea::cursor_position()` and `text_input_caret_rect()` expose the caret byte offset and scrolled window-coordinate bounds.
*   **Search Editing Example:** `search_input` demonstrates toolkit search editing, change notifications, and submission. Dedicated regressions cover selection, clipboard, undo/redo, composition events, caret scrolling, clear cancellation, and rendering bounds; the installed SDK smoke test now exercises search through `TextField`.
*   **Live Renderer Screenshots:** `Renderer::read_back_frame()` copies the last presented frame to CPU memory (software and Vulkan), and `WindowInspector::capture_debug_screenshot()` exposes it with the `source_backend` it came from. Debug screenshots and bundles now capture real Vulkan output instead of a software re-render; the bundle manifest records the source under `screenshot_source`.

### Fixed
*   **Shared Editor Composition:** TextField, SearchField, and TextArea share preedit state and surrounding deletion. Preedit visually replaces the current selection, empty commits cancel it, and deletion preserves supported character clusters and hard-newline boundaries. Secure TextField also masks preedit during painting.
*   **Shared Editor Undo:** TextField, SearchField, and TextArea use a private buffer for text, caret/anchor, replacement, and history. Undo restores the pre-edit selection, changing edit kind starts a new undo group, and editing after undo discards stale redo. Word helpers and clipboard access are shared by the built-in editors.
*   **Pointer Drag Routing:** Window sends pointer motion to the pressed widget until release, allowing text selection to continue beyond widget bounds while hover tracking follows the pointer.
*   **TextArea Viewport:** Multiline content now scrolls vertically and horizontally, including precise deltas and Shift+wheel. Clicks place the caret at a complete character boundary in the visible line. Editing, navigation, focus, and resizing reveal the caret; pointer focus preserves the clicked viewport. Text and the empty-editor caret are clipped to the padded viewport. Read-only areas allow navigation and scrolling while rejecting edits.
*   **SearchField Editing:** Search now reuses the single-line editor for pointer and keyboard selection, clipboard shortcuts, undo/redo, composition events, and horizontal caret scrolling. Enter emits the current query once. Escape cancels composition before clearing the query; the clear button clears on release inside its bounds. Clearing is undoable.
*   **Single-Line Editor State:** Surrounding-text deletion preserves UTF-8 character boundaries and can be undone. Read-only editors reject undo/redo and preedit input, switching to read-only cancels composition, and undo/redo discard stale preedit. Programmatic text replacement reveals the caret, and text/selection painting stays inside editor bounds.
*   **Multiline Unicode Editing:** `TextArea` now shares `TextField`'s character-boundary helpers for deletion and horizontal navigation. Editing accented text, Cyrillic, CJK, combining marks, and supported emoji clusters no longer splits their UTF-8 bytes. Navigation after a trailing newline stays within the text instead of allowing a later insertion to throw.
*   **Disabled ComboBox Styling:** Disabled combo boxes use muted borders and chevrons across theme families. The macOS chevron capsule also loses its active accent fill in light and dark mode.
*   **Dangling Dirty Widgets:** A widget that queued a redraw and was destroyed before the next frame (for example a dialog closing inside key dispatch) stayed in the window's dirty list as a raw pointer and was dereferenced during damage collection. Widget destruction and detachment now purge the entry, and `Window` tears its widget tree and overlays down while its own state is still alive so the purge never touches a half-destroyed window.
*   **macOS Accessibility Crash:** Any assistive-technology query (VoiceOver, Accessibility Inspector) crashed with `SIGTRAP` in `NSAccessibilityChildren` because the per-query widget bridge handed AppKit ephemeral nodes that its accessibility cache outlived. The window is now exposed as a single accessibility group carrying the window title. This is a deliberate interim regression: per-widget accessibility elements on macOS return once they are anchored and thread-safe. Linux AT-SPI is unaffected.
*   **Sanitizer Test Budget:** The `basic_app` suite carries an explicit Meson timeout so instrumented Vulkan drivers no longer trip the default 30s limit.
*   **Build:** Removed the orphaned `src/platform/window_inspector.cpp`, whose contents had already moved into `window.cpp`.

### Changed
*   **Multiline Navigation:** Home/End move within the current line; Control or Command plus Home/End moves through the document. Up/Down and Page Up/Page Down use a retained horizontal position measured in pixels, including across short lines. Lines remain unwrapped and overflow horizontally. TextArea composition and native IME integration remain planned work.
*   **C++ ABI:** `SearchField` now derives from `TextField`, retaining its existing public methods and adding inherited editor operations. `TextField` exposes protected content-rendering and geometry hooks. Rebuild the library and all C++ consumers together: the class layout and virtual interface changed.

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
*   **Build:** MSVC C++23 (`/std:c++latest`) support and an install-smoke target that builds a downstream sample against the staged SDK.
*   **Examples:** A long-task example for background work on the event loop and a process-launch example monitoring a child process off the UI thread.

### Changed
*   **Build:** Meson >= 1.11 is now required.
*   **Theming:** Widget styling converged on semantic tokens — radius roles (`radius-control/card/popup/...`), the `control-height` metric, named spacing metrics, per-family type-scale tokens, paired selection colors (active/inactive, focus-aware), a `scrollbar-mode` policy (overlay vs persistent), and a rule-driven combo chevron style per platform family.
*   **Linux Showcase:** Redesigned around GNOME navigation patterns with a sidebar, page stack, headerbar, status bar, and per-page PPM screenshot capture (`--screenshot-dir`); the macOS showcase flattens stages under the native toolbar with content inset below native chrome.
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
