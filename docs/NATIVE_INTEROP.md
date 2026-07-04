# Native interop cookbook

NodalKit owns its platform surfaces, but real applications — especially ones
migrating from a native toolkit — sometimes need the underlying window handle to
bridge legacy code or call platform APIs. This document describes the supported
seams and their contract.

> Scope: these are the *supported* ways to reach native state. Reaching into
> private backend headers under `src/platform/**` is not supported and may break
> between releases.

## The native handle contract

Every realized window exposes a platform-native handle through its surface:

```cpp
nk::NativeSurface* surface = window.native_surface(); // nullptr until present()
void* handle = surface ? surface->native_handle() : nullptr;
```

The concrete type behind the opaque `void*` is:

| Platform | `native_handle()` | `native_display_handle()` |
| -------- | ----------------- | ------------------------- |
| Windows  | `HWND`            | `HINSTANCE`               |
| macOS    | `NSWindow*`       | `nullptr`                 |
| Linux    | `wl_surface*`     | `wl_display*` (Wayland)   |

Contract (identical for both accessors):

- **Validity** — non-null only after the owning `Window`'s first `present()`.
  Before that, `native_surface()` itself is `nullptr`.
- **Lifetime** — valid until the `Window` is destroyed. Never retain it beyond
  the window's lifetime.
- **Stability** — stable across resize and fullscreen transitions. NodalKit does
  not recreate the top-level window for either, so the handle does not change.
- **Thread affinity** — touch it only on the UI/event-loop thread. Marshal work
  from other threads with `EventLoop::post()`.

## Windows: typed `HWND` / `HINSTANCE` without `<windows.h>`

Prefer the typed helpers over casting `void*` yourself. They fold in the
null-until-realized check and give you a value you can pass straight to Win32
APIs:

```cpp
#include <nk/platform/windows_interop.h>

void attach_legacy_config(const nk::Window& window) {
    nk::Hwnd hwnd = nk::window_hwnd(window);   // same type as the SDK's HWND
    if (hwnd == nullptr) {
        return; // window not realized yet — call after present()
    }
    // Pass straight to Win32 with no cast:
    ::EnableWindow(hwnd, TRUE);
}
```

`nk::Hwnd` and `nk::Hinstance` are declared as `struct HWND__*` / `struct
HINSTANCE__*` — binary-identical to what `DECLARE_HANDLE` produces — so the
header does **not** drag `<windows.h>` (and its `min`/`max` macros) into your
translation unit. Include `<windows.h>` yourself only where you actually call
Win32 APIs, and always with `NOMINMAX` (NodalKit builds define it project-wide).

Typical uses:

- Parent or position an emulator output window relative to the frontend.
- Keep legacy Win32-only dialogs working during a staged migration.
- Attach platform diagnostics or a message filter.

## macOS and Linux

There are no typed helpers yet; cast the `void*` at the call site:

```cpp
// macOS
auto* ns_window = static_cast<NSWindow*>(window.native_surface()->native_handle());

// Linux / Wayland
auto* wl_surface =
    static_cast<struct wl_surface*>(window.native_surface()->native_handle());
auto* wl_display =
    static_cast<struct wl_display*>(window.native_surface()->native_display_handle());
```

The same validity, lifetime, stability, and thread rules apply.

## Bridging native message loops

NodalKit runs its own loop via `EventLoop::run()`. Do not run a second blocking
native modal loop on the UI thread — it starves NodalKit's frame and input
dispatch. Instead:

- Keep long or blocking work on a worker thread and post results back with
  `EventLoop::post()` (see [PROCESS_LAUNCH.md](PROCESS_LAUNCH.md) for the full
  pattern).
- For transient native UI (a legacy modal dialog), show it and pump only while
  it is open, then return control to `EventLoop::run()`.

## Hosting or coordinating external native windows

For emulator output, prefer a **separate top-level window** or a native child
window you own, positioned relative to `window_hwnd()`, over trying to host a
foreign swap chain inside the widget tree. NodalKit's `ImageView` is appropriate
for copied ARGB frames and previews; see the embedding guidance in
[BUILDING_APPLICATIONS.md](BUILDING_APPLICATIONS.md). Do not reparent NodalKit's
own surface into another window — the backend owns its lifecycle.
