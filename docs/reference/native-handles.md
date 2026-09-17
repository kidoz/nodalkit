# Native handle contract

Every realized window exposes a platform-native handle through its surface.
This page states what the handles are and the rules that apply to them. For
usage patterns, see [Use native window handles](../how-to/use-native-window-handles.md).

## Accessors

```cpp
nk::NativeSurface* surface = window.native_surface(); // nullptr until present()
void* handle = surface ? surface->native_handle() : nullptr;
void* display = surface ? surface->native_display_handle() : nullptr;
```

## Concrete types

| Platform | `native_handle()` | `native_display_handle()` |
| -------- | ----------------- | ------------------------- |
| Windows  | `HWND`            | `HINSTANCE`               |
| macOS    | `NSWindow*`       | `nullptr`                 |
| Linux    | `wl_surface*`     | `wl_display*` (Wayland)   |

## Rules

The same rules apply to both accessors on every platform.

| Rule | Contract |
| ---- | -------- |
| Validity | Non-null only after the owning `Window`'s first `present()`. Before that, `native_surface()` itself is `nullptr`. |
| Lifetime | Valid until the `Window` is destroyed. Never retain it beyond the window's lifetime. |
| Stability | Stable across resize and fullscreen transitions. NodalKit does not recreate the top-level window for either, so the handle does not change. |
| Thread affinity | Touch it only on the UI/event-loop thread. Marshal work from other threads with `EventLoop::post()`. |
| Ownership | The backend owns the surface. Do not reparent NodalKit's surface into another window. |

## Windows typed helpers

Declared in [`nk/platform/windows_interop.h`](../../include/nk/platform/windows_interop.h)
and available only when `_WIN32` is defined:

| Declaration | Description |
| ----------- | ----------- |
| `nk::Hwnd` | Alias for `HWND__*`, binary-identical to the SDK's `HWND`. |
| `nk::Hinstance` | Alias for `HINSTANCE__*`, binary-identical to the SDK's `HINSTANCE`. |
| `nk::window_hwnd(const Window&)` | Returns the window's `HWND`, or `nullptr` until the window is realized. |
| `nk::window_hinstance(const Window&)` | Returns the window's `HINSTANCE`, or `nullptr` until the window is realized. |

The header does not include `<windows.h>`. NodalKit builds define `NOMINMAX`
project-wide; consumers that include `<windows.h>` themselves should do the
same.

## Unsupported

Reaching into private backend headers under `src/platform/**` is not
supported and may break between releases. macOS and Linux have no typed
helpers yet; cast the `void*` at the call site.
