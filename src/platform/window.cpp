#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <optional>
#include <nk/controllers/event_controller.h>
#include <nk/debug/diagnostics.h>
#include <nk/foundation/logging.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>
#include <nk/ui_core/widget.h>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

namespace nk {

struct Window::Impl {
    struct OverlayEntry {
        std::shared_ptr<Widget> widget;
        bool modal = false;
        std::weak_ptr<Widget> previous_focus;
    };

    WindowConfig config;
    std::shared_ptr<Widget> child;
    std::vector<OverlayEntry> overlays;
    std::unique_ptr<NativeSurface> surface;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<TextShaper> text_shaper;
    Widget* hovered_widget = nullptr;
    Widget* focused_widget = nullptr;
    Widget* pressed_widget = nullptr;
    CursorShape current_cursor_shape = CursorShape::Default;
    std::weak_ptr<Widget> pending_focus_restore;
    std::array<bool, 256> key_state{};
    DebugOverlayFlags debug_overlay_flags = DebugOverlayFlags::None;
    FrameDiagnostics last_frame_diagnostics;
    std::vector<FrameDiagnostics> frame_history;
    std::vector<Widget*> dirty_widgets;
    Widget* debug_selected_widget = nullptr;
    uint64_t debug_selected_frame_id = 0;
    std::vector<std::size_t> debug_selected_render_path;
    bool debug_picker_enabled = false;
    std::size_t redraw_request_count = 0;
    std::size_t layout_request_count = 0;
    uint64_t next_frame_id = 1;
    std::chrono::steady_clock::time_point diagnostics_origin = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point pending_frame_requested_at = diagnostics_origin;
    std::vector<FrameRequestReason> pending_frame_reasons;

    bool visible = false;
    bool needs_layout = true;
    bool frame_pending = false;

    Signal<> close_request;
    Signal<int, int> resize_signal;
};

namespace {

std::optional<RendererBackend> parse_renderer_backend_override(std::string_view value) {
    if (value == "software") {
        return RendererBackend::Software;
    }
    if (value == "metal") {
        return RendererBackend::Metal;
    }
    if (value == "opengl" || value == "open_gl" || value == "gl") {
        return RendererBackend::OpenGL;
    }
    if (value == "vulkan" || value == "vk") {
        return RendererBackend::Vulkan;
    }
    return std::nullopt;
}

std::optional<RendererBackend> renderer_backend_override_from_environment() {
    const char* env = std::getenv("NK_RENDERER_BACKEND");
    if (env == nullptr || *env == '\0') {
        return std::nullopt;
    }

    const auto requested = parse_renderer_backend_override(env);
    if (!requested.has_value()) {
        NK_LOG_WARN("Window",
                    "Ignoring unknown NK_RENDERER_BACKEND override; expected software, metal, "
                    "opengl, or vulkan");
        return std::nullopt;
    }

    return requested;
}

struct InspectorEntry {
    Widget* widget = nullptr;
    std::size_t depth = 0;
    std::string label;
};

struct RenderInspectorEntry {
    const RenderSnapshotNode* node = nullptr;
    std::size_t depth = 0;
    std::vector<std::size_t> path;
    std::string label;
};

[[nodiscard]] bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

[[nodiscard]] Rect intersect_rect(Rect lhs, Rect rhs) {
    const float x0 = std::max(lhs.x, rhs.x);
    const float y0 = std::max(lhs.y, rhs.y);
    const float x1 = std::min(lhs.right(), rhs.right());
    const float y1 = std::min(lhs.bottom(), rhs.bottom());
    return {
        x0,
        y0,
        std::max(0.0F, x1 - x0),
        std::max(0.0F, y1 - y0),
    };
}

[[nodiscard]] bool rects_overlap_or_touch(Rect lhs, Rect rhs) {
    return lhs.x <= rhs.right() && lhs.right() >= rhs.x && lhs.y <= rhs.bottom() &&
           lhs.bottom() >= rhs.y;
}

[[nodiscard]] Rect union_rect(Rect lhs, Rect rhs) {
    const float x0 = std::min(lhs.x, rhs.x);
    const float y0 = std::min(lhs.y, rhs.y);
    const float x1 = std::max(lhs.right(), rhs.right());
    const float y1 = std::max(lhs.bottom(), rhs.bottom());
    return {
        x0,
        y0,
        std::max(0.0F, x1 - x0),
        std::max(0.0F, y1 - y0),
    };
}

[[nodiscard]] std::vector<Rect> collect_damage_regions(std::span<Widget* const> widgets,
                                                       Size viewport) {
    const Rect viewport_rect{0.0F, 0.0F, viewport.width, viewport.height};
    std::vector<Rect> merged;
    merged.reserve(widgets.size());

    for (auto* widget : widgets) {
        if (widget == nullptr || !widget->is_visible()) {
            continue;
        }

        Rect damage = intersect_rect(widget->allocation(), viewport_rect);
        if (rect_is_empty(damage)) {
            continue;
        }

        bool merged_existing = false;
        for (auto& existing : merged) {
            if (rects_overlap_or_touch(existing, damage)) {
                existing = union_rect(existing, damage);
                merged_existing = true;
                break;
            }
        }

        if (!merged_existing) {
            merged.push_back(damage);
        }
    }

    for (std::size_t outer = 0; outer < merged.size(); ++outer) {
        for (std::size_t inner = outer + 1; inner < merged.size();) {
            if (rects_overlap_or_touch(merged[outer], merged[inner])) {
                merged[outer] = union_rect(merged[outer], merged[inner]);
                merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(inner));
            } else {
                ++inner;
            }
        }
    }

    return merged;
}

struct InspectorPanelLayout {
    Rect panel{};
    Rect timeline_area{};
    Rect tree_area{};
    Rect render_tree_area{};
    Rect detail_area{};
    float tree_row_height = 16.0F;
    float tree_top = 0.0F;
    std::size_t visible_start = 0;
    std::size_t visible_count = 0;
    float render_tree_top = 0.0F;
    std::size_t render_visible_start = 0;
    std::size_t render_visible_count = 0;
};

constexpr std::size_t kDebugFrameHistoryLimit = 48;

RendererBackend select_renderer_backend(const NativeSurface* surface) {
    const auto support =
        surface != nullptr ? surface->renderer_backend_support() : RendererBackendSupport{};
    if (const auto override_backend = renderer_backend_override_from_environment();
        override_backend.has_value()) {
        const auto backend = *override_backend;
        if (!renderer_backend_supported(support, backend)) {
            NK_LOG_WARN("Window",
                        "Requested renderer backend is not supported by the active surface; "
                        "falling back to automatic selection");
        } else {
            if (!renderer_backend_available(backend)) {
                NK_LOG_WARN("Window",
                            "Requested renderer backend bypasses normal availability checks; "
                            "treating it as an experimental override");
            }
            return backend;
        }
    }

    constexpr RendererBackend preferred_backends[] = {
        RendererBackend::Metal,
        RendererBackend::Vulkan,
        RendererBackend::OpenGL,
        RendererBackend::Software,
    };
    for (const auto backend : preferred_backends) {
        if (renderer_backend_supported(support, backend) && renderer_backend_available(backend)) {
            return backend;
        }
    }
    return RendererBackend::Software;
}

Widget* hit_test_widget(Widget* widget, Point point) {
    if (widget == nullptr || !widget->is_visible() || !widget->hit_test(point)) {
        return nullptr;
    }

    const auto children = widget->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (*it == nullptr) {
            continue;
        }
        if (auto* hit = hit_test_widget(it->get(), point)) {
            return hit;
        }
    }

    return widget;
}

bool is_descendant_of(const Widget* widget, const Widget* ancestor) {
    auto* current = widget;
    while (current != nullptr) {
        if (current == ancestor) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

bool is_attached_to_window_root(const Widget* widget, const std::shared_ptr<Widget>& root) {
    return widget != nullptr && root != nullptr && is_descendant_of(widget, root.get());
}

Widget* find_widget_by_debug_tree_path(Widget* root, std::span<const std::size_t> path) {
    if (root == nullptr) {
        return nullptr;
    }
    auto* current = root;
    for (const auto child_index : path) {
        const auto children = current->children();
        if (child_index >= children.size() || children[child_index] == nullptr) {
            return nullptr;
        }
        current = children[child_index].get();
    }
    return current;
}

std::vector<Widget*> widget_path_to_root(Widget* widget) {
    std::vector<Widget*> path;
    for (auto* current = widget; current != nullptr; current = current->parent()) {
        path.push_back(current);
    }
    return path;
}

void collect_focusable_widgets(Widget* widget, std::vector<Widget*>& out) {
    if (widget == nullptr || !widget->is_visible() || !widget->is_sensitive()) {
        return;
    }

    if (widget->is_focusable()) {
        out.push_back(widget);
    }

    for (const auto& child : widget->children()) {
        if (child != nullptr) {
            collect_focusable_widgets(child.get(), out);
        }
    }
}

bool can_focus_widget(const Widget* widget) {
    return widget != nullptr && widget->is_focusable() && widget->is_visible() &&
           widget->is_sensitive();
}

void append_unique_widget(std::vector<Widget*>& widgets, Widget* widget) {
    if (widget == nullptr) {
        return;
    }
    if (std::find(widgets.begin(), widgets.end(), widget) == widgets.end()) {
        widgets.push_back(widget);
    }
}

void append_unique_reason(std::vector<FrameRequestReason>& reasons, FrameRequestReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

void accumulate_widget_hotspot_counters(WidgetHotspotCounters& total,
                                        const WidgetHotspotCounters& value) {
    total.measure_count += value.measure_count;
    total.allocate_count += value.allocate_count;
    total.snapshot_count += value.snapshot_count;
    total.text_measure_count += value.text_measure_count;
    total.image_snapshot_count += value.image_snapshot_count;
    total.model_sync_count += value.model_sync_count;
    total.model_row_materialize_count += value.model_row_materialize_count;
    total.model_row_reuse_count += value.model_row_reuse_count;
    total.model_row_dispose_count += value.model_row_dispose_count;
}

WidgetHotspotCounters collect_widget_hotspot_totals(const Widget* widget) {
    WidgetHotspotCounters total;
    if (widget == nullptr) {
        return total;
    }

    accumulate_widget_hotspot_counters(total, widget->debug_hotspot_counters());
    for (const auto& child : widget->children()) {
        if (child != nullptr) {
            accumulate_widget_hotspot_counters(total, collect_widget_hotspot_totals(child.get()));
        }
    }
    return total;
}

std::size_t count_widgets_recursive(const Widget* widget) {
    if (widget == nullptr) {
        return 0;
    }

    std::size_t count = 1;
    for (const auto& child : widget->children()) {
        if (child != nullptr) {
            count += count_widgets_recursive(child.get());
        }
    }
    return count;
}

std::size_t count_render_nodes_recursive(const RenderNode* node) {
    if (node == nullptr) {
        return 0;
    }

    std::size_t count = 1;
    for (const auto& child : node->children()) {
        if (child != nullptr) {
            count += count_render_nodes_recursive(child.get());
        }
    }
    return count;
}

std::string debug_type_name(const Widget& widget) {
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(widget).name(), nullptr, nullptr, &status);
    std::string result = (status == 0 && demangled != nullptr) ? demangled : typeid(widget).name();
    std::free(demangled);
    return result;
#else
    return typeid(widget).name();
#endif
}

WidgetDebugNode build_widget_debug_node(const Widget& widget) {
    WidgetDebugNode node;
    node.type_name = debug_type_name(widget);
    node.debug_name = std::string(widget.debug_name());
    node.allocation = widget.allocation();
    node.state_flags = widget.state_flags();
    node.visible = widget.is_visible();
    node.sensitive = widget.is_sensitive();
    node.focusable = widget.is_focusable();
    node.hotspot_counters = widget.debug_hotspot_counters();
    const auto classes = widget.style_classes();
    node.style_classes.assign(classes.begin(), classes.end());
    node.children.reserve(widget.children().size());
    for (const auto& child : widget.children()) {
        if (child != nullptr) {
            node.children.push_back(build_widget_debug_node(*child));
        }
    }
    return node;
}

std::string truncate_for_overlay(std::string value, std::size_t max_chars) {
    if (value.size() <= max_chars) {
        return value;
    }
    if (max_chars <= 3) {
        return value.substr(0, max_chars);
    }
    return value.substr(0, max_chars - 3) + "...";
}

std::string widget_label_for_inspector(const Widget& widget) {
    auto type = debug_type_name(widget);
    if (!widget.debug_name().empty()) {
        type += " \"";
        type += std::string(widget.debug_name());
        type += "\"";
    }
    return type;
}

void collect_inspector_entries(Widget* widget,
                               std::size_t depth,
                               std::vector<InspectorEntry>& out) {
    if (widget == nullptr) {
        return;
    }

    out.push_back({
        .widget = widget,
        .depth = depth,
        .label = widget_label_for_inspector(*widget),
    });

    for (const auto& child : widget->children()) {
        if (child != nullptr) {
            collect_inspector_entries(child.get(), depth + 1, out);
        }
    }
}

std::vector<InspectorEntry>
build_inspector_entries(Widget* root, std::span<const std::shared_ptr<Widget>> overlays) {
    std::vector<InspectorEntry> entries;
    if (root != nullptr) {
        collect_inspector_entries(root, 0, entries);
    }
    for (const auto& overlay : overlays) {
        if (overlay != nullptr) {
            collect_inspector_entries(overlay.get(), 0, entries);
        }
    }
    return entries;
}

void collect_render_inspector_entries(const RenderSnapshotNode& node,
                                      std::size_t depth,
                                      std::vector<std::size_t>& path,
                                      std::vector<RenderInspectorEntry>& out) {
    std::string label = node.kind;
    if (!node.detail.empty()) {
        label += " ";
        label += node.detail;
    }
    out.push_back({
        .node = &node,
        .depth = depth,
        .path = path,
        .label = std::move(label),
    });

    for (std::size_t index = 0; index < node.children.size(); ++index) {
        path.push_back(index);
        collect_render_inspector_entries(node.children[index], depth + 1, path, out);
        path.pop_back();
    }
}

std::vector<RenderInspectorEntry> build_render_inspector_entries(const RenderSnapshotNode& root) {
    std::vector<RenderInspectorEntry> entries;
    std::vector<std::size_t> path;
    collect_render_inspector_entries(root, 0, path, entries);
    return entries;
}

std::size_t selected_entry_index(const std::vector<InspectorEntry>& entries,
                                 Widget* selected_widget) {
    if (selected_widget == nullptr) {
        return 0;
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].widget == selected_widget) {
            return index;
        }
    }
    return 0;
}

std::size_t selected_render_entry_index(const std::vector<RenderInspectorEntry>& entries,
                                        std::span<const std::size_t> selected_path) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& path = entries[index].path;
        if (path.size() != selected_path.size()) {
            continue;
        }
        if (std::equal(path.begin(), path.end(), selected_path.begin(), selected_path.end())) {
            return index;
        }
    }
    return 0;
}

bool is_navigable_inspector_entry(const InspectorEntry& entry) {
    return entry.depth > 0 || !entry.widget->debug_name().empty();
}

std::size_t first_navigable_entry_index(const std::vector<InspectorEntry>& entries) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (is_navigable_inspector_entry(entries[index])) {
            return index;
        }
    }
    return 0;
}

std::size_t last_navigable_entry_index(const std::vector<InspectorEntry>& entries) {
    for (std::size_t index = entries.size(); index > 0; --index) {
        if (is_navigable_inspector_entry(entries[index - 1])) {
            return index - 1;
        }
    }
    return entries.empty() ? 0 : entries.size() - 1;
}

bool is_navigable_render_entry(const RenderInspectorEntry& entry) {
    return !entry.path.empty();
}

std::size_t first_navigable_render_entry_index(const std::vector<RenderInspectorEntry>& entries) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (is_navigable_render_entry(entries[index])) {
            return index;
        }
    }
    return 0;
}

std::size_t last_navigable_render_entry_index(const std::vector<RenderInspectorEntry>& entries) {
    for (std::size_t index = entries.size(); index > 0; --index) {
        if (is_navigable_render_entry(entries[index - 1])) {
            return index - 1;
        }
    }
    return entries.empty() ? 0 : entries.size() - 1;
}

std::size_t selected_history_index(std::span<const FrameDiagnostics> history, uint64_t frame_id) {
    if (history.empty()) {
        return 0;
    }
    if (frame_id == 0) {
        return history.size() - 1;
    }
    for (std::size_t index = 0; index < history.size(); ++index) {
        if (history[index].frame_id == frame_id) {
            return index;
        }
    }
    return history.size() - 1;
}

const FrameDiagnostics* selected_history_frame(std::span<const FrameDiagnostics> history,
                                               uint64_t frame_id) {
    if (history.empty()) {
        return nullptr;
    }
    return &history[selected_history_index(history, frame_id)];
}

const RenderSnapshotNode* selected_render_node_from_frame(const FrameDiagnostics* frame,
                                                          std::span<const std::size_t> path) {
    if (frame == nullptr) {
        return nullptr;
    }
    const RenderSnapshotNode* current = &frame->render_snapshot;
    for (const auto child_index : path) {
        if (child_index >= current->children.size()) {
            return &frame->render_snapshot;
        }
        current = &current->children[child_index];
    }
    return current;
}

bool find_render_path_for_widget(const RenderSnapshotNode& node,
                                 std::span<const std::size_t> widget_path,
                                 std::vector<std::size_t>& current_path,
                                 std::vector<std::size_t>& result_path) {
    if (node.source_widget_path.size() == widget_path.size() &&
        std::equal(node.source_widget_path.begin(),
                   node.source_widget_path.end(),
                   widget_path.begin(),
                   widget_path.end())) {
        result_path = current_path;
        return true;
    }
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        current_path.push_back(index);
        if (find_render_path_for_widget(
                node.children[index], widget_path, current_path, result_path)) {
            return true;
        }
        current_path.pop_back();
    }
    return false;
}

InspectorPanelLayout compute_inspector_panel_layout(Size viewport,
                                                    std::size_t entry_count,
                                                    std::size_t selected_index,
                                                    std::size_t render_entry_count,
                                                    std::size_t render_selected_index) {
    InspectorPanelLayout layout;
    const float panel_width = std::min(560.0F, std::max(420.0F, viewport.width * 0.52F));
    const float panel_height = std::min(340.0F, std::max(220.0F, viewport.height - 116.0F));
    layout.panel = {
        viewport.width - panel_width - 12.0F,
        112.0F,
        panel_width,
        panel_height,
    };

    layout.timeline_area = {
        layout.panel.x + 12.0F,
        layout.panel.y + 30.0F,
        layout.panel.width - 24.0F,
        26.0F,
    };

    const float body_top = layout.timeline_area.bottom() + 14.0F;
    const float body_height =
        std::max(0.0F, layout.panel.height - (body_top - layout.panel.y) - 12.0F);
    const float tree_width = std::max(110.0F, layout.panel.width * 0.27F);
    const float render_tree_width = std::max(110.0F, layout.panel.width * 0.29F);
    layout.tree_area = {
        layout.panel.x + 12.0F,
        body_top,
        tree_width,
        body_height,
    };
    layout.render_tree_area = {
        layout.tree_area.right() + 12.0F,
        body_top,
        render_tree_width,
        body_height,
    };
    layout.detail_area = {
        layout.render_tree_area.right() + 12.0F,
        body_top,
        std::max(0.0F, layout.panel.right() - (layout.render_tree_area.right() + 24.0F)),
        body_height,
    };
    layout.tree_top = layout.tree_area.y + 8.0F;
    layout.render_tree_top = layout.render_tree_area.y + 8.0F;
    layout.tree_row_height = 16.0F;

    const auto raw_rows = static_cast<std::size_t>(
        std::max(0.0F, std::floor((layout.tree_area.height - 14.0F) / layout.tree_row_height)));
    const std::size_t max_rows = std::max<std::size_t>(1, raw_rows);
    layout.visible_count = std::min(entry_count, max_rows);
    layout.render_visible_count = std::min(render_entry_count, max_rows);

    if (entry_count <= layout.visible_count) {
        layout.visible_start = 0;
    } else {
        const std::size_t center = layout.visible_count / 2;
        std::size_t start = selected_index > center ? selected_index - center : 0;
        if (start + layout.visible_count > entry_count) {
            start = entry_count - layout.visible_count;
        }
        layout.visible_start = start;
    }

    if (render_entry_count <= layout.render_visible_count) {
        layout.render_visible_start = 0;
    } else {
        const std::size_t center = layout.render_visible_count / 2;
        std::size_t start = render_selected_index > center ? render_selected_index - center : 0;
        if (start + layout.render_visible_count > render_entry_count) {
            start = render_entry_count - layout.render_visible_count;
        }
        layout.render_visible_start = start;
    }
    return layout;
}

std::size_t compute_visible_history_start(std::size_t history_count,
                                          std::size_t selected_index,
                                          std::size_t max_visible) {
    if (history_count <= max_visible) {
        return 0;
    }
    const std::size_t center = max_visible / 2;
    std::size_t start = selected_index > center ? selected_index - center : 0;
    if (start + max_visible > history_count) {
        start = history_count - max_visible;
    }
    return start;
}

uint64_t frame_id_at_timeline_point(std::span<const FrameDiagnostics> history,
                                    uint64_t selected_frame_id,
                                    const InspectorPanelLayout& layout,
                                    Point point) {
    if (history.empty() || !layout.timeline_area.contains(point)) {
        return 0;
    }

    const std::size_t max_visible = std::min<std::size_t>(history.size(), 24);
    const std::size_t selected_index = selected_history_index(history, selected_frame_id);
    const std::size_t visible_start =
        compute_visible_history_start(history.size(), selected_index, max_visible);
    const float gap = 3.0F;
    const float available_width = layout.timeline_area.width - gap * (max_visible - 1);
    if (available_width <= 0.0F) {
        return history.back().frame_id;
    }
    const float bar_width = available_width / static_cast<float>(max_visible);
    const float relative_x = point.x - layout.timeline_area.x;
    if (relative_x < 0.0F) {
        return 0;
    }
    const float stride = bar_width + gap;
    const auto slot = static_cast<std::size_t>(std::floor(relative_x / stride));
    if (slot >= max_visible) {
        return 0;
    }
    const std::size_t history_index = visible_start + slot;
    if (history_index >= history.size()) {
        return 0;
    }
    return history[history_index].frame_id;
}

void append_frame_hud_lines(std::vector<std::string>& lines, const FrameDiagnostics& frame) {
    std::ostringstream total;
    total << std::fixed << std::setprecision(2);
    total << "Frame " << frame.frame_id << "  total " << frame.total_ms << " ms";
    lines.push_back(total.str());

    std::ostringstream phases;
    phases << std::fixed << std::setprecision(2);
    phases << "layout " << frame.layout_ms << "  snapshot " << frame.snapshot_ms << "  render "
           << frame.render_ms << "  present " << frame.present_ms;
    lines.push_back(phases.str());

    std::ostringstream counts;
    counts << "widgets " << frame.widget_count << "  nodes " << frame.render_node_count
           << "  dirty " << frame.dirty_widget_count;
    lines.push_back(counts.str());

    std::ostringstream invalidation;
    invalidation << "redraw req " << frame.redraw_request_count << "  layout req "
                 << frame.layout_request_count << "  layout pass "
                 << (frame.had_layout ? "yes" : "no");
    lines.push_back(invalidation.str());

    if (frame.render_hotspot_counters.gpu_present_path != GpuPresentPath::None) {
        std::ostringstream gpu_line;
        gpu_line << "gpu " << gpu_present_path_name(frame.render_hotspot_counters.gpu_present_path)
                 << "  draws " << frame.render_hotspot_counters.gpu_draw_call_count
                 << "  px " << frame.render_hotspot_counters.gpu_estimated_draw_pixel_count;
        lines.push_back(gpu_line.str());
    }
}

void append_frame_detail_lines(std::vector<std::string>& lines, const FrameDiagnostics& frame) {
    std::ostringstream title;
    title << std::fixed << std::setprecision(2);
    title << "Frame " << frame.frame_id << "  total " << frame.total_ms << " ms";
    lines.push_back(title.str());

    std::ostringstream queue;
    queue << std::fixed << std::setprecision(2);
    queue << "queued " << frame.queue_delay_ms << " ms  start " << frame.started_at_ms;
    lines.push_back(queue.str());

    if (frame.request_reasons.empty()) {
        lines.push_back("request reasons: (none)");
    } else {
        lines.push_back("request reasons:");
        for (const auto reason : frame.request_reasons) {
            lines.push_back("  - " + std::string(frame_request_reason_name(reason)));
        }
    }

    if (frame.widget_hotspot_totals.text_measure_count > 0 ||
        frame.render_hotspot_counters.text_node_count > 0) {
        std::ostringstream text_line;
        text_line << "text: measure " << frame.widget_hotspot_totals.text_measure_count
                  << "  nodes " << frame.render_hotspot_counters.text_node_count << "  shape "
                  << frame.render_hotspot_counters.text_shape_count << "  cache hit "
                  << frame.render_hotspot_counters.text_shape_cache_hit_count;
        lines.push_back(text_line.str());
    }

    if (frame.widget_hotspot_totals.image_snapshot_count > 0 ||
        frame.render_hotspot_counters.image_node_count > 0) {
        std::ostringstream image_line;
        image_line << "image: snapshot " << frame.widget_hotspot_totals.image_snapshot_count
                   << "  nodes " << frame.render_hotspot_counters.image_node_count << "  src px "
                   << frame.render_hotspot_counters.image_source_pixel_count;
        lines.push_back(image_line.str());
    }

    if (frame.widget_hotspot_totals.model_sync_count > 0) {
        std::ostringstream model_line;
        model_line << "model: sync " << frame.widget_hotspot_totals.model_sync_count << "  make "
                   << frame.widget_hotspot_totals.model_row_materialize_count << "  reuse "
                   << frame.widget_hotspot_totals.model_row_reuse_count << "  drop "
                   << frame.widget_hotspot_totals.model_row_dispose_count;
        lines.push_back(model_line.str());
    }

    if (frame.render_hotspot_counters.gpu_present_path != GpuPresentPath::None) {
        std::ostringstream gpu_line;
        gpu_line << "gpu present: "
                 << gpu_present_path_name(frame.render_hotspot_counters.gpu_present_path)
                 << "  draws " << frame.render_hotspot_counters.gpu_draw_call_count
                 << "  regions " << frame.render_hotspot_counters.gpu_present_region_count
                 << "  copies " << frame.render_hotspot_counters.gpu_swapchain_copy_count
                 << "  draw px "
                 << frame.render_hotspot_counters.gpu_estimated_draw_pixel_count;
        lines.push_back(gpu_line.str());
    }
}

double trace_event_end_ms(const TraceEvent& event) {
    return event.timestamp_ms + event.duration_ms;
}

bool trace_event_overlaps_frame(const TraceEvent& event, const FrameDiagnostics& frame) {
    const double frame_start_ms = frame.requested_at_ms;
    const double frame_end_ms = frame.started_at_ms + frame.total_ms;
    return trace_event_end_ms(event) >= frame_start_ms && event.timestamp_ms <= frame_end_ms;
}

std::vector<TraceEvent> collect_frame_runtime_events(const FrameDiagnostics& frame,
                                                     std::span<const TraceEvent> events) {
    std::vector<TraceEvent> matched;
    matched.reserve(events.size());
    for (const auto& event : events) {
        if (trace_event_overlaps_frame(event, frame)) {
            matched.push_back(event);
        }
    }
    return matched;
}

void append_frame_runtime_event_lines(std::vector<std::string>& lines,
                                      const FrameDiagnostics& frame,
                                      std::span<const TraceEvent> events) {
    const auto matched = collect_frame_runtime_events(frame, events);
    if (matched.empty()) {
        lines.push_back("runtime events: (none)");
        return;
    }

    lines.push_back("runtime events:");
    for (const auto& event : matched) {
        std::ostringstream summary;
        summary << "  - " << event.name;
        if (!event.category.empty()) {
            summary << " [" << event.category << "]";
        }
        summary << " " << std::fixed << std::setprecision(2) << event.duration_ms << " ms";
        lines.push_back(truncate_for_overlay(summary.str(), 48));
        if (!event.detail.empty()) {
            lines.push_back("    " + truncate_for_overlay(event.detail, 44));
        }
    }
}

void append_widget_panel_lines(std::vector<std::string>& lines, const Widget* widget) {
    if (widget == nullptr) {
        lines.push_back("No widget selected");
        lines.push_back("Use Ctrl+Shift+P to enable picker.");
        return;
    }

    const auto rect = widget->allocation();
    lines.push_back(truncate_for_overlay(debug_type_name(*widget), 38));
    if (!widget->debug_name().empty()) {
        lines.push_back("name: " + truncate_for_overlay(std::string(widget->debug_name()), 31));
    }

    {
        std::ostringstream allocation;
        allocation << "rect: " << static_cast<int>(rect.x) << ", " << static_cast<int>(rect.y)
                   << "  " << static_cast<int>(rect.width) << "x" << static_cast<int>(rect.height);
        lines.push_back(allocation.str());
    }

    {
        std::ostringstream flags;
        flags << "visible " << (widget->is_visible() ? "yes" : "no") << "  focusable "
              << (widget->is_focusable() ? "yes" : "no");
        lines.push_back(flags.str());
    }

    {
        const auto counters = widget->debug_hotspot_counters();
        std::ostringstream hotspot;
        hotspot << "measure " << counters.measure_count << "  allocate " << counters.allocate_count
                << "  snapshot " << counters.snapshot_count;
        lines.push_back(hotspot.str());

        if (counters.text_measure_count > 0 || counters.image_snapshot_count > 0) {
            std::ostringstream content_hotspot;
            content_hotspot << "text measure " << counters.text_measure_count << "  image snap "
                            << counters.image_snapshot_count;
            lines.push_back(content_hotspot.str());
        }

        if (counters.model_sync_count > 0) {
            std::ostringstream model_hotspot;
            model_hotspot << "model sync " << counters.model_sync_count << "  make "
                          << counters.model_row_materialize_count << "  reuse "
                          << counters.model_row_reuse_count << "  drop "
                          << counters.model_row_dispose_count;
            lines.push_back(model_hotspot.str());
        }
    }

    {
        std::ostringstream state;
        state << "state: " << static_cast<uint32_t>(widget->state_flags());
        lines.push_back(state.str());
    }

    std::string classes = "classes: ";
    const auto style_classes = widget->style_classes();
    if (style_classes.empty()) {
        classes += "(none)";
    } else {
        for (std::size_t index = 0; index < style_classes.size(); ++index) {
            if (index > 0) {
                classes += ' ';
            }
            classes += ".";
            classes += style_classes[index];
        }
    }
    lines.push_back(truncate_for_overlay(classes, 40));
}

void draw_widget_bounds_overlay(SnapshotContext& ctx,
                                const Widget& widget,
                                Widget* hovered_widget,
                                Widget* focused_widget,
                                const std::vector<Widget*>& dirty_widgets,
                                bool draw_layout_bounds,
                                bool draw_dirty_widgets) {
    const auto rect = widget.allocation();
    if (rect.width <= 0.0F || rect.height <= 0.0F) {
        return;
    }

    const bool is_hovered = (&widget == hovered_widget);
    const bool is_focused = (&widget == focused_widget);
    const bool is_dirty =
        std::find(dirty_widgets.begin(), dirty_widgets.end(), &widget) != dirty_widgets.end();

    if (draw_dirty_widgets && is_dirty) {
        ctx.add_rounded_rect(rect, Color{0.98F, 0.61F, 0.16F, 0.10F}, 5.0F);
        ctx.add_border(rect, Color{0.96F, 0.58F, 0.08F, 0.65F}, 2.0F, 5.0F);
    }

    if (draw_layout_bounds) {
        Color border = Color{0.19F, 0.54F, 0.95F, 0.62F};
        float thickness = 1.0F;
        if (is_hovered) {
            border = Color{0.16F, 0.73F, 0.69F, 0.85F};
            thickness = 2.0F;
        }
        if (is_focused) {
            border = Color{0.15F, 0.48F, 0.47F, 0.95F};
            thickness = 2.0F;
        }
        ctx.add_border(rect, border, thickness, 4.0F);
    }

    for (const auto& child : widget.children()) {
        if (child != nullptr && child->is_visible()) {
            draw_widget_bounds_overlay(ctx,
                                       *child,
                                       hovered_widget,
                                       focused_widget,
                                       dirty_widgets,
                                       draw_layout_bounds,
                                       draw_dirty_widgets);
        }
    }
}

} // namespace

Window::Window(WindowConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->renderer = create_renderer(RendererBackend::Software);
    impl_->text_shaper = TextShaper::create();
    if (impl_->renderer != nullptr && impl_->text_shaper) {
        impl_->renderer->set_text_shaper(impl_->text_shaper.get());
    }
    NK_LOG_DEBUG("Window", "Window created");
}

Window::~Window() {
    NK_LOG_DEBUG("Window", "Window destroyed");
}

void Window::set_title(std::string_view title) {
    impl_->config.title = std::string(title);
    if (impl_->surface) {
        impl_->surface->set_title(title);
    }
}

std::string_view Window::title() const {
    return impl_->config.title;
}

void Window::resize(int width, int height) {
    impl_->config.width = width;
    impl_->config.height = height;
    if (impl_->surface) {
        impl_->surface->resize(width, height);
    }
    impl_->needs_layout = true;
    impl_->resize_signal.emit(width, height);
    request_frame(FrameRequestReason::Resize);
}

Size Window::size() const {
    if (impl_->surface) {
        return impl_->surface->size();
    }
    return {static_cast<float>(impl_->config.width), static_cast<float>(impl_->config.height)};
}

void Window::set_child(std::shared_ptr<Widget> child) {
    impl_->hovered_widget = nullptr;
    impl_->pressed_widget = nullptr;
    impl_->focused_widget = nullptr;
    impl_->pending_focus_restore.reset();
    impl_->current_cursor_shape = CursorShape::Default;
    impl_->debug_selected_widget = nullptr;
    impl_->debug_selected_render_path.clear();
    if (impl_->child) {
        impl_->child->set_host_window(nullptr);
    }
    impl_->child = std::move(child);
    if (impl_->child) {
        impl_->child->set_host_window(this);
    }
    impl_->needs_layout = true;
    request_frame(FrameRequestReason::ChildChanged);
}

Widget* Window::child() const {
    return impl_->child.get();
}

void Window::present() {
    // Create native surface if we have a platform backend.
    if (!impl_->surface) {
        auto* app = Application::instance();
        if (app && app->has_platform_backend()) {
            impl_->surface = app->platform_backend().create_surface(impl_->config, *this);
        }
    }

    const auto preferred_backend = select_renderer_backend(impl_->surface.get());
    if (impl_->renderer == nullptr || impl_->renderer->backend() != preferred_backend) {
        auto renderer = create_renderer(preferred_backend);
        if (renderer == nullptr) {
            renderer = create_renderer(RendererBackend::Software);
        }
        if (renderer != nullptr) {
            if (impl_->text_shaper) {
                renderer->set_text_shaper(impl_->text_shaper.get());
            }
            if (impl_->surface == nullptr || renderer->attach_surface(*impl_->surface)) {
                impl_->renderer = std::move(renderer);
            }
        }
    } else if (impl_->renderer != nullptr && impl_->surface != nullptr) {
        (void)impl_->renderer->attach_surface(*impl_->surface);
    }

    impl_->visible = true;

    if (impl_->surface) {
        impl_->surface->show();
        impl_->surface->set_cursor_shape(impl_->current_cursor_shape);
    }

    impl_->needs_layout = true;
    request_frame(FrameRequestReason::Present);

    NK_LOG_INFO("Window", "Window presented");
}

void Window::hide() {
    impl_->visible = false;
    if (impl_->surface) {
        impl_->surface->hide();
    }
}

void Window::close() {
    impl_->close_request.emit();
    hide();
}

bool Window::is_visible() const {
    return impl_->visible;
}

void Window::set_fullscreen(bool fullscreen) {
    if (impl_->surface) {
        impl_->surface->set_fullscreen(fullscreen);
    }
}

bool Window::is_fullscreen() const {
    if (impl_->surface) {
        return impl_->surface->is_fullscreen();
    }
    return false;
}

void Window::request_frame() {
    request_frame(FrameRequestReason::Manual);
}

void Window::request_frame(FrameRequestReason reason) {
    if (!impl_->visible) {
        return;
    }

    append_unique_reason(impl_->pending_frame_reasons, reason);
    if (impl_->frame_pending) {
        return;
    }
    impl_->frame_pending = true;
    impl_->pending_frame_requested_at = std::chrono::steady_clock::now();

    auto* app = Application::instance();
    if (!app) {
        impl_->frame_pending = false;
        impl_->pending_frame_reasons.clear();
        return;
    }

    (void)app->event_loop().add_idle([this] {
        impl_->frame_pending = false;

        if (!impl_->visible || !impl_->child) {
            impl_->pending_frame_reasons.clear();
            return;
        }

        using Clock = std::chrono::steady_clock;
        auto elapsed_ms = [](Clock::time_point start, Clock::time_point end) {
            return std::chrono::duration<double, std::milli>(end - start).count();
        };

        const auto sz = size();
        const float scale_factor =
            impl_->surface != nullptr ? impl_->surface->scale_factor() : 1.0F;
        FrameDiagnostics frame;
        frame.frame_id = impl_->next_frame_id++;
        frame.logical_viewport = sz;
        frame.scale_factor = scale_factor;
        frame.had_layout = impl_->needs_layout;
        const auto frame_start = Clock::now();
        frame.requested_at_ms =
            elapsed_ms(impl_->diagnostics_origin, impl_->pending_frame_requested_at);
        frame.started_at_ms = elapsed_ms(impl_->diagnostics_origin, frame_start);
        frame.queue_delay_ms = elapsed_ms(impl_->pending_frame_requested_at, frame_start);
        frame.redraw_request_count = impl_->redraw_request_count;
        frame.layout_request_count = impl_->layout_request_count;
        frame.dirty_widget_count = impl_->dirty_widgets.size();
        frame.request_reasons = impl_->pending_frame_reasons;
        frame.widget_count = count_widgets_recursive(impl_->child.get());
        for (const auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr) {
                frame.widget_count += count_widgets_recursive(overlay.widget.get());
            }
        }

        impl_->child->reset_debug_hotspot_counters_recursive();
        for (auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr) {
                overlay.widget->reset_debug_hotspot_counters_recursive();
            }
        }

        // 1. Layout pass.
        if (impl_->needs_layout) {
            const auto layout_start = Clock::now();
            const auto constraints = Constraints::tight(sz);
            (void)impl_->child->measure_for_diagnostics(constraints);
            impl_->child->allocate({0, 0, sz.width, sz.height});
            for (auto& overlay : impl_->overlays) {
                if (overlay.widget != nullptr && overlay.widget->is_visible()) {
                    overlay.widget->allocate({0, 0, sz.width, sz.height});
                }
            }
            impl_->needs_layout = false;
            frame.layout_ms = elapsed_ms(layout_start, Clock::now());
        }

        // 2. Snapshot pass.
        const auto snapshot_start = Clock::now();
        SnapshotContext snap_ctx;
        impl_->child->snapshot_subtree(snap_ctx);
        for (auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr && overlay.widget->is_visible()) {
                overlay.widget->snapshot_subtree(snap_ctx);
            }
        }

        if (impl_->debug_overlay_flags != DebugOverlayFlags::None) {
            snap_ctx.push_overlay_container({0.0F, 0.0F, sz.width, sz.height});

            const bool draw_layout_bounds =
                has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::LayoutBounds);
            const bool draw_dirty_widgets =
                has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::DirtyWidgets);
            const bool draw_inspector_panel = has_debug_overlay_flag(
                impl_->debug_overlay_flags, DebugOverlayFlags::InspectorPanel);
            if (draw_layout_bounds || draw_dirty_widgets) {
                draw_widget_bounds_overlay(snap_ctx,
                                           *impl_->child,
                                           impl_->hovered_widget,
                                           impl_->focused_widget,
                                           impl_->dirty_widgets,
                                           draw_layout_bounds,
                                           draw_dirty_widgets);
                for (const auto& overlay : impl_->overlays) {
                    if (overlay.widget != nullptr && overlay.widget->is_visible()) {
                        draw_widget_bounds_overlay(snap_ctx,
                                                   *overlay.widget,
                                                   impl_->hovered_widget,
                                                   impl_->focused_widget,
                                                   impl_->dirty_widgets,
                                                   draw_layout_bounds,
                                                   draw_dirty_widgets);
                    }
                }
            }

            if (draw_inspector_panel) {
                const auto* selected_frame =
                    selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
                const auto* selected_render_node = selected_render_node_from_frame(
                    selected_frame, impl_->debug_selected_render_path);
                if (selected_render_node != nullptr) {
                    Rect render_rect = selected_render_node->bounds;
                    if ((render_rect.width <= 0.0F || render_rect.height <= 0.0F) &&
                        !selected_render_node->source_widget_label.empty()) {
                        if (auto* source_widget = debug_selected_render_widget();
                            source_widget != nullptr) {
                            render_rect = source_widget->allocation();
                        }
                    }
                    if (render_rect.width > 0.0F && render_rect.height > 0.0F) {
                        snap_ctx.add_rounded_rect(
                            render_rect, Color{0.95F, 0.71F, 0.18F, 0.10F}, 8.0F);
                        snap_ctx.add_border(
                            render_rect, Color{0.95F, 0.71F, 0.18F, 0.92F}, 2.0F, 8.0F);
                    }
                }

                if (impl_->debug_selected_widget != nullptr) {
                    const auto rect = impl_->debug_selected_widget->allocation();
                    snap_ctx.add_rounded_rect(rect, Color{0.17F, 0.74F, 0.70F, 0.10F}, 6.0F);
                    snap_ctx.add_border(rect, Color{0.14F, 0.73F, 0.67F, 0.95F}, 3.0F, 6.0F);
                }
            }

            if (has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::FrameHud)) {
                std::vector<std::string> lines;
                const FrameDiagnostics& hud_frame = impl_->frame_history.empty()
                                                        ? impl_->last_frame_diagnostics
                                                        : impl_->frame_history.back();
                append_frame_hud_lines(lines, hud_frame);
                const Rect hud_rect{12.0F, 12.0F, 316.0F, 110.0F};
                snap_ctx.add_rounded_rect(hud_rect, Color{0.10F, 0.12F, 0.15F, 0.80F}, 10.0F);
                snap_ctx.add_border(hud_rect, Color{0.82F, 0.85F, 0.90F, 0.40F}, 1.0F, 10.0F);
                FontDescriptor font{
                    .family = {},
                    .size = 12.0F,
                    .weight = FontWeight::Medium,
                };
                float y = hud_rect.y + 12.0F;
                for (const auto& line : lines) {
                    snap_ctx.add_text(
                        {hud_rect.x + 12.0F, y}, line, Color{1.0F, 1.0F, 1.0F, 1.0F}, font);
                    y += 18.0F;
                }
            }

            if (draw_inspector_panel) {
                std::vector<std::shared_ptr<Widget>> overlay_widgets;
                overlay_widgets.reserve(impl_->overlays.size());
                for (const auto& overlay : impl_->overlays) {
                    overlay_widgets.push_back(overlay.widget);
                }
                const auto entries = build_inspector_entries(impl_->child.get(), overlay_widgets);
                const auto selected_index =
                    selected_entry_index(entries, impl_->debug_selected_widget);
                const auto* selected_frame =
                    selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
                const auto render_entries =
                    selected_frame != nullptr
                        ? build_render_inspector_entries(selected_frame->render_snapshot)
                        : std::vector<RenderInspectorEntry>{};
                const auto render_selected_index =
                    selected_render_entry_index(render_entries, impl_->debug_selected_render_path);
                const auto panel_layout = compute_inspector_panel_layout(sz,
                                                                         entries.size(),
                                                                         selected_index,
                                                                         render_entries.size(),
                                                                         render_selected_index);
                const Rect panel_rect = panel_layout.panel;
                snap_ctx.add_rounded_rect(panel_rect, Color{0.10F, 0.12F, 0.15F, 0.84F}, 12.0F);
                snap_ctx.add_border(panel_rect, Color{0.83F, 0.86F, 0.90F, 0.30F}, 1.0F, 12.0F);
                snap_ctx.add_rounded_rect(
                    panel_layout.timeline_area, Color{0.14F, 0.16F, 0.20F, 0.72F}, 8.0F);
                snap_ctx.add_rounded_rect(
                    panel_layout.tree_area, Color{0.14F, 0.16F, 0.20F, 0.72F}, 8.0F);
                snap_ctx.add_rounded_rect(
                    panel_layout.detail_area, Color{0.14F, 0.16F, 0.20F, 0.72F}, 8.0F);

                FontDescriptor title_font{
                    .family = {},
                    .size = 13.0F,
                    .weight = FontWeight::Bold,
                };
                FontDescriptor body_font{
                    .family = {},
                    .size = 12.0F,
                    .weight = FontWeight::Regular,
                };
                snap_ctx.add_text({panel_rect.x + 14.0F, panel_rect.y + 12.0F},
                                  "Inspector",
                                  Color{1.0F, 1.0F, 1.0F, 1.0F},
                                  title_font);
                snap_ctx.add_text(
                    {panel_layout.timeline_area.x + 10.0F, panel_layout.timeline_area.y + 8.0F},
                    "Frames",
                    Color{0.90F, 0.93F, 0.97F, 1.0F},
                    title_font);
                snap_ctx.add_text(
                    {panel_layout.tree_area.x + 10.0F, panel_layout.tree_area.y + 8.0F},
                    "Widget Tree",
                    Color{0.90F, 0.93F, 0.97F, 1.0F},
                    title_font);
                snap_ctx.add_text({panel_layout.render_tree_area.x + 10.0F,
                                   panel_layout.render_tree_area.y + 8.0F},
                                  "Render Tree",
                                  Color{0.90F, 0.93F, 0.97F, 1.0F},
                                  title_font);
                snap_ctx.add_text(
                    {panel_layout.detail_area.x + 10.0F, panel_layout.detail_area.y + 8.0F},
                    "Details",
                    Color{0.90F, 0.93F, 0.97F, 1.0F},
                    title_font);

                std::vector<std::string> lines;
                if (selected_frame != nullptr) {
                    append_frame_detail_lines(lines, *selected_frame);
                    if (auto* app = Application::instance()) {
                        append_frame_runtime_event_lines(
                            lines, *selected_frame, app->event_loop().debug_trace_events());
                    } else {
                        lines.push_back("runtime events: unavailable");
                    }
                    {
                        std::ostringstream render_summary;
                        render_summary << "render nodes: "
                                       << selected_frame->render_snapshot_node_count;
                        lines.push_back(render_summary.str());
                    }
                    if (const auto* selected_render_node = selected_render_node_from_frame(
                            selected_frame, impl_->debug_selected_render_path);
                        selected_render_node != nullptr) {
                        lines.push_back("render node:");
                        lines.push_back(truncate_for_overlay(selected_render_node->kind, 40));
                        if (!selected_render_node->detail.empty()) {
                            lines.push_back(truncate_for_overlay(selected_render_node->detail, 40));
                        }
                        std::ostringstream bounds;
                        bounds << "rect: " << static_cast<int>(selected_render_node->bounds.x)
                               << ", " << static_cast<int>(selected_render_node->bounds.y) << "  "
                               << static_cast<int>(selected_render_node->bounds.width) << "x"
                               << static_cast<int>(selected_render_node->bounds.height);
                        lines.push_back(bounds.str());
                        std::ostringstream children;
                        children << "children: " << selected_render_node->children.size();
                        lines.push_back(children.str());
                    }
                }
                if (impl_->debug_picker_enabled) {
                    lines.push_back("Picker: active");
                    lines.push_back("Click a widget to select it.");
                } else {
                    lines.push_back("Picker: off");
                    lines.push_back("Ctrl+Shift+P picker  Up/Down widgets  Left/Right frames");
                    lines.push_back("PageUp/PageDown render tree");
                }
                append_widget_panel_lines(lines, impl_->debug_selected_widget);

                if (!impl_->frame_history.empty()) {
                    const std::size_t max_visible =
                        std::min<std::size_t>(impl_->frame_history.size(), 24);
                    const std::size_t frame_index = selected_history_index(
                        impl_->frame_history, impl_->debug_selected_frame_id);
                    const std::size_t visible_start = compute_visible_history_start(
                        impl_->frame_history.size(), frame_index, max_visible);
                    const float gap = 3.0F;
                    const float available_width =
                        panel_layout.timeline_area.width - gap * (max_visible - 1);
                    const float bar_width =
                        std::max(3.0F,
                                 available_width /
                                     static_cast<float>(std::max<std::size_t>(1, max_visible)));
                    double max_total_ms = 16.0;
                    for (const auto& history_frame : impl_->frame_history) {
                        max_total_ms = std::max(max_total_ms, history_frame.total_ms);
                    }

                    float x = panel_layout.timeline_area.x + 10.0F;
                    const float base_y = panel_layout.timeline_area.bottom() - 6.0F;
                    for (std::size_t offset = 0; offset < max_visible; ++offset) {
                        const std::size_t history_index = visible_start + offset;
                        if (history_index >= impl_->frame_history.size()) {
                            break;
                        }
                        const auto& history_frame = impl_->frame_history[history_index];
                        const float normalized = static_cast<float>(
                            std::clamp(history_frame.total_ms / max_total_ms, 0.15, 1.0));
                        const float bar_height = 6.0F + normalized * 12.0F;
                        const Rect bar_rect{
                            x,
                            base_y - bar_height,
                            bar_width,
                            bar_height,
                        };
                        const bool selected =
                            history_frame.frame_id == impl_->debug_selected_frame_id;
                        snap_ctx.add_rounded_rect(bar_rect,
                                                  selected ? Color{0.17F, 0.74F, 0.70F, 0.92F}
                                                           : Color{0.70F, 0.76F, 0.84F, 0.48F},
                                                  3.0F);
                        x += bar_width + gap;
                    }
                }

                float tree_y = panel_layout.tree_top;
                for (std::size_t index = 0; index < panel_layout.visible_count; ++index) {
                    const std::size_t entry_index = panel_layout.visible_start + index;
                    if (entry_index >= entries.size()) {
                        break;
                    }

                    const auto& entry = entries[entry_index];
                    const Rect row_rect{
                        panel_layout.tree_area.x + 6.0F,
                        tree_y - 1.0F,
                        panel_layout.tree_area.width - 12.0F,
                        panel_layout.tree_row_height,
                    };
                    if (entry.widget == impl_->debug_selected_widget) {
                        snap_ctx.add_rounded_rect(
                            row_rect, Color{0.17F, 0.74F, 0.70F, 0.24F}, 5.0F);
                        snap_ctx.add_border(
                            row_rect, Color{0.17F, 0.74F, 0.70F, 0.80F}, 1.0F, 5.0F);
                    }

                    std::string row_label(entry.depth * 2, ' ');
                    row_label += truncate_for_overlay(entry.label, 26);
                    snap_ctx.add_text({row_rect.x + 8.0F, tree_y},
                                      row_label,
                                      Color{0.96F, 0.97F, 0.99F, 1.0F},
                                      body_font);
                    tree_y += panel_layout.tree_row_height;
                }

                float render_tree_y = panel_layout.render_tree_top;
                for (std::size_t index = 0; index < panel_layout.render_visible_count; ++index) {
                    const std::size_t entry_index = panel_layout.render_visible_start + index;
                    if (entry_index >= render_entries.size()) {
                        break;
                    }

                    const auto& entry = render_entries[entry_index];
                    const Rect row_rect{
                        panel_layout.render_tree_area.x + 6.0F,
                        render_tree_y - 1.0F,
                        panel_layout.render_tree_area.width - 12.0F,
                        panel_layout.tree_row_height,
                    };
                    if (selected_render_entry_index(
                            render_entries, impl_->debug_selected_render_path) == entry_index) {
                        snap_ctx.add_rounded_rect(
                            row_rect, Color{0.95F, 0.71F, 0.18F, 0.22F}, 5.0F);
                        snap_ctx.add_border(
                            row_rect, Color{0.95F, 0.71F, 0.18F, 0.82F}, 1.0F, 5.0F);
                    }

                    std::string row_label(entry.depth * 2, ' ');
                    row_label += truncate_for_overlay(entry.label, 24);
                    snap_ctx.add_text({row_rect.x + 8.0F, render_tree_y},
                                      row_label,
                                      Color{0.96F, 0.97F, 0.99F, 1.0F},
                                      body_font);
                    render_tree_y += panel_layout.tree_row_height;
                }

                float y = panel_layout.detail_area.y + 28.0F;
                for (const auto& line : lines) {
                    if (y + 14.0F > panel_layout.detail_area.bottom() - 10.0F) {
                        break;
                    }
                    snap_ctx.add_text({panel_layout.detail_area.x + 10.0F, y},
                                      line,
                                      Color{0.96F, 0.97F, 0.99F, 1.0F},
                                      body_font);
                    y += 16.0F;
                }
            }

            snap_ctx.pop_container();
        }

        auto root_node = snap_ctx.take_root();
        frame.snapshot_ms = elapsed_ms(snapshot_start, Clock::now());
        frame.widget_hotspot_totals = collect_widget_hotspot_totals(impl_->child.get());
        for (const auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr) {
                accumulate_widget_hotspot_counters(
                    frame.widget_hotspot_totals,
                    collect_widget_hotspot_totals(overlay.widget.get()));
            }
        }
        frame.render_node_count = count_render_nodes_recursive(root_node.get());
        if (root_node) {
            frame.render_snapshot = build_render_snapshot(*root_node);
            frame.render_snapshot_node_count = count_render_snapshot_nodes(frame.render_snapshot);
        }

        const auto damage_regions =
            (!frame.had_layout && !impl_->dirty_widgets.empty())
                ? collect_damage_regions(impl_->dirty_widgets, sz)
                : std::vector<Rect>{};

        // 3. Render pass.
        const auto render_start = Clock::now();
        if (impl_->renderer == nullptr) {
            impl_->renderer = create_renderer(RendererBackend::Software);
            if (impl_->renderer != nullptr && impl_->text_shaper) {
                impl_->renderer->set_text_shaper(impl_->text_shaper.get());
            }
        }
        if (impl_->renderer == nullptr) {
            impl_->pending_frame_reasons.clear();
            return;
        }
        impl_->renderer->begin_frame(sz, scale_factor);
        impl_->renderer->set_damage_regions(damage_regions);
        if (root_node) {
            impl_->renderer->render(*root_node);
        }
        impl_->renderer->end_frame();
        frame.render_hotspot_counters = impl_->renderer->last_hotspot_counters();
        frame.render_ms = elapsed_ms(render_start, Clock::now());

        // 4. Present pass.
        const auto present_start = Clock::now();
        if (impl_->surface && impl_->renderer != nullptr) {
            impl_->renderer->present(*impl_->surface);
            frame.render_hotspot_counters = impl_->renderer->last_hotspot_counters();
        }
        frame.present_ms = elapsed_ms(present_start, Clock::now());
        frame.total_ms = elapsed_ms(frame_start, Clock::now());
        impl_->last_frame_diagnostics = frame;
        const uint64_t previous_selected_frame_id = impl_->debug_selected_frame_id;
        const uint64_t previous_latest_frame_id =
            impl_->frame_history.empty() ? 0 : impl_->frame_history.back().frame_id;
        impl_->frame_history.push_back(frame);
        if (impl_->frame_history.size() > kDebugFrameHistoryLimit) {
            impl_->frame_history.erase(impl_->frame_history.begin());
        }
        const auto selected_it =
            std::find_if(impl_->frame_history.begin(),
                         impl_->frame_history.end(),
                         [&](const FrameDiagnostics& item) {
                             return item.frame_id == previous_selected_frame_id;
                         });
        if (previous_selected_frame_id == 0 ||
            previous_selected_frame_id == previous_latest_frame_id ||
            selected_it == impl_->frame_history.end()) {
            impl_->debug_selected_frame_id = frame.frame_id;
        }
        sync_debug_selected_render_path();
        impl_->pending_frame_reasons.clear();
        impl_->dirty_widgets.clear();
        impl_->redraw_request_count = 0;
        impl_->layout_request_count = 0;
    });
}

void Window::invalidate_layout() {
    impl_->needs_layout = true;
    ++impl_->layout_request_count;
    request_frame(FrameRequestReason::LayoutInvalidation);
}

void Window::sync_debug_selected_render_path() {
    if (impl_->debug_selected_widget == nullptr) {
        impl_->debug_selected_render_path.clear();
        return;
    }
    const auto* frame =
        selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
    if (frame == nullptr) {
        impl_->debug_selected_render_path.clear();
        return;
    }
    const auto widget_path = impl_->debug_selected_widget->debug_tree_path();
    std::vector<std::size_t> current_path;
    std::vector<std::size_t> result_path;
    if (find_render_path_for_widget(
            frame->render_snapshot, widget_path, current_path, result_path)) {
        impl_->debug_selected_render_path = std::move(result_path);
    } else {
        impl_->debug_selected_render_path.clear();
    }
}

Widget* Window::debug_selected_render_widget() const {
    const auto* frame =
        selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
    const auto* render_node =
        selected_render_node_from_frame(frame, impl_->debug_selected_render_path);
    if (render_node == nullptr || render_node->source_widget_label.empty()) {
        return nullptr;
    }
    if (auto* widget =
            find_widget_by_debug_tree_path(impl_->child.get(), render_node->source_widget_path);
        widget != nullptr) {
        return widget;
    }
    for (const auto& overlay : impl_->overlays) {
        if (auto* widget = find_widget_by_debug_tree_path(overlay.widget.get(),
                                                          render_node->source_widget_path);
            widget != nullptr) {
            return widget;
        }
    }
    return nullptr;
}

void Window::sync_debug_selected_widget_from_render_selection() {
    if (impl_->debug_selected_render_path.empty()) {
        return;
    }
    if (auto* widget = debug_selected_render_widget(); widget != nullptr) {
        impl_->debug_selected_widget = widget;
    }
}

void Window::focus_widget(Widget* widget) {
    auto is_attached_to_window = [this](const Widget* candidate) {
        if (candidate == nullptr) {
            return false;
        }
        if (is_attached_to_window_root(candidate, impl_->child)) {
            return true;
        }
        for (const auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr && is_descendant_of(candidate, overlay.widget.get())) {
                return true;
            }
        }
        return false;
    };

    if (widget != nullptr && (!can_focus_widget(widget) || !is_attached_to_window(widget))) {
        widget = nullptr;
    }

    if (impl_->focused_widget == widget) {
        return;
    }

    if (impl_->focused_widget != nullptr) {
        impl_->focused_widget->set_state_flag(StateFlags::Focused, false);
        impl_->focused_widget->dispatch_focus_controllers(false);
        impl_->focused_widget->on_focus_changed(false);
    }

    impl_->focused_widget = widget;

    if (impl_->focused_widget != nullptr) {
        impl_->focused_widget->set_state_flag(StateFlags::Focused, true);
        impl_->focused_widget->dispatch_focus_controllers(true);
        impl_->focused_widget->on_focus_changed(true);
        impl_->pending_focus_restore = impl_->focused_widget->shared_from_this();
    }
}

void Window::handle_widget_state_change(Widget& widget) {
    if (!widget.is_visible() || !widget.is_sensitive()) {
        if (impl_->hovered_widget != nullptr && is_descendant_of(impl_->hovered_widget, &widget)) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
            impl_->hovered_widget = nullptr;
            impl_->current_cursor_shape = CursorShape::Default;
            if (impl_->surface != nullptr) {
                impl_->surface->set_cursor_shape(CursorShape::Default);
            }
        }
        if (impl_->pressed_widget != nullptr && is_descendant_of(impl_->pressed_widget, &widget)) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        }
        if (impl_->focused_widget != nullptr && is_descendant_of(impl_->focused_widget, &widget)) {
            focus_widget(nullptr);
        }
        if (impl_->debug_selected_widget != nullptr &&
            is_descendant_of(impl_->debug_selected_widget, &widget)) {
            impl_->debug_selected_widget = nullptr;
            impl_->debug_selected_render_path.clear();
        }
    }
}

void Window::handle_widget_detached(Widget& widget) {
    if (impl_->hovered_widget != nullptr && is_descendant_of(impl_->hovered_widget, &widget)) {
        impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        impl_->hovered_widget = nullptr;
        impl_->current_cursor_shape = CursorShape::Default;
        if (impl_->surface != nullptr) {
            impl_->surface->set_cursor_shape(CursorShape::Default);
        }
    }
    if (impl_->pressed_widget != nullptr && is_descendant_of(impl_->pressed_widget, &widget)) {
        impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        impl_->pressed_widget = nullptr;
    }
    if (impl_->focused_widget != nullptr && is_descendant_of(impl_->focused_widget, &widget)) {
        focus_widget(nullptr);
    }
    if (auto pending = impl_->pending_focus_restore.lock();
        pending != nullptr && is_descendant_of(pending.get(), &widget)) {
        impl_->pending_focus_restore.reset();
    }
    if (impl_->debug_selected_widget != nullptr &&
        is_descendant_of(impl_->debug_selected_widget, &widget)) {
        impl_->debug_selected_widget = nullptr;
        impl_->debug_selected_render_path.clear();
    }
}

CursorShape Window::current_cursor_shape() const {
    return impl_->current_cursor_shape;
}

bool Window::is_key_pressed(KeyCode key) const {
    const auto index = static_cast<std::size_t>(key);
    if (index >= impl_->key_state.size()) {
        return false;
    }
    return impl_->key_state[index];
}

void Window::set_debug_overlay_flags(DebugOverlayFlags flags) {
    if (impl_->debug_overlay_flags == flags) {
        return;
    }
    impl_->debug_overlay_flags = flags;
    if (!has_debug_overlay_flag(flags, DebugOverlayFlags::InspectorPanel)) {
        impl_->debug_picker_enabled = false;
    }
    request_frame(FrameRequestReason::DebugOverlayChanged);
}

DebugOverlayFlags Window::debug_overlay_flags() const {
    return impl_->debug_overlay_flags;
}

const FrameDiagnostics& Window::last_frame_diagnostics() const {
    return impl_->last_frame_diagnostics;
}

std::span<const FrameDiagnostics> Window::debug_frame_history() const {
    return impl_->frame_history;
}

WidgetDebugNode Window::debug_tree() const {
    WidgetDebugNode root;
    root.type_name = "Window";
    root.debug_name = impl_->config.title;
    root.allocation = {0.0F, 0.0F, size().width, size().height};
    root.visible = impl_->visible;
    root.sensitive = true;
    root.focusable = false;
    if (impl_->child != nullptr) {
        root.children.push_back(build_widget_debug_node(*impl_->child));
    }
    for (const auto& overlay : impl_->overlays) {
        if (overlay.widget != nullptr) {
            root.children.push_back(build_widget_debug_node(*overlay.widget));
        }
    }
    return root;
}

std::string Window::dump_widget_tree() const {
    return format_widget_debug_tree(debug_tree());
}

std::string Window::dump_frame_trace_json() const {
    if (auto* app = Application::instance()) {
        return format_frame_diagnostics_trace_json(impl_->frame_history,
                                                   app->event_loop().debug_trace_events());
    }
    return format_frame_diagnostics_trace_json(impl_->frame_history);
}

std::vector<TraceEvent> Window::debug_selected_frame_runtime_events() const {
    const auto* frame =
        selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
    if (frame == nullptr) {
        return {};
    }
    if (auto* app = Application::instance()) {
        return collect_frame_runtime_events(*frame, app->event_loop().debug_trace_events());
    }
    return {};
}

RenderSnapshotNode Window::debug_selected_frame_render_snapshot() const {
    if (const auto* frame =
            selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
        frame != nullptr) {
        return frame->render_snapshot;
    }
    return {};
}

RenderSnapshotNode Window::debug_selected_render_node() const {
    if (const auto* frame =
            selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
        frame != nullptr) {
        if (const auto* node =
                selected_render_node_from_frame(frame, impl_->debug_selected_render_path);
            node != nullptr) {
            return *node;
        }
    }
    return {};
}

std::string Window::dump_selected_frame_render_snapshot() const {
    if (const auto* frame =
            selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
        frame != nullptr) {
        return format_render_snapshot_tree(frame->render_snapshot);
    }
    return {};
}

std::string Window::dump_selected_frame_render_snapshot_json() const {
    if (const auto* frame =
            selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
        frame != nullptr) {
        return format_render_snapshot_json(frame->render_snapshot);
    }
    return {};
}

void Window::set_debug_picker_enabled(bool enabled) {
    if (impl_->debug_picker_enabled == enabled) {
        return;
    }
    impl_->debug_picker_enabled = enabled;
    if (enabled) {
        impl_->debug_overlay_flags |= DebugOverlayFlags::InspectorPanel;
    }
    if (impl_->surface != nullptr) {
        impl_->surface->set_cursor_shape(enabled ? CursorShape::PointingHand
                                                 : impl_->current_cursor_shape);
    }
    request_frame(FrameRequestReason::PickerChanged);
}

bool Window::debug_picker_enabled() const {
    return impl_->debug_picker_enabled;
}

Widget* Window::debug_selected_widget() const {
    return impl_->debug_selected_widget;
}

void Window::set_debug_selected_widget(Widget* widget) {
    auto is_attached_to_window = [this](const Widget* candidate) {
        if (candidate == nullptr) {
            return false;
        }
        if (is_attached_to_window_root(candidate, impl_->child)) {
            return true;
        }
        for (const auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr && is_descendant_of(candidate, overlay.widget.get())) {
                return true;
            }
        }
        return false;
    };

    if (widget != nullptr && !is_attached_to_window(widget)) {
        widget = nullptr;
    }
    if (impl_->debug_selected_widget == widget) {
        return;
    }
    impl_->debug_selected_widget = widget;
    sync_debug_selected_render_path();
    request_frame(FrameRequestReason::DebugSelectionChanged);
}

WidgetDebugNode Window::debug_selected_widget_info() const {
    return impl_->debug_selected_widget != nullptr
               ? build_widget_debug_node(*impl_->debug_selected_widget)
               : WidgetDebugNode{};
}

void Window::note_widget_redraw_request(Widget& widget) {
    append_unique_widget(impl_->dirty_widgets, &widget);
    ++impl_->redraw_request_count;
    request_frame(FrameRequestReason::WidgetRedraw);
}

void Window::note_widget_layout_request(Widget& widget) {
    append_unique_widget(impl_->dirty_widgets, &widget);
    ++impl_->layout_request_count;
    impl_->needs_layout = true;
    request_frame(FrameRequestReason::WidgetLayout);
}

// --- Event dispatch ---

void Window::dispatch_mouse_event(const MouseEvent& event) {
    if (!impl_->child) {
        return;
    }

    const auto point = Point{event.x, event.y};
    std::vector<std::shared_ptr<Widget>> overlay_widgets;
    overlay_widgets.reserve(impl_->overlays.size());
    for (const auto& overlay : impl_->overlays) {
        overlay_widgets.push_back(overlay.widget);
    }
    const auto inspector_entries = build_inspector_entries(impl_->child.get(), overlay_widgets);
    const auto inspector_selected_index =
        selected_entry_index(inspector_entries, impl_->debug_selected_widget);
    const auto* selected_frame =
        selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
    const auto render_entries =
        selected_frame != nullptr ? build_render_inspector_entries(selected_frame->render_snapshot)
                                  : std::vector<RenderInspectorEntry>{};
    const auto render_selected_index =
        selected_render_entry_index(render_entries, impl_->debug_selected_render_path);
    const auto inspector_layout = compute_inspector_panel_layout(size(),
                                                                 inspector_entries.size(),
                                                                 inspector_selected_index,
                                                                 render_entries.size(),
                                                                 render_selected_index);
    const auto selected_timeline_frame_id = frame_id_at_timeline_point(
        impl_->frame_history, impl_->debug_selected_frame_id, inspector_layout, point);
    auto inspector_entry_at_point = [&]() -> Widget* {
        if (!has_debug_overlay_flag(impl_->debug_overlay_flags,
                                    DebugOverlayFlags::InspectorPanel) ||
            !inspector_layout.tree_area.contains(point)) {
            return nullptr;
        }
        const float relative_y = point.y - inspector_layout.tree_top;
        if (relative_y < 0.0F) {
            return nullptr;
        }
        const auto row =
            static_cast<std::size_t>(std::floor(relative_y / inspector_layout.tree_row_height));
        if (row >= inspector_layout.visible_count) {
            return nullptr;
        }
        const std::size_t entry_index = inspector_layout.visible_start + row;
        if (entry_index >= inspector_entries.size()) {
            return nullptr;
        }
        return inspector_entries[entry_index].widget;
    };
    auto render_entry_at_point = [&]() -> const RenderInspectorEntry* {
        if (!has_debug_overlay_flag(impl_->debug_overlay_flags,
                                    DebugOverlayFlags::InspectorPanel) ||
            !inspector_layout.render_tree_area.contains(point)) {
            return nullptr;
        }
        const float relative_y = point.y - inspector_layout.render_tree_top;
        if (relative_y < 0.0F) {
            return nullptr;
        }
        const auto row =
            static_cast<std::size_t>(std::floor(relative_y / inspector_layout.tree_row_height));
        if (row >= inspector_layout.render_visible_count) {
            return nullptr;
        }
        const std::size_t entry_index = inspector_layout.render_visible_start + row;
        if (entry_index >= render_entries.size()) {
            return nullptr;
        }
        return &render_entries[entry_index];
    };
    auto resolve_overlay_target = [this, point]() -> Widget* {
        for (auto it = impl_->overlays.rbegin(); it != impl_->overlays.rend(); ++it) {
            if (it->widget == nullptr || !it->widget->is_visible()) {
                continue;
            }
            if (auto* hit = hit_test_widget(it->widget.get(), point)) {
                return hit;
            }
            if (it->modal && it->widget->hit_test(point)) {
                return it->widget.get();
            }
        }
        return nullptr;
    };
    auto resolve_target = [this, point, &resolve_overlay_target]() -> Widget* {
        if (auto* overlay_target = resolve_overlay_target()) {
            return overlay_target;
        }
        if (impl_->focused_widget != nullptr && impl_->focused_widget->hit_test(point)) {
            return impl_->focused_widget;
        }
        return hit_test_widget(impl_->child.get(), point);
    };
    auto set_hovered_widget = [this, point](Widget* widget) {
        if (impl_->hovered_widget == widget) {
            return;
        }

        const auto previous_path = widget_path_to_root(impl_->hovered_widget);
        const auto next_path = widget_path_to_root(widget);
        std::size_t shared_suffix = 0;
        while (shared_suffix < previous_path.size() && shared_suffix < next_path.size() &&
               previous_path[previous_path.size() - 1 - shared_suffix] ==
                   next_path[next_path.size() - 1 - shared_suffix]) {
            ++shared_suffix;
        }

        MouseEvent leave_event{
            .type = MouseEvent::Type::Leave,
            .x = point.x,
            .y = point.y,
        };
        for (std::size_t i = 0; i + shared_suffix < previous_path.size(); ++i) {
            previous_path[i]->dispatch_pointer_controllers(leave_event);
        }

        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        }
        impl_->hovered_widget = widget;
        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, true);
        }

        MouseEvent enter_event{
            .type = MouseEvent::Type::Enter,
            .x = point.x,
            .y = point.y,
        };
        for (std::size_t remaining = next_path.size() - shared_suffix; remaining > 0; --remaining) {
            next_path[remaining - 1]->dispatch_pointer_controllers(enter_event);
        }
    };
    auto dispatch_mouse_bubble = [&event](Widget* target) {
        for (auto* current = target; current != nullptr; current = current->parent()) {
            current->dispatch_pointer_controllers(event);
            if (current->handle_mouse_event(event)) {
                return true;
            }
        }
        return false;
    };
    auto update_cursor_shape = [this](Widget* widget) {
        if (impl_->debug_picker_enabled) {
            if (impl_->surface != nullptr) {
                impl_->surface->set_cursor_shape(CursorShape::PointingHand);
            }
            return;
        }
        const CursorShape next_shape =
            widget != nullptr ? widget->cursor_shape() : CursorShape::Default;
        if (impl_->current_cursor_shape == next_shape) {
            return;
        }
        impl_->current_cursor_shape = next_shape;
        if (impl_->surface != nullptr) {
            impl_->surface->set_cursor_shape(next_shape);
        }
    };

    switch (event.type) {
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Move: {
        if (has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::InspectorPanel) &&
            inspector_layout.panel.contains(point)) {
            if (impl_->debug_picker_enabled) {
                auto* target = resolve_target();
                set_hovered_widget(target);
                update_cursor_shape(target);
                set_debug_selected_widget(target);
            } else {
                set_hovered_widget(nullptr);
                update_cursor_shape(nullptr);
            }
            break;
        }
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (impl_->debug_picker_enabled) {
            set_debug_selected_widget(target);
            break;
        }
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed,
                                                  impl_->pressed_widget == target);
        }
        if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    case MouseEvent::Type::Leave:
        set_hovered_widget(nullptr);
        update_cursor_shape(nullptr);
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        }
        break;
    case MouseEvent::Type::Press: {
        if (has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::InspectorPanel) &&
            inspector_layout.panel.contains(point) && !impl_->debug_picker_enabled) {
            if (event.button == 1) {
                if (selected_timeline_frame_id != 0) {
                    impl_->debug_selected_frame_id = selected_timeline_frame_id;
                    sync_debug_selected_render_path();
                    request_frame(FrameRequestReason::DebugSelectionChanged);
                } else if (const auto* render_entry = render_entry_at_point();
                           render_entry != nullptr) {
                    impl_->debug_selected_render_path = render_entry->path;
                    sync_debug_selected_widget_from_render_selection();
                    request_frame(FrameRequestReason::DebugSelectionChanged);
                } else {
                    set_debug_selected_widget(inspector_entry_at_point());
                }
            }
            break;
        }
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (impl_->debug_picker_enabled && event.button == 1) {
            set_debug_selected_widget(target);
            set_debug_picker_enabled(false);
            break;
        }
        impl_->pressed_widget = target;
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, true);
        }
        focus_widget(target);
        if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    case MouseEvent::Type::Release: {
        if (has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::InspectorPanel) &&
            inspector_layout.panel.contains(point) && !impl_->debug_picker_enabled) {
            break;
        }
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (impl_->pressed_widget != nullptr) {
            auto* pressed = impl_->pressed_widget;
            (void)dispatch_mouse_bubble(pressed);
            pressed->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        } else if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    case MouseEvent::Type::Scroll: {
        if (has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::InspectorPanel) &&
            inspector_layout.panel.contains(point)) {
            break;
        }
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    }
}

void Window::dispatch_key_event(const KeyEvent& event) {
    const auto key_index = static_cast<std::size_t>(event.key);
    if (key_index < impl_->key_state.size()) {
        impl_->key_state[key_index] = event.type == KeyEvent::Type::Press;
    }

    std::vector<std::shared_ptr<Widget>> overlay_widgets;
    overlay_widgets.reserve(impl_->overlays.size());
    for (const auto& overlay : impl_->overlays) {
        overlay_widgets.push_back(overlay.widget);
    }
    auto inspector_entries = build_inspector_entries(impl_->child.get(), overlay_widgets);
    const auto* selected_frame =
        selected_history_frame(impl_->frame_history, impl_->debug_selected_frame_id);
    auto render_entries = selected_frame != nullptr
                              ? build_render_inspector_entries(selected_frame->render_snapshot)
                              : std::vector<RenderInspectorEntry>{};
    auto move_debug_selection = [this, &inspector_entries](int delta) {
        if (inspector_entries.empty()) {
            set_debug_selected_widget(nullptr);
            return;
        }
        const auto first_index = first_navigable_entry_index(inspector_entries);
        const auto last_index = last_navigable_entry_index(inspector_entries);
        if (impl_->debug_selected_widget == nullptr) {
            const std::size_t initial_index = delta < 0 ? last_index : first_index;
            set_debug_selected_widget(inspector_entries[initial_index].widget);
            return;
        }
        const auto current_index =
            selected_entry_index(inspector_entries, impl_->debug_selected_widget);
        const int next_index = std::clamp(static_cast<int>(current_index) + delta,
                                          static_cast<int>(first_index),
                                          static_cast<int>(last_index));
        set_debug_selected_widget(inspector_entries[static_cast<std::size_t>(next_index)].widget);
    };
    auto move_selected_frame = [this](int delta) {
        if (impl_->frame_history.empty()) {
            return;
        }
        const auto current_index =
            selected_history_index(impl_->frame_history, impl_->debug_selected_frame_id);
        const auto next_index =
            static_cast<std::size_t>(std::clamp(static_cast<int>(current_index) + delta,
                                                0,
                                                static_cast<int>(impl_->frame_history.size() - 1)));
        const uint64_t next_frame_id = impl_->frame_history[next_index].frame_id;
        if (impl_->debug_selected_frame_id == next_frame_id) {
            return;
        }
        impl_->debug_selected_frame_id = next_frame_id;
        sync_debug_selected_render_path();
        request_frame(FrameRequestReason::DebugSelectionChanged);
    };
    auto move_selected_render = [this, &render_entries](int delta) {
        if (render_entries.empty()) {
            impl_->debug_selected_render_path.clear();
            return;
        }
        const auto first_index = first_navigable_render_entry_index(render_entries);
        const auto last_index = last_navigable_render_entry_index(render_entries);
        if (impl_->debug_selected_render_path.empty()) {
            const std::size_t initial_index = delta < 0 ? last_index : first_index;
            impl_->debug_selected_render_path = render_entries[initial_index].path;
            request_frame(FrameRequestReason::DebugSelectionChanged);
            return;
        }
        const auto current_index =
            selected_render_entry_index(render_entries, impl_->debug_selected_render_path);
        const int next_index = std::clamp(static_cast<int>(current_index) + delta,
                                          static_cast<int>(first_index),
                                          static_cast<int>(last_index));
        if (render_entries[static_cast<std::size_t>(next_index)].path ==
            impl_->debug_selected_render_path) {
            return;
        }
        impl_->debug_selected_render_path =
            render_entries[static_cast<std::size_t>(next_index)].path;
        sync_debug_selected_widget_from_render_selection();
        request_frame(FrameRequestReason::DebugSelectionChanged);
    };

    if (event.type == KeyEvent::Type::Press) {
        const auto ctrl_shift = Modifiers::Ctrl | Modifiers::Shift;
        if (event.modifiers == ctrl_shift && event.key == KeyCode::I) {
            const auto has_panel = has_debug_overlay_flag(impl_->debug_overlay_flags,
                                                          DebugOverlayFlags::InspectorPanel);
            if (has_panel) {
                impl_->debug_overlay_flags = static_cast<DebugOverlayFlags>(
                    static_cast<uint32_t>(impl_->debug_overlay_flags) &
                    ~static_cast<uint32_t>(DebugOverlayFlags::InspectorPanel));
                impl_->debug_picker_enabled = false;
            } else {
                impl_->debug_overlay_flags |= DebugOverlayFlags::InspectorPanel;
            }
            request_frame(FrameRequestReason::DebugOverlayChanged);
            return;
        }
        if (event.modifiers == ctrl_shift && event.key == KeyCode::P) {
            if (!has_debug_overlay_flag(impl_->debug_overlay_flags,
                                        DebugOverlayFlags::InspectorPanel)) {
                impl_->debug_overlay_flags |= DebugOverlayFlags::InspectorPanel;
            }
            set_debug_picker_enabled(!impl_->debug_picker_enabled);
            return;
        }
        if (impl_->debug_picker_enabled && event.key == KeyCode::Escape) {
            set_debug_picker_enabled(false);
            return;
        }
        if (!impl_->debug_picker_enabled &&
            has_debug_overlay_flag(impl_->debug_overlay_flags, DebugOverlayFlags::InspectorPanel)) {
            if (event.key == KeyCode::Up) {
                move_debug_selection(-1);
                return;
            }
            if (event.key == KeyCode::Down) {
                move_debug_selection(1);
                return;
            }
            if (event.key == KeyCode::Left) {
                move_selected_frame(-1);
                return;
            }
            if (event.key == KeyCode::Right) {
                move_selected_frame(1);
                return;
            }
            if (event.key == KeyCode::PageUp) {
                move_selected_render(-1);
                return;
            }
            if (event.key == KeyCode::PageDown) {
                move_selected_render(1);
                return;
            }
            if (event.key == KeyCode::Home && !inspector_entries.empty()) {
                set_debug_selected_widget(
                    inspector_entries[first_navigable_entry_index(inspector_entries)].widget);
                return;
            }
            if (event.key == KeyCode::End && !inspector_entries.empty()) {
                set_debug_selected_widget(
                    inspector_entries[last_navigable_entry_index(inspector_entries)].widget);
                return;
            }
        }
    }

    auto dispatch_key_bubble = [&event](Widget* target) {
        for (auto* current = target; current != nullptr; current = current->parent()) {
            current->dispatch_keyboard_controllers(event);
            if (current->handle_key_event(event)) {
                return true;
            }
        }
        return false;
    };

    auto advance_focus = [this](bool reverse) -> bool {
        Widget* scope = impl_->child.get();
        for (auto it = impl_->overlays.rbegin(); it != impl_->overlays.rend(); ++it) {
            if (it->modal && it->widget != nullptr && it->widget->is_visible()) {
                scope = it->widget.get();
                break;
            }
        }

        std::vector<Widget*> focusable_widgets;
        collect_focusable_widgets(scope, focusable_widgets);
        if (focusable_widgets.empty()) {
            focus_widget(nullptr);
            return false;
        }

        const auto current =
            std::find(focusable_widgets.begin(), focusable_widgets.end(), impl_->focused_widget);
        std::size_t index = 0;
        if (current == focusable_widgets.end()) {
            index = reverse ? focusable_widgets.size() - 1 : 0;
        } else if (reverse) {
            index = current == focusable_widgets.begin()
                        ? focusable_widgets.size() - 1
                        : static_cast<std::size_t>((current - focusable_widgets.begin()) - 1);
        } else {
            index = static_cast<std::size_t>((current - focusable_widgets.begin() + 1) %
                                             focusable_widgets.size());
        }

        focus_widget(focusable_widgets[index]);
        return true;
    };

    if (event.type == KeyEvent::Type::Press && event.key == KeyCode::Tab &&
        (event.modifiers == Modifiers::None || event.modifiers == Modifiers::Shift)) {
        (void)advance_focus((event.modifiers & Modifiers::Shift) == Modifiers::Shift);
        return;
    }

    for (auto it = impl_->overlays.rbegin(); it != impl_->overlays.rend(); ++it) {
        if (!it->modal || it->widget == nullptr || !it->widget->is_visible()) {
            continue;
        }

        if (impl_->focused_widget != nullptr &&
            is_descendant_of(impl_->focused_widget, it->widget.get())) {
            (void)dispatch_key_bubble(impl_->focused_widget);
        } else {
            (void)dispatch_key_bubble(it->widget.get());
        }
        return;
    }

    if (impl_->focused_widget != nullptr) {
        (void)dispatch_key_bubble(impl_->focused_widget);
    }
}

void Window::dispatch_window_event(const WindowEvent& event) {
    auto set_hovered_widget = [this](Widget* widget) {
        if (impl_->hovered_widget == widget) {
            return;
        }

        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        }
        impl_->hovered_widget = widget;
        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, true);
        }
    };

    switch (event.type) {
    case WindowEvent::Type::Resize:
        impl_->config.width = event.width;
        impl_->config.height = event.height;
        impl_->needs_layout = true;
        impl_->resize_signal.emit(event.width, event.height);
        request_frame(FrameRequestReason::Resize);
        break;
    case WindowEvent::Type::Close:
        close();
        break;
    case WindowEvent::Type::Expose:
        request_frame(FrameRequestReason::Expose);
        break;
    case WindowEvent::Type::FocusIn:
        if (auto restore = impl_->pending_focus_restore.lock()) {
            focus_widget(restore.get());
        }
        break;
    case WindowEvent::Type::FocusOut:
        set_hovered_widget(nullptr);
        impl_->current_cursor_shape = CursorShape::Default;
        if (impl_->surface != nullptr) {
            impl_->surface->set_cursor_shape(CursorShape::Default);
        }
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        }
        if (impl_->focused_widget != nullptr) {
            impl_->pending_focus_restore = impl_->focused_widget->shared_from_this();
        }
        impl_->key_state.fill(false);
        focus_widget(nullptr);
        break;
    }
}

NativeSurface* Window::native_surface() const {
    return impl_->surface.get();
}

RendererBackend Window::renderer_backend() const {
    return impl_->renderer != nullptr ? impl_->renderer->backend() : RendererBackend::Software;
}

TextShaper* Window::text_shaper() const {
    return impl_->text_shaper.get();
}

void Window::show_overlay(std::shared_ptr<Widget> overlay, bool modal) {
    if (overlay == nullptr) {
        return;
    }

    for (auto& entry : impl_->overlays) {
        if (entry.widget.get() == overlay.get()) {
            entry.widget = std::move(overlay);
            entry.modal = modal;
            entry.previous_focus = modal && impl_->focused_widget != nullptr
                                       ? impl_->focused_widget->shared_from_this()
                                       : std::weak_ptr<Widget>{};
            entry.widget->set_host_window(this);
            impl_->needs_layout = true;
            request_frame(FrameRequestReason::OverlayChanged);
            return;
        }
    }

    overlay->set_host_window(this);
    impl_->overlays.push_back({std::move(overlay),
                               modal,
                               modal && impl_->focused_widget != nullptr
                                   ? impl_->focused_widget->shared_from_this()
                                   : std::weak_ptr<Widget>{}});
    impl_->needs_layout = true;
    request_frame(FrameRequestReason::OverlayChanged);
}

void Window::dismiss_overlay(Widget& overlay) {
    auto it = std::find_if(
        impl_->overlays.begin(), impl_->overlays.end(), [&](const Impl::OverlayEntry& entry) {
            return entry.widget.get() == &overlay;
        });
    if (it == impl_->overlays.end()) {
        return;
    }

    if (impl_->hovered_widget != nullptr && is_descendant_of(impl_->hovered_widget, &overlay)) {
        impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        impl_->hovered_widget = nullptr;
    }
    if (impl_->pressed_widget != nullptr && is_descendant_of(impl_->pressed_widget, &overlay)) {
        impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        impl_->pressed_widget = nullptr;
    }
    if (impl_->focused_widget != nullptr && is_descendant_of(impl_->focused_widget, &overlay)) {
        focus_widget(nullptr);
    }

    auto restore_focus = it->previous_focus;

    it->widget->set_host_window(nullptr);
    impl_->overlays.erase(it);
    impl_->needs_layout = true;
    request_frame(FrameRequestReason::OverlayChanged);

    if (auto widget = restore_focus.lock()) {
        focus_widget(widget.get());
    }
}

Signal<>& Window::on_close_request() {
    return impl_->close_request;
}

Signal<int, int>& Window::on_resize() {
    return impl_->resize_signal;
}

} // namespace nk
