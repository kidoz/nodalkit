#pragma once

/// @file windows_interop.h
/// @brief Typed native-interop helpers for the Windows backend.
///
/// These let application code that is staging a migration from Win32 obtain the
/// backing `HWND`/`HINSTANCE` of a NodalKit window without depending on private
/// backend types or including `<windows.h>` from a public header. See
/// docs/NATIVE_INTEROP.md for the full contract and usage patterns.

#if defined(_WIN32)

// A consumer may include <windows.h> before this header without NOMINMAX, which
// defines min()/max() function-like macros that break the std::min/std::max
// calls in the NodalKit headers pulled in below. Neutralize them across just
// these includes, then restore whatever the consumer had so this header is safe
// to include in any order.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

// Forward-declare the opaque handle structs at global scope exactly as the
// Win32 SDK's DECLARE_HANDLE() does. This makes nk::Hwnd/nk::Hinstance the SAME
// types as the SDK's HWND/HINSTANCE (both are `struct HWND__*` / `HINSTANCE__*`),
// so the values below can be passed to Win32 APIs with no cast, whether or not
// <windows.h> is also included in the translation unit.
struct HWND__;      // NOLINT(readability-identifier-naming)
struct HINSTANCE__; // NOLINT(readability-identifier-naming)

namespace nk {

/// `HWND` without pulling in `<windows.h>`.
using Hwnd = ::HWND__*;

/// `HINSTANCE` without pulling in `<windows.h>`.
using Hinstance = ::HINSTANCE__*;

/// The top-level `HWND` backing @p window, or nullptr if the window has not
/// been realized yet (before its first present(), Window::native_surface() is
/// itself null).
///
/// The handle is stable across resize and fullscreen transitions and remains
/// valid until @p window is destroyed. Only call on the UI/event-loop thread;
/// marshal from worker threads with EventLoop::post().
[[nodiscard]] inline Hwnd window_hwnd(const Window& window) noexcept {
    const NativeSurface* surface = window.native_surface();
    return surface != nullptr ? static_cast<Hwnd>(surface->native_handle()) : nullptr;
}

/// The `HINSTANCE` this window's class was registered with, or nullptr if the
/// window has not been realized yet. Same lifetime/thread contract as
/// window_hwnd().
[[nodiscard]] inline Hinstance window_hinstance(const Window& window) noexcept {
    const NativeSurface* surface = window.native_surface();
    return surface != nullptr ? static_cast<Hinstance>(surface->native_display_handle()) : nullptr;
}

} // namespace nk

#endif // defined(_WIN32)
