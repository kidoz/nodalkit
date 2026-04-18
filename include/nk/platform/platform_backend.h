#pragma once

/// @file platform_backend.h
/// @brief Abstract platform backend for OS integration.
///
/// This is a private interface — application code does not use it
/// directly. Application and Window delegate to the active backend.

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/platform/events.h>
#include <nk/platform/file_dialog.h>
#include <nk/platform/native_menu.h>
#include <nk/platform/spell_checker.h>
#include <nk/platform/system_preferences.h>
#include <nk/render/renderer.h>
#include <nk/ui_core/cursor_shape.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class EventLoop;
class Window;
struct WindowConfig;
enum class TitlebarStyle : std::uint8_t;

/// Opaque handle to a platform-native window surface.
using NativeWindowHandle = void*;

/// Callback for delivering events from the platform to a Window.
using EventCallback = std::function<void(Window&)>;

/// Callback for backend-driven system preference changes.
using SystemPreferencesObserver = std::function<void(const SystemPreferences&)>;

/// Callback for native app-menu activation.
using NativeMenuActionHandler = std::function<void(std::string_view)>;

/// Abstract native surface for a single window.
class NativeSurface {
public:
    virtual ~NativeSurface() = default;

    /// Show the window on screen.
    virtual void show() = 0;

    /// Hide the window.
    virtual void hide() = 0;

    /// Set the window title.
    virtual void set_title(std::string_view title) = 0;

    /// Resize the window content area.
    virtual void resize(int width, int height) = 0;

    /// Current content area size.
    [[nodiscard]] virtual Size size() const = 0;

    /// Device pixel ratio for the current content area.
    [[nodiscard]] virtual float scale_factor() const = 0;

    /// Current framebuffer size in physical pixels.
    [[nodiscard]] virtual Size framebuffer_size() const {
        const auto logical = size();
        const auto scale = std::max(scale_factor(), 0.0F);
        return {
            std::round(logical.width * scale),
            std::round(logical.height * scale),
        };
    }

    /// Renderer backends that this surface can host.
    [[nodiscard]] virtual RendererBackendSupport renderer_backend_support() const { return {}; }

    /// Present a rendered pixel buffer to the screen.
    /// @param rgba  Pixel data in RGBA8 format, row-major.
    /// @param w     Pixel width.
    /// @param h     Pixel height.
    /// @param damage_regions Optional changed regions in pixel-buffer coordinates.
    virtual void
    present(const uint8_t* rgba, int w, int h, std::span<const Rect> damage_regions = {}) = 0;

    /// Toggle fullscreen mode.
    virtual void set_fullscreen(bool fullscreen) = 0;
    [[nodiscard]] virtual bool is_fullscreen() const = 0;

    /// Get the platform-native handle (NSWindow*, wl_surface*, etc.).
    [[nodiscard]] virtual NativeWindowHandle native_handle() const = 0;

    /// Get the platform-native display/connection handle when the renderer
    /// needs both the surface and the parent display server connection.
    [[nodiscard]] virtual NativeWindowHandle native_display_handle() const { return nullptr; }

    /// Apply a platform cursor shape for the current pointer location.
    virtual void set_cursor_shape(CursorShape shape) = 0;

    /// Apply a titlebar presentation style. Default no-op; macOS implements
    /// full-size content-view and transparent-titlebar variants.
    virtual void set_titlebar_style(TitlebarStyle style) { (void)style; }
};

/// Abstract platform backend. One per Application.
class PlatformBackend {
public:
    virtual ~PlatformBackend() = default;

    /// Initialize platform state (e.g. connect to display server).
    [[nodiscard]] virtual Result<void> initialize() = 0;

    /// Shut down and release all platform resources.
    virtual void shutdown() = 0;

    /// Create a native surface for a Window.
    [[nodiscard]] virtual std::unique_ptr<NativeSurface> create_surface(const WindowConfig& config,
                                                                        Window& owner) = 0;

    /// Run the platform event loop. Drives EventLoop polling and
    /// platform event dispatch. Returns the exit code.
    virtual int run_event_loop(EventLoop& loop) = 0;

    /// Wake the event loop from another thread (e.g. for posted tasks).
    virtual void wake_event_loop() = 0;

    /// Request the event loop to stop.
    virtual void request_quit(int exit_code) = 0;

    /// Whether the backend implements a native single-file open dialog.
    [[nodiscard]] virtual bool supports_open_file_dialog() const { return false; }

    /// Show a native "open file" dialog.
    [[nodiscard]] virtual OpenFileDialogResult
    show_open_file_dialog(std::string_view title, const std::vector<std::string>& filters) = 0;

    /// Whether the backend implements clipboard text integration.
    [[nodiscard]] virtual bool supports_clipboard_text() const { return false; }

    /// Read UTF-8 text from the platform clipboard.
    [[nodiscard]] virtual std::string clipboard_text() const { return {}; }

    /// Write UTF-8 text to the platform clipboard.
    virtual void set_clipboard_text(std::string_view text) { (void)text; }

    /// Whether the backend implements a primary-selection text buffer.
    [[nodiscard]] virtual bool supports_primary_selection_text() const { return false; }

    /// Read UTF-8 text from the platform primary selection buffer.
    [[nodiscard]] virtual std::string primary_selection_text() const { return {}; }

    /// Write UTF-8 text to the platform primary selection buffer.
    virtual void set_primary_selection_text(std::string_view text) { (void)text; }

    /// Query current visual/system preferences used for theme selection.
    [[nodiscard]] virtual SystemPreferences system_preferences() const = 0;

    /// Whether the backend can push native system-preference updates.
    [[nodiscard]] virtual bool supports_system_preferences_observation() const { return false; }

    /// Start native system-preference observation.
    /// Backends that do not support this may ignore the request.
    virtual void start_system_preferences_observation(SystemPreferencesObserver observer) {
        (void)observer;
    }

    /// Stop native system-preference observation.
    virtual void stop_system_preferences_observation() {}

    /// Platform spell-checking service. Returns nullptr on backends that do
    /// not provide one. The returned pointer is owned by the backend and
    /// remains valid until shutdown.
    [[nodiscard]] virtual SpellChecker* spell_checker() { return nullptr; }

    /// Whether the backend supports a native application menu.
    [[nodiscard]] virtual bool supports_native_app_menu() const { return false; }

    /// Install a native application menu model.
    virtual void set_native_app_menu(std::span<const NativeMenu> menus,
                                     NativeMenuActionHandler action_handler) {
        (void)menus;
        (void)action_handler;
    }

    /// Create the platform-appropriate backend for the current OS.
    [[nodiscard]] static std::unique_ptr<PlatformBackend> create();
};

} // namespace nk
