#pragma once

/// @file window.h
/// @brief Top-level window abstraction.

#include <memory>
#include <nk/debug/diagnostics.h>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/platform/key_codes.h>
#include <nk/render/renderer.h>
#include <nk/ui_core/cursor_shape.h>
#include <span>
#include <string>
#include <string_view>

namespace nk {

class NativeSurface;
class TextShaper;
class Widget;
class Dialog;
class RenderNode;
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

    /// Active renderer backend for this window.
    [[nodiscard]] RendererBackend renderer_backend() const;

    /// Current cursor shape resolved from the hovered widget.
    [[nodiscard]] CursorShape current_cursor_shape() const;

    /// Whether the given key is currently pressed according to window input state.
    [[nodiscard]] bool is_key_pressed(KeyCode key) const;

    /// Configure built-in debug overlays for layout, dirtiness, and frame HUD output.
    void set_debug_overlay_flags(DebugOverlayFlags flags);
    [[nodiscard]] DebugOverlayFlags debug_overlay_flags() const;

    /// Choose whether the inspector is drawn as a floating overlay, a docked side pane,
    /// or a detachable sidecar-style pane.
    void set_debug_inspector_presentation(DebugInspectorPresentation presentation);
    [[nodiscard]] DebugInspectorPresentation debug_inspector_presentation() const;

    /// Frame diagnostics captured for the most recently rendered frame.
    [[nodiscard]] const FrameDiagnostics& last_frame_diagnostics() const;

    /// Recent frame diagnostics retained for the in-process inspector timeline.
    [[nodiscard]] std::span<const FrameDiagnostics> debug_frame_history() const;

    /// Build a debug snapshot of the current widget tree and overlays.
    [[nodiscard]] WidgetDebugNode debug_tree() const;

    /// Format the current widget tree into a readable text dump.
    [[nodiscard]] std::string dump_widget_tree() const;

    /// Format the current widget tree into versioned JSON.
    [[nodiscard]] std::string dump_widget_tree_json() const;

    /// Save the current widget tree as a versioned JSON file.
    [[nodiscard]] Result<void> save_widget_tree_json_file(std::string_view path) const;

    /// Export recent frame history as Chrome Trace JSON.
    [[nodiscard]] std::string dump_frame_trace_json() const;

    /// Save recent frame history as a versioned diagnostics artifact JSON file.
    [[nodiscard]] Result<void>
    save_frame_diagnostics_artifact_json_file(std::string_view path) const;

    /// Save recent frame history and runtime events as a trace JSON file.
    [[nodiscard]] Result<void> save_frame_trace_json_file(std::string_view path) const;

    /// Save a one-shot diagnostics bundle containing trace, widget tree, render snapshot,
    /// screenshot, and supporting summaries into a directory.
    [[nodiscard]] Result<void> save_debug_bundle(std::string_view directory_path) const;

    /// Runtime trace events correlated to the currently selected inspector frame.
    [[nodiscard]] std::vector<TraceEvent> debug_selected_frame_runtime_events() const;

    /// Format the currently selected inspector frame as a readable summary.
    [[nodiscard]] std::string dump_selected_frame_summary() const;

    /// Copy the selected-frame summary to the clipboard.
    [[nodiscard]] Result<void> copy_selected_frame_summary_to_clipboard() const;

    /// Save the selected-frame summary to a text file.
    [[nodiscard]] Result<void> save_selected_frame_summary_file(std::string_view path) const;

    /// Render-tree snapshot for the currently selected inspector frame.
    [[nodiscard]] RenderSnapshotNode debug_selected_frame_render_snapshot() const;

    /// Currently selected render node within the selected frame snapshot.
    [[nodiscard]] RenderSnapshotNode debug_selected_render_node() const;

    /// Format the selected frame's render snapshot as a readable tree dump.
    [[nodiscard]] std::string dump_selected_frame_render_snapshot() const;

    /// Format the selected frame's render snapshot as JSON.
    [[nodiscard]] std::string dump_selected_frame_render_snapshot_json() const;

    /// Format the selected render node as a readable summary.
    [[nodiscard]] std::string dump_selected_render_node_details() const;

    /// Copy the selected render-node summary to the clipboard.
    [[nodiscard]] Result<void> copy_selected_render_node_details_to_clipboard() const;

    /// Save the selected render-node summary to a text file.
    [[nodiscard]] Result<void> save_selected_render_node_details_file(std::string_view path) const;

    /// Enable a live widget picker that selects widgets under the pointer.
    void set_debug_picker_enabled(bool enabled);
    [[nodiscard]] bool debug_picker_enabled() const;

    /// Currently selected widget in the live inspector, if any.
    [[nodiscard]] Widget* debug_selected_widget() const;
    void set_debug_selected_widget(Widget* widget);
    void set_debug_widget_filter(std::string_view filter);
    [[nodiscard]] std::string_view debug_widget_filter() const;
    [[nodiscard]] WidgetDebugNode debug_selected_widget_info() const;

    /// Format the selected widget as a readable summary.
    [[nodiscard]] std::string dump_selected_widget_details() const;

    /// Format the selected widget as versioned JSON.
    [[nodiscard]] std::string dump_selected_widget_details_json() const;

    /// Copy the selected widget summary to the clipboard.
    [[nodiscard]] Result<void> copy_selected_widget_details_to_clipboard() const;

    /// Save the selected widget summary to a text file.
    [[nodiscard]] Result<void> save_selected_widget_details_file(std::string_view path) const;

    /// Save the selected widget details as a versioned JSON file.
    [[nodiscard]] Result<void> save_selected_widget_details_json_file(std::string_view path) const;

    /// Save the currently rendered debug surface to a PPM file when readback is available.
    [[nodiscard]] Result<void> save_debug_screenshot_ppm_file(std::string_view path) const;

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
    void request_frame(FrameRequestReason reason);
    void perform_window_layout(Rect content_area);
    [[nodiscard]] std::unique_ptr<RenderNode> build_window_debug_render_tree(Size viewport_size,
                                                                             Rect content_area);
    void sync_debug_selected_render_path();
    [[nodiscard]] Widget* debug_selected_render_widget() const;
    void sync_debug_selected_widget_from_render_selection();
    void focus_widget(Widget* widget);
    void handle_widget_state_change(Widget& widget);
    void handle_widget_detached(Widget& widget);
    void note_widget_redraw_request(Widget& widget);
    void note_widget_layout_request(Widget& widget);
    void show_overlay(std::shared_ptr<Widget> overlay, bool modal);
    void dismiss_overlay(Widget& overlay);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
