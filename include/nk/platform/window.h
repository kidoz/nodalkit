#pragma once

/// @file window.h
/// @brief Top-level window abstraction.

#include <cstdint>
#include <functional>
#include <memory>
#include <nk/debug/diagnostics.h>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/platform/drag_drop.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/native_toolbar.h>
#include <nk/render/renderer.h>
#include <nk/ui_core/cursor_shape.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nk {

class NativeSurface;
class TextShaper;
class Widget;
class Dialog;
class Headerbar;
class RenderNode;
class WindowInspector;

struct SystemPreferences;
struct MouseEvent;
struct KeyEvent;
struct TextInputEvent;
struct WindowEvent;

/// Titlebar presentation style.
///
/// macOS honors all variants; Linux/Windows backends currently fall back to
/// the regular decoration. When the titlebar is transparent (`Unified` or
/// `Hidden`) on macOS, the content view extends under it, so root layouts
/// should reserve vertical space for the titlebar area (about 28 points) to
/// avoid drawing behind the traffic lights.
enum class TitlebarStyle : std::uint8_t {
    /// Traditional opaque titlebar; content sits below it.
    Regular,
    /// Titlebar becomes transparent and content extends under it; the title
    /// text remains visible.
    Unified,
    /// Titlebar is transparent and the title text is hidden; traffic lights
    /// remain functional on macOS.
    Hidden,
};

/// Configuration for window creation.
struct WindowConfig {
    std::string title;
    /// Wayland app_id (reverse-DNS) used by the compositor to group windows,
    /// associate a `.desktop` file, and apply window-matching rules. When left
    /// empty, `Window::present()` seeds it from `Application::app_id()`.
    std::string app_id{};
    int width = 800;
    int height = 600;
    bool resizable = true;
    bool decorated = true;
    TitlebarStyle titlebar_style = TitlebarStyle::Regular;
};

/// Focused text-input state exposed to platform backends for IME integration.
struct WindowTextInputState {
    std::string text;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    Rect caret_rect{};
};

/// A top-level application window. Owns a root widget and manages
/// the platform surface.
class Window {
public:
    /// Create a window with the given configuration.
    explicit Window(WindowConfig config = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Set the window title.
    void set_title(std::string_view title);
    [[nodiscard]] std::string_view title() const;

    /// Resize the window.
    void resize(int width, int height);
    [[nodiscard]] Size size() const;
    [[nodiscard]] float scale_factor() const;
    [[nodiscard]] Size framebuffer_size() const;

    /// Set the root widget displayed in this window.
    void set_child(std::shared_ptr<Widget> child);

    /// Get the current root widget.
    [[nodiscard]] Widget* child() const;

    /// Show the window on screen.
    void present();

    /// Hide the window.
    void hide();

    /// Close the window unconditionally: emit the close-request notification
    /// and hide it. This bypasses any close policy (see set_close_policy) — it
    /// is the "force close" a confirm-before-close flow calls once the user has
    /// approved. Application code and the platform window manager should prefer
    /// request_close().
    void close();

    /// Request a close that a close policy may veto. If a policy is installed
    /// (see set_close_policy) and it returns false, nothing happens and the
    /// window stays open; otherwise this behaves like close(). This is the path
    /// the platform close button (WindowEvent::Type::Close) drives.
    void request_close();

    /// Install a predicate consulted by request_close() before the window
    /// hides. Return true to allow the close, false to veto it. Runs on the
    /// UI/event-loop thread. Only one policy may be installed; a new one
    /// replaces the previous. Pass nullptr to clear.
    ///
    /// For an asynchronous "confirm before close" flow (NodalKit dialogs are
    /// non-blocking), veto the request by returning false and kick off a
    /// confirm Dialog; when the user approves, call close() to force it.
    void set_close_policy(std::function<bool()> policy);

    /// Whether the window is currently visible.
    [[nodiscard]] bool is_visible() const;

    /// Whether the window currently has platform focus (is the key/active window).
    [[nodiscard]] bool is_focused() const;

    /// Fullscreen mode.
    void set_fullscreen(bool fullscreen);
    [[nodiscard]] bool is_fullscreen() const;

    /// Minimize (iconify) the window. No-op if the window has no surface yet.
    void minimize();
    /// Toggle the maximized state of the window.
    void toggle_maximize();
    /// Whether the window is currently maximized.
    [[nodiscard]] bool is_maximized() const;
    /// Whether the active platform surface expects client-drawn window chrome.
    [[nodiscard]] bool uses_client_side_decorations() const;

    /// Titlebar presentation style. See TitlebarStyle for per-platform behavior.
    void set_titlebar_style(TitlebarStyle style);
    [[nodiscard]] TitlebarStyle titlebar_style() const;

    /// Install a window-attached native toolbar. On macOS this uses NSToolbar
    /// with autosaved user customization (reorder, show/hide). Platforms
    /// without a native toolbar ignore the call. The config is retained by
    /// the window until cleared or replaced.
    void set_native_toolbar(NativeToolbarConfig config);

    /// Remove any installed native toolbar.
    void clear_native_toolbar();

    /// Whether a native toolbar is currently installed on this window.
    [[nodiscard]] bool has_native_toolbar() const;

    /// Request a frame to be rendered on the next idle.
    void request_frame();

    /// Mark the widget tree layout as dirty and schedule a redraw.
    void invalidate_layout();

    /// Deliver a platform event to this window.
    void dispatch_mouse_event(const MouseEvent& event);
    void dispatch_key_event(const KeyEvent& event);
    void dispatch_text_input_event(const TextInputEvent& event);
    void dispatch_window_event(const WindowEvent& event);
    [[nodiscard]] DragOperation dispatch_drag_drop_event(DragDropEvent event);

    /// Start an in-process drag operation. The payload remains alive until
    /// the drag is dropped or cancelled.
    void start_drag(DragPayload payload, DragOperation operation = DragOperation::Copy);
    void cancel_drag();
    [[nodiscard]] bool is_drag_active() const;

    /// Access the native surface (nullptr until present() is called).
    [[nodiscard]] NativeSurface* native_surface() const;

    /// Active renderer backend for this window.
    [[nodiscard]] RendererBackend renderer_backend() const;

    /// Current cursor shape resolved from the hovered widget.
    [[nodiscard]] CursorShape current_cursor_shape() const;

    /// Whether the given key is currently pressed according to window input state.
    [[nodiscard]] bool is_key_pressed(KeyCode key) const;

    /// Caret rectangle for the currently focused text-input widget, when one exists.
    [[nodiscard]] std::optional<Rect> current_text_input_caret_rect() const;

    /// Full text-input state for the currently focused editable text widget, when one exists.
    [[nodiscard]] std::optional<WindowTextInputState> current_text_input_state() const;

    /// Get the inspector for this window.
    [[nodiscard]] WindowInspector& inspector();
    [[nodiscard]] const WindowInspector& inspector() const;

    // --- Signals ---

    /// Emitted just before the window hides. Notification-only — slots
    /// cannot veto the close; the window hides unconditionally after all
    /// slots return. Fires both when the application calls Window::close()
    /// and when the platform window manager initiates a close (e.g. title
    /// bar close button). Typical use: call Application::quit() or save
    /// state. For a "confirm before close" flow, intercept earlier — this
    /// signal cannot cancel a close that has already been requested.
    Signal<>& on_close_requested();

    /// Emitted when the window is resized.
    Signal<int, int>& on_resize();

    /// Emitted when the window content scale changes.
    Signal<float>& on_scale_factor_changed();

private:
    friend class Widget;
    friend class Dialog;
    friend class Headerbar;
    friend class WindowInspector;

    [[nodiscard]] TextShaper* text_shaper() const;
    // Pushes platform-configured default font families (GNOME font-name /
    // monospace-font-name) into the text shaper. No-op when the preferences are
    // absent or the shaper was not created.
    void apply_system_font_preferences_to_shaper(const SystemPreferences& preferences);
    [[nodiscard]] bool begin_system_move(std::uint32_t native_serial);
    void request_frame(FrameRequestReason reason);
    void perform_window_layout(Rect content_area);
    [[nodiscard]] std::unique_ptr<RenderNode> build_window_debug_render_tree(Size viewport_size,
                                                                             Rect content_area);
    void focus_widget(Widget* widget);
    void handle_widget_state_change(Widget& widget);
    void handle_widget_detached(Widget& widget);
    void note_widget_redraw_request(Widget& widget);
    void note_widget_layout_request(Widget& widget);
    [[nodiscard]] Widget* drag_target_at(Point point) const;
    void show_overlay(std::shared_ptr<Widget> overlay, bool modal);
    void dismiss_overlay(Widget& overlay);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
