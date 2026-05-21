#include <nk/platform/window_inspector.h>
#include "window_p.h"

namespace nk {

} // namespace nk

WindowInspector::WindowInspector(Window& window) : window_(window) {}
WindowInspector::~WindowInspector() = default;

void WindowInspector::set_debug_overlay_flags(DebugOverlayFlags flags) {
    if (window_.window_.impl_->debug_overlay_flags == flags) {
        return;
    }
    window_.window_.impl_->debug_overlay_flags = flags;
    if (!has_debug_overlay_flag(flags, DebugOverlayFlags::InspectorPanel)) {
        window_.window_.impl_->debug_picker_enabled = false;
    }
    window_.request_frame(FrameRequestReason::DebugOverlayChanged);
}

DebugOverlayFlags WindowInspector::debug_overlay_flags() const {
    return window_.window_.impl_->debug_overlay_flags;
}

void WindowInspector::set_debug_inspector_presentation(DebugInspectorPresentation presentation) {
    if (window_.window_.impl_->debug_inspector_presentation == presentation) {
        return;
    }
    window_.window_.impl_->debug_inspector_presentation = presentation;
    if (presentation == DebugInspectorPresentation::Detached &&
        !window_.window_.impl_->debug_detached_inspector_origin_initialized) {
        const auto viewport = window_.size();
        const float panel_width = std::min(600.0F, std::max(460.0F, viewport.width * 0.54F));
        window_.window_.impl_->debug_detached_inspector_origin = {
            std::max(12.0F, viewport.width - panel_width - 24.0F),
            24.0F,
        };
        window_.window_.impl_->debug_detached_inspector_origin_initialized = true;
    }
    window_.window_.impl_->debug_detached_inspector_drag_active = false;
    window_.window_.impl_->needs_layout = true;
    window_.request_frame(FrameRequestReason::DebugOverlayChanged);
}

DebugInspectorPresentation WindowInspector::debug_inspector_presentation() const {
    return window_.window_.impl_->debug_inspector_presentation;
}

const FrameDiagnostics& WindowInspector::last_frame_diagnostics() const {
    return window_.window_.impl_->last_frame_diagnostics;
}

std::span<const FrameDiagnostics> WindowInspector::debug_frame_history() const {
    return window_.window_.impl_->frame_history;
}

WidgetDebugNode WindowInspector::debug_tree() const {
    WidgetDebugNode root;
    root.type_name = "Window";
    root.debug_name = window_.window_.impl_->config.title;
    root.allocation = {0.0F, 0.0F, window_.size().width, window_.size().height};
    root.visible = window_.window_.impl_->visible;
    root.sensitive = true;
    root.focusable = false;
    if (window_.window_.impl_->child != nullptr) {
        root.children.push_back(build_widget_debug_node(*window_.window_.impl_->child,
                                                        {0},
                                                        window_.window_.impl_->focused_widget,
                                                        window_.window_.impl_->hovered_widget,
                                                        window_.window_.impl_->pressed_widget));
    }
    for (std::size_t overlay_index = 0; overlay_index < window_.window_.impl_->overlays.window_.size(); ++overlay_index) {
        const auto& overlay = window_.window_.impl_->overlays[overlay_index];
        if (overlay.widget != nullptr) {
            root.children.push_back(build_widget_debug_node(*overlay.widget,
                                                            {overlay_index + 1},
                                                            window_.window_.impl_->focused_widget,
                                                            window_.window_.impl_->hovered_widget,
                                                            window_.window_.impl_->pressed_widget));
        }
    }
    return root;
}

std::string WindowInspector::dump_widget_tree() const {
    return format_widget_debug_tree(debug_tree());
}

std::string WindowInspector::dump_widget_tree_json() const {
    return format_widget_debug_json(debug_tree());
}

Result<void> WindowInspector::save_widget_tree_json_file(std::string_view path) const {
    return save_widget_debug_json_file(debug_tree(), path);
}

std::string WindowInspector::dump_frame_trace_json() const {
    if (auto* app = Application::instance()) {
        return format_frame_diagnostics_trace_json(window_.window_.impl_->frame_history,
                                                   app->event_loop().debug_trace_events());
    }
    return format_frame_diagnostics_trace_json(window_.window_.impl_->frame_history);
}

Result<void> WindowInspector::save_frame_diagnostics_artifact_json_file(std::string_view path) const {
    return nk::save_frame_diagnostics_artifact_json_file(
        FrameDiagnosticsArtifact{.frames = window_.window_.impl_->frame_history}, path);
}

Result<void> WindowInspector::save_frame_trace_json_file(std::string_view path) const {
    return save_text_file(path, dump_frame_trace_json());
}

Result<void> WindowInspector::save_debug_bundle(std::string_view directory_path) const {
    const auto ensure_dir = ensure_directory_exists(directory_path);
    if (!ensure_dir) {
        return ensure_dir;
    }

    const auto bundle_dir = std::filesystem::path(directory_path);
    const auto widget_tree_path = bundle_dir / "widget_tree.txt";
    const auto widget_tree_json_path = bundle_dir / "widget_tree.json";
    const auto frame_trace_path = bundle_dir / "frame_trace.json";
    const auto frame_artifact_path = bundle_dir / "frame_diagnostics.json";
    const auto render_snapshot_path = bundle_dir / "render_snapshot.json";
    const auto frame_summary_path = bundle_dir / "selected_frame_summary.txt";
    const auto selected_widget_path = bundle_dir / "selected_widget.json";
    const auto screenshot_path = bundle_dir / "screenshot.ppm";
    const auto manifest_path = bundle_dir / "manifest.json";

    if (const auto result = save_text_file(widget_tree_path.string(), dump_widget_tree());
        !result) {
        return result;
    }
    if (const auto result = save_widget_tree_json_file(widget_tree_json_path.string()); !result) {
        return result;
    }
    if (const auto result = save_frame_trace_json_file(frame_trace_path.string()); !result) {
        return result;
    }
    if (const auto result = save_frame_diagnostics_artifact_json_file(frame_artifact_path.string());
        !result) {
        return result;
    }
    if (const auto result = save_render_snapshot_json_file(debug_selected_frame_render_snapshot(),
                                                           render_snapshot_path.string());
        !result) {
        return result;
    }
    if (const auto result =
            save_text_file(frame_summary_path.string(), dump_selected_frame_summary());
        !result) {
        return result;
    }
    if (const auto result = save_selected_widget_details_json_file(selected_widget_path.string());
        !result) {
        return result;
    }
    if (const auto result = save_debug_screenshot_ppm_file(screenshot_path.string()); !result) {
        return result;
    }

    std::ostringstream manifest;
    manifest << "{\n";
    manifest << "  \"format\": \"nk-debug-bundle-v1\",\n";
    manifest << "  \"title\": \"" << window_.window_.impl_->config.title << "\",\n";
    manifest << "  \"renderer_backend\": \"" << renderer_backend_name(renderer_backend())
             << "\",\n";
    manifest << "  \"files\": {\n";
    manifest << "    \"widget_tree\": \"" << widget_tree_path.filename().string() << "\",\n";
    manifest << "    \"widget_tree_json\": \"" << widget_tree_json_path.filename().string()
             << "\",\n";
    manifest << "    \"frame_trace\": \"" << frame_trace_path.filename().string() << "\",\n";
    manifest << "    \"frame_diagnostics\": \"" << frame_artifact_path.filename().string()
             << "\",\n";
    manifest << "    \"render_snapshot\": \"" << render_snapshot_path.filename().string()
             << "\",\n";
    manifest << "    \"frame_summary\": \"" << frame_summary_path.filename().string() << "\",\n";
    manifest << "    \"selected_widget\": \"" << selected_widget_path.filename().string()
             << "\",\n";
    manifest << "    \"screenshot\": \"" << screenshot_path.filename().string() << "\"\n";
    manifest << "  }\n";
    manifest << "}\n";
    return save_text_file(manifest_path.string(), manifest.str());
}

std::vector<TraceEvent> WindowInspector::debug_selected_frame_runtime_events() const {
    const auto* frame =
        selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
    if (frame == nullptr) {
        return {};
    }
    if (auto* app = Application::instance()) {
        return collect_frame_runtime_events(*frame, app->event_loop().debug_trace_events());
    }
    return {};
}

std::string WindowInspector::dump_selected_frame_summary() const {
    const auto* frame =
        selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
    if (frame == nullptr) {
        return "No frame selected";
    }
    if (auto* app = Application::instance()) {
        return format_selected_frame_summary_text(*frame, app->event_loop().debug_trace_events());
    }
    return format_selected_frame_summary_text(*frame, {});
}

Result<void> WindowInspector::copy_selected_frame_summary_to_clipboard() const {
    return copy_text_to_application_clipboard(dump_selected_frame_summary());
}

Result<void> WindowInspector::save_selected_frame_summary_file(std::string_view path) const {
    return save_text_file(path, dump_selected_frame_summary());
}

RenderSnapshotNode WindowInspector::debug_selected_frame_render_snapshot() const {
    if (const auto* frame =
            selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
        frame != nullptr) {
        return frame->render_snapshot;
    }
    return {};
}

RenderSnapshotNode WindowInspector::debug_selected_render_node() const {
    if (const auto* frame =
            selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
        frame != nullptr) {
        if (const auto* node =
                selected_render_node_from_frame(frame, window_.window_.impl_->debug_selected_render_path);
            node != nullptr) {
            return *node;
        }
    }
    return {};
}

std::string WindowInspector::dump_selected_frame_render_snapshot() const {
    if (const auto* frame =
            selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
        frame != nullptr) {
        return format_render_snapshot_tree(frame->render_snapshot);
    }
    return {};
}

std::string WindowInspector::dump_selected_frame_render_snapshot_json() const {
    if (const auto* frame =
            selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
        frame != nullptr) {
        return format_render_snapshot_json(frame->render_snapshot);
    }
    return {};
}

std::string WindowInspector::dump_selected_render_node_details() const {
    const auto* frame =
        selected_history_frame(window_.window_.impl_->frame_history, window_.window_.impl_->debug_selected_frame_id);
    if (frame == nullptr) {
        return "No frame selected";
    }
    return format_selected_render_node_text(
        selected_render_node_from_frame(frame, window_.window_.impl_->debug_selected_render_path));
}

Result<void> WindowInspector::copy_selected_render_node_details_to_clipboard() const {
    return copy_text_to_application_clipboard(dump_selected_render_node_details());
}

Result<void> WindowInspector::save_selected_render_node_details_file(std::string_view path) const {
    return save_text_file(path, dump_selected_render_node_details());
}

void WindowInspector::set_debug_picker_enabled(bool enabled) {
    if (window_.window_.impl_->debug_picker_enabled == enabled) {
        return;
    }
    window_.window_.impl_->debug_picker_enabled = enabled;
    if (enabled) {
        window_.window_.impl_->debug_overlay_flags |= DebugOverlayFlags::InspectorPanel;
    }
    if (window_.window_.impl_->surface != nullptr) {
        window_.window_.impl_->surface->set_cursor_shape(enabled ? CursorShape::PointingHand
                                                 : window_.window_.impl_->current_cursor_shape);
    }
    window_.request_frame(FrameRequestReason::PickerChanged);
}

bool WindowInspector::debug_picker_enabled() const {
    return window_.window_.impl_->debug_picker_enabled;
}

Widget* WindowInspector::debug_selected_widget() const {
    return window_.window_.impl_->debug_selected_widget;
}

void WindowInspector::set_debug_selected_widget(Widget* widget) {
    auto is_attached_to_window = [&window_](const Widget* candidate) {
        if (candidate == nullptr) {
            return false;
        }
        if (is_attached_to_window_root(candidate, window_.window_.impl_->child)) {
            return true;
        }
        for (const auto& overlay : window_.window_.impl_->overlays) {
            if (overlay.widget != nullptr && is_descendant_of(candidate, overlay.widget.get())) {
                return true;
            }
        }
        return false;
    };

    if (widget != nullptr && !is_attached_to_window(widget)) {
        widget = nullptr;
    }
    if (window_.window_.impl_->debug_selected_widget == widget) {
        return;
    }
    window_.window_.impl_->debug_selected_widget = widget;
    sync_debug_selected_render_path();
    window_.request_frame(FrameRequestReason::DebugSelectionChanged);
}

void WindowInspector::set_debug_widget_filter(std::string_view filter) {
    const std::string normalized = lowercase_copy(filter);
    if (window_.window_.impl_->debug_widget_filter == normalized) {
        return;
    }
    window_.window_.impl_->debug_widget_filter = normalized;

    std::vector<std::shared_ptr<Widget>> overlay_widgets;
    overlay_widgets.reserve(window_.window_.impl_->overlays.window_.size());
    for (const auto& overlay : window_.window_.impl_->overlays) {
        overlay_widgets.push_back(overlay.widget);
    }
    const auto entries =
        build_inspector_entries(window_.window_.impl_->child.get(), overlay_widgets, window_.window_.impl_->debug_widget_filter);
    window_.window_.impl_->debug_selected_widget =
        normalized_selected_widget(entries, window_.window_.impl_->debug_selected_widget);
    sync_debug_selected_render_path();
    window_.request_frame(FrameRequestReason::DebugSelectionChanged);
}

std::string_view WindowInspector::debug_widget_filter() const {
    return window_.window_.impl_->debug_widget_filter;
}

WidgetDebugNode WindowInspector::debug_selected_widget_info() const {
    return window_.window_.impl_->debug_selected_widget != nullptr
               ? build_widget_debug_node(*window_.window_.impl_->debug_selected_widget,
                                         window_.window_.impl_->debug_selected_widget->debug_tree_path(),
                                         window_.window_.impl_->focused_widget,
                                         window_.window_.impl_->hovered_widget,
                                         window_.window_.impl_->pressed_widget)
               : WidgetDebugNode{};
}

std::string WindowInspector::dump_selected_widget_details() const {
    return format_widget_panel_text(window_.window_.impl_->debug_selected_widget,
                                    window_.window_.impl_->focused_widget,
                                    window_.window_.impl_->hovered_widget,
                                    window_.window_.impl_->pressed_widget);
}

std::string WindowInspector::dump_selected_widget_details_json() const {
    return format_widget_debug_json(debug_selected_widget_info());
}

Result<void> WindowInspector::copy_selected_widget_details_to_clipboard() const {
    return copy_text_to_application_clipboard(dump_selected_widget_details());
}

Result<void> WindowInspector::save_selected_widget_details_file(std::string_view path) const {
    return save_text_file(path, dump_selected_widget_details());
}

Result<void> WindowInspector::save_selected_widget_details_json_file(std::string_view path) const {
    return save_widget_debug_json_file(debug_selected_widget_info(), path);
}

Result<void> WindowInspector::save_debug_screenshot_ppm_file(std::string_view path) const {
    const auto* renderer = dynamic_cast<const SoftwareRenderer*>(window_.window_.impl_->renderer.get());
    if (renderer == nullptr) {
        if (window_.window_.impl_->child == nullptr) {
            return Unexpected(std::string("debug screenshot capture requires a root widget"));
        }

        const auto viewport_size = window_.size();
        const float scale_factor =
            window_.window_.impl_->surface != nullptr ? window_.window_.impl_->surface->scale_factor() : 1.0F;
        const auto content_area = compute_debug_content_area(
            viewport_size, window_.window_.impl_->debug_overlay_flags, window_.window_.impl_->debug_inspector_presentation);

        auto* self = const_cast<Window*>(&window_);
        if (window_.window_.impl_->needs_layout) {
            self->perform_window_layout(content_area);
        }

        auto root_node = self->build_window_debug_render_tree(viewport_size, content_area);
        auto screenshot_renderer = create_renderer(RendererBackend::Software);
        auto* software = dynamic_cast<SoftwareRenderer*>(screenshot_renderer.get());
        if (software == nullptr) {
            return Unexpected(std::string("failed to create software renderer for screenshot"));
        }
        if (window_.window_.impl_->text_shaper != nullptr) {
            software->set_text_shaper(window_.window_.impl_->text_shaper.get());
        }
        software->begin_frame(viewport_size, scale_factor);
        if (root_node) {
            software->render(*root_node);
        }
        software->end_frame();
        return save_ppm_file(
            path, software->pixel_data(), software->pixel_width(), software->pixel_height());
    }
    return save_ppm_file(
        path, renderer->pixel_data(), renderer->pixel_width(), renderer->pixel_height());
}

