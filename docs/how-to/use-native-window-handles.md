# Use native window handles

NodalKit owns its platform surfaces, but real applications, especially ones
migrating from a native toolkit, sometimes need the underlying window handle
to bridge legacy code or call platform APIs. This guide shows the supported
ways to reach native state on each platform. The validity, lifetime, and
threading rules for every handle are in the
[Native handle contract](../reference/native-handles.md); read it once
before using anything below.

> Scope: these are the supported ways to reach native state. Reaching into
> private backend headers under `src/platform/**` is not supported and may
> break between releases.

## Windows: get a typed `HWND` or `HINSTANCE` without `<windows.h>`

Prefer the typed helpers over casting `void*` yourself. They fold in the
null-until-realized check and give you a value you can pass straight to
Win32 APIs:

```cpp
#include <nk/platform/windows_interop.h>

void attach_legacy_config(const nk::Window& window) {
    nk::Hwnd hwnd = nk::window_hwnd(window);   // same type as the SDK's HWND
    if (hwnd == nullptr) {
        return; // window not realized yet; call after present()
    }
    // Pass straight to Win32 with no cast:
    ::EnableWindow(hwnd, TRUE);
}
```

`nk::Hwnd` and `nk::Hinstance` are declared as `struct HWND__*` and
`struct HINSTANCE__*`, binary-identical to what `DECLARE_HANDLE` produces,
so the header does not drag `<windows.h>` (and its `min`/`max` macros) into
your translation unit. Include `<windows.h>` yourself only where you
actually call Win32 APIs, and always with `NOMINMAX` (NodalKit builds define
it project-wide).

Typical uses:

- Parent or position an emulator output window relative to the frontend.
- Keep legacy Win32-only dialogs working during a staged migration.
- Attach platform diagnostics or a message filter.

## macOS and Linux: cast at the call site

There are no typed helpers yet; cast the `void*` where you use it:

```cpp
// macOS
auto* ns_window = static_cast<NSWindow*>(window.native_surface()->native_handle());

// Linux / Wayland
auto* wl_surface =
    static_cast<struct wl_surface*>(window.native_surface()->native_handle());
auto* wl_display =
    static_cast<struct wl_display*>(window.native_surface()->native_display_handle());
```

Check `native_surface()` for null first: it is null until the window's
first `present()`.

## Bridge a native message loop

NodalKit runs its own loop via `EventLoop::run()`. Do not run a second
blocking native modal loop on the UI thread; it starves NodalKit's frame and
input dispatch. Instead:

- Keep long or blocking work on a worker thread and post results back with
  `EventLoop::post()`. See
  [Launch and monitor external processes](launch-external-processes.md) for
  the full pattern.
- For transient native UI (a legacy modal dialog), show it and pump only
  while it is open, then return control to `EventLoop::run()`.

## Host or coordinate an external native window

For emulator output, prefer a separate top-level window or a native child
window you own, positioned relative to `window_hwnd()`, over trying to host
a foreign swap chain inside the widget tree. NodalKit's `ImageView` is
appropriate for copied ARGB frames and previews. Do not reparent NodalKit's
own surface into another window; the backend owns its lifecycle.
