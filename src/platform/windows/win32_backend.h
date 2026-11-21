#pragma once

/// @file win32_backend.h
/// @brief Win32 platform backend (private header).

#include <cstdint>
#include <memory>
#include <nk/platform/drag_drop.h>
#include <nk/platform/platform_backend.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

struct IDataObject;

namespace nk {

namespace detail {
/// Read a CF_UNICODETEXT allocation while its clipboard or OLE owner keeps it alive.
[[nodiscard]] std::optional<std::string> read_global_unicode_text(HGLOBAL data);

/// Multi-click counter for Win32 button presses: consecutive presses of the
/// same button inside the system double-click time and rectangle increment the
/// count; anything else resets it. Clock and thresholds are injected so the
/// transition logic can be unit tested.
class Win32ClickCounter {
public:
    /// Record a press of `button` at a client position and return the resulting
    /// multi-click count (1 for a fresh click, 2 for a double-click, ...).
    int press(int button,
              int x,
              int y,
              std::uint64_t now_ms,
              std::uint64_t double_click_ms,
              int double_click_width,
              int double_click_height);

private:
    int last_button_ = 0;
    int last_x_ = 0;
    int last_y_ = 0;
    std::uint64_t last_tick_ = 0;
    int count_ = 0;
};

/// Reverse key translation: KeyCode to a Win32 virtual-key code. Returns 0
/// when the key has no accelerator equivalent (modifiers, Unknown).
[[nodiscard]] UINT win32_virtual_key_for_key(KeyCode key);

/// Build a Win32 accelerator (FVIRTKEY-based) for a native menu shortcut.
/// Returns false when the shortcut cannot be represented (Super modifier, a
/// bare key without Ctrl/Alt, or a key without a virtual code), matching the
/// label-only rendering of those shortcuts.
[[nodiscard]] bool
win32_accel_for_shortcut(const NativeMenuShortcut& shortcut, UINT command, ACCEL* out);

/// Convert an IME composition string and its cursor offset (in wchar units,
/// negative when the IME reports none) into a toolkit preedit event with
/// UTF-8 byte selection offsets, matching the Wayland preedit contract.
[[nodiscard]] TextInputEvent win32_preedit_event_from_composition(std::wstring_view composition,
                                                                  int cursor_chars);

/// Build a reference-counted OLE data object exposing a drag payload as
/// CF_HDROP (files) and CF_UNICODETEXT (text) for DoDragDrop. Returns null
/// when the payload carries neither files nor text.
[[nodiscard]] IDataObject* win32_make_drag_data_object(const DragPayload& payload);

/// Current per-window DPI scale (1.0 at 96 DPI); falls back like the backend's
/// own PMv2-aware query. Used by the UIA bridge for physical-pixel geometry.
[[nodiscard]] float win32_dpi_scale_for_hwnd(HWND hwnd);

/// UTF-8 to UTF-16 conversion shared with the UIA bridge.
[[nodiscard]] std::wstring win32_wide_from_utf8(std::string_view text);
} // namespace detail

class Win32Backend : public PlatformBackend {
public:
    Win32Backend();
    ~Win32Backend() override;

    [[nodiscard]] Result<void> initialize() override;
    void shutdown() override;

    [[nodiscard]] std::unique_ptr<NativeSurface> create_surface(const WindowConfig& config,
                                                                Window& owner) override;

    int run_event_loop(EventLoop& loop) override;
    void wake_event_loop() override;
    void request_quit(int exit_code) override;

    [[nodiscard]] bool supports_open_file_dialog() const override;
    void show_open_file_dialog_async(std::string_view title,
                                     const std::vector<std::string>& filters,
                                     OpenFileDialogCallback callback) override;
    [[nodiscard]] bool supports_save_file_dialog() const override;
    void show_save_file_dialog_async(SaveFileDialogOptions options,
                                     SaveFileDialogCallback callback) override;
    [[nodiscard]] bool supports_clipboard_text() const override;
    [[nodiscard]] std::string clipboard_text() const override;
    void set_clipboard_text(std::string_view text) override;

    [[nodiscard]] SystemPreferences system_preferences() const override;
    [[nodiscard]] bool supports_system_preferences_observation() const override;
    void start_system_preferences_observation(SystemPreferencesObserver observer) override;
    void stop_system_preferences_observation() override;

    [[nodiscard]] SpellChecker* spell_checker() override;
    [[nodiscard]] bool supports_native_app_menu() const override;
    void set_native_app_menu(std::span<const NativeMenu> menus,
                             NativeMenuActionHandler action_handler) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
