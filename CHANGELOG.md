# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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
