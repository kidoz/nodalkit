#pragma once

/// @file window.h
/// @brief Top-level window abstraction.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/platform/key_codes.h>
#include <nk/ui_core/cursor_shape.h>
#include <string>
#include <string_view>

namespace nk {

class NativeSurface;
class TextShaper;
class Widget;
class Dialog;
struct MouseEvent;
struct KeyEvent;
struct WindowEvent;

/// Configuration for window creation.
struct WindowConfig {
    std::string title;
    int width = 800;
    int height = 600;
    bool resizable = true;
    bool decorated = true;
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

    /// Set the root widget displayed in this window.
    void set_child(std::shared_ptr<Widget> child);

    /// Get the current root widget.
    [[nodiscard]] Widget* child() const;

    /// Show the window on screen.
    void present();

    /// Hide the window.
    void hide();

    /// Close the window by emitting a close-request notification and hiding it.
    void close();

    /// Whether the window is currently visible.
    [[nodiscard]] bool is_visible() const;

    /// Fullscreen mode.
    void set_fullscreen(bool fullscreen);
    [[nodiscard]] bool is_fullscreen() const;

    /// Request a frame to be rendered on the next idle.
    void request_frame();

    /// Mark the widget tree layout as dirty and schedule a redraw.
    void invalidate_layout();

    /// Deliver a platform event to this window.
    void dispatch_mouse_event(const MouseEvent& event);
    void dispatch_key_event(const KeyEvent& event);
    void dispatch_window_event(const WindowEvent& event);

    /// Access the native surface (nullptr until present() is called).
    [[nodiscard]] NativeSurface* native_surface() const;

    /// Current cursor shape resolved from the hovered widget.
    [[nodiscard]] CursorShape current_cursor_shape() const;

    /// Whether the given key is currently pressed according to window input state.
    [[nodiscard]] bool is_key_pressed(KeyCode key) const;

    // --- Signals ---

    /// Emitted when the window receives a close request.
    /// This is notification-only; connected slots cannot veto the close.
    Signal<>& on_close_request();

    /// Emitted when the window is resized.
    Signal<int, int>& on_resize();

private:
    friend class Widget;
    friend class Dialog;

    [[nodiscard]] TextShaper* text_shaper() const;
    void focus_widget(Widget* widget);
    void handle_widget_state_change(Widget& widget);
    void handle_widget_detached(Widget& widget);
    void show_overlay(std::shared_ptr<Widget> overlay, bool modal);
    void dismiss_overlay(Widget& overlay);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
