#pragma once

/// @file uia_provider.h
/// @brief UI Automation provider bridge for the Win32 backend (private header).
///
/// Exposes the shared accessibility tree (nk::detail::AccessibilityTree) to UI
/// Automation clients through the fragment provider pattern: one COM provider
/// per exposed element, answering everything live from the widget tree, plus a
/// fragment-root provider for the HWND.

#include <memory>
#include <nk/accessibility/accessible.h>
#include <string_view>
#include <windows.h>

namespace nk {

class Window;

namespace detail {

/// Map a NodalKit accessible role (plus the widget type name for special
/// cases like ComboBox) to a UIA control type id. Plain int so this header
/// does not need the UI Automation SDK headers; compare against the
/// UIA_*ControlTypeId constants in tests.
[[nodiscard]] int win32_uia_control_type(AccessibleRole role, std::string_view type_name);

/// The WM_GETOBJECT object id that identifies a UI Automation request
/// (the SDK's UiaRootObjectId; mirrored here so this header stays lean).
constexpr LONG kUiaRootObjectId = -25;

/// UI Automation bridge for one Win32Surface. Lazily created by the surface on
/// the first UIA WM_GETOBJECT; all provider callbacks arrive on the UI thread
/// (ProviderOptions_UseComThreading over the OleInitialize STA), so providers
/// read the widget tree and dispatch actions without any marshaling.
class Win32UiaBridge {
public:
    Win32UiaBridge(HWND hwnd, Window& owner);
    ~Win32UiaBridge();

    Win32UiaBridge(const Win32UiaBridge&) = delete;
    Win32UiaBridge& operator=(const Win32UiaBridge&) = delete;
    Win32UiaBridge(Win32UiaBridge&&) = delete;
    Win32UiaBridge& operator=(Win32UiaBridge&&) = delete;

    /// Whether a WM_GETOBJECT carries the UIA root object id.
    [[nodiscard]] static bool is_uia_request(LPARAM lparam) {
        return static_cast<LONG>(lparam) == kUiaRootObjectId;
    }

    /// Answer WM_GETOBJECT. Returns 0 when the request is not for UIA, so the
    /// caller can fall through to DefWindowProc.
    [[nodiscard]] LRESULT handle_get_object(WPARAM wparam, LPARAM lparam);

    /// Disconnect every provider (WM_DESTROY / surface teardown). Idempotent.
    void disconnect();

    /// Retire providers of destroyed widgets and raise a focus-changed event
    /// when the focused element moved. Cheap no-op until a client connected.
    void synchronize();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace detail
} // namespace nk
