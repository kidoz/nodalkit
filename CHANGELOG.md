# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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
