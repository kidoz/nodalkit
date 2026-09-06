#pragma once

#include <nk/debug/diagnostics.h>
#include <nk/foundation/result.h>
#include <nk/foundation/types.h>
#include <nk/render/renderer.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class Window;
class Widget;
class RenderNode;

/// A window frame captured for debugging. `rgba` is RGBA8, row-major,
/// top-left origin, `width * height * 4` bytes. `source_backend` names the
/// renderer whose output the pixels came from: the window's live renderer
/// when it can read its presented frame back, otherwise a software re-render
/// of the current scene.
struct DebugScreenshot {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    RendererBackend source_backend = RendererBackend::Software;
};

class WindowInspector {
public:
    explicit WindowInspector(Window& window);
    ~WindowInspector();

    WindowInspector(const WindowInspector&) = delete;
    WindowInspector& operator=(const WindowInspector&) = delete;

    void set_debug_overlay_flags(DebugOverlayFlags flags);
    [[nodiscard]] DebugOverlayFlags debug_overlay_flags() const;
    void set_debug_inspector_presentation(DebugInspectorPresentation presentation);
    [[nodiscard]] DebugInspectorPresentation debug_inspector_presentation() const;
    [[nodiscard]] const FrameDiagnostics& last_frame_diagnostics() const;
    [[nodiscard]] std::span<const FrameDiagnostics> debug_frame_history() const;
    [[nodiscard]] WidgetDebugNode debug_tree() const;
    [[nodiscard]] std::string dump_widget_tree() const;
    [[nodiscard]] std::string dump_widget_tree_json() const;
    [[nodiscard]] Result<void> save_widget_tree_json_file(std::string_view path) const;
    [[nodiscard]] std::string dump_frame_trace_json() const;
    [[nodiscard]] Result<void>
    save_frame_diagnostics_artifact_json_file(std::string_view path) const;
    [[nodiscard]] Result<void> save_frame_trace_json_file(std::string_view path) const;
    [[nodiscard]] Result<void> save_debug_bundle(std::string_view directory_path) const;
    [[nodiscard]] std::vector<TraceEvent> debug_selected_frame_runtime_events() const;
    [[nodiscard]] std::string dump_selected_frame_summary() const;
    [[nodiscard]] Result<void> copy_selected_frame_summary_to_clipboard() const;
    [[nodiscard]] Result<void> save_selected_frame_summary_file(std::string_view path) const;
    [[nodiscard]] RenderSnapshotNode debug_selected_frame_render_snapshot() const;
    [[nodiscard]] RenderSnapshotNode debug_selected_render_node() const;
    [[nodiscard]] std::string dump_selected_frame_render_snapshot() const;
    [[nodiscard]] std::string dump_selected_frame_render_snapshot_json() const;
    [[nodiscard]] std::string dump_selected_render_node_details() const;
    [[nodiscard]] Result<void> copy_selected_render_node_details_to_clipboard() const;
    [[nodiscard]] Result<void> save_selected_render_node_details_file(std::string_view path) const;
    void set_debug_picker_enabled(bool enabled);
    [[nodiscard]] bool debug_picker_enabled() const;
    [[nodiscard]] Widget* debug_selected_widget() const;
    void set_debug_selected_widget(Widget* widget);
    void set_debug_widget_filter(std::string_view filter);
    [[nodiscard]] std::string_view debug_widget_filter() const;
    [[nodiscard]] WidgetDebugNode debug_selected_widget_info() const;
    [[nodiscard]] std::string dump_selected_widget_details() const;
    [[nodiscard]] std::string dump_selected_widget_details_json() const;
    [[nodiscard]] Result<void> copy_selected_widget_details_to_clipboard() const;
    [[nodiscard]] Result<void> save_selected_widget_details_file(std::string_view path) const;
    [[nodiscard]] Result<void> save_selected_widget_details_json_file(std::string_view path) const;
    /// Capture the window contents. Prefers the live renderer's presented
    /// frame (including GPU backends that support readback) and falls back to
    /// a software re-render so a capture is always available.
    [[nodiscard]] Result<DebugScreenshot> capture_debug_screenshot() const;
    [[nodiscard]] Result<void> save_debug_screenshot_ppm_file(std::string_view path) const;

    void sync_debug_selected_render_path();
    [[nodiscard]] Widget* debug_selected_render_widget() const;
    void sync_debug_selected_widget_from_render_selection();

private:
    Window& window_;
};

} // namespace nk
