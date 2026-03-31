#pragma once

/// @file window.h
/// @brief Top-level window abstraction.

#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>

#include <memory>
#include <string>
#include <string_view>

namespace nk {

class NativeSurface;
class TextShaper;
class Widget;
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

    Window(Window const&) = delete;
    Window& operator=(Window const&) = delete;

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

    /// Close the window (triggers on_close_request).
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
    void dispatch_mouse_event(MouseEvent const& event);
    void dispatch_key_event(KeyEvent const& event);
    void dispatch_window_event(WindowEvent const& event);

    /// Access the native surface (nullptr until present() is called).
    [[nodiscard]] NativeSurface* native_surface() const;

    // --- Signals ---

    /// Emitted when the window is about to close.
    /// Returning true from a connected slot prevents the close.
    Signal<>& on_close_request();

    /// Emitted when the window is resized.
    Signal<int, int>& on_resize();

private:
    friend class Widget;

    [[nodiscard]] TextShaper* text_shaper() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
