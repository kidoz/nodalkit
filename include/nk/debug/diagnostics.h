#pragma once

/// @file diagnostics.h
/// @brief Debug and performance diagnostics primitives for NodalKit windows.

#include <cstddef>
#include <cstdint>
#include <nk/foundation/result.h>
#include <nk/foundation/types.h>
#include <nk/ui_core/state_flags.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class RenderNode;

enum class DebugOverlayFlags : uint32_t {
    None = 0,
    LayoutBounds = 1U << 0,
    DirtyWidgets = 1U << 1,
    FrameHud = 1U << 2,
    InspectorPanel = 1U << 3,
};

enum class DebugInspectorPresentation : uint8_t {
    Overlay,
    DockedRight,
    Detached,
};

[[nodiscard]] constexpr DebugOverlayFlags operator|(DebugOverlayFlags lhs,
                                                    DebugOverlayFlags rhs) noexcept {
    return static_cast<DebugOverlayFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

[[nodiscard]] constexpr DebugOverlayFlags operator&(DebugOverlayFlags lhs,
                                                    DebugOverlayFlags rhs) noexcept {
    return static_cast<DebugOverlayFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr DebugOverlayFlags& operator|=(DebugOverlayFlags& lhs, DebugOverlayFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool has_debug_overlay_flag(DebugOverlayFlags value,
                                                    DebugOverlayFlags flag) noexcept {
    return (static_cast<uint32_t>(value & flag) != 0U);
}

enum class FrameRequestReason : uint8_t {
    Manual,
    Present,
    Resize,
    ScaleFactorChanged,
    Expose,
    ChildChanged,
    OverlayChanged,
    LayoutInvalidation,
    WidgetRedraw,
    WidgetLayout,
    DebugOverlayChanged,
    DebugSelectionChanged,
    PickerChanged,
    SystemPreferencesChanged,
};

enum class GpuPresentPath : uint8_t {
    None,
    SoftwareDirect,
    SoftwareUpload,
    FullRedrawDirect,
    FullRedrawCopyBack,
    PartialRedrawCopy,
};

enum class GpuPresentTradeoff : uint8_t {
    None,
    BandwidthFavored,
    DrawFavored,
};

enum class FramePerformanceMarker : uint8_t {
    WithinBudget,
    OverBudget,
    Slow,
    VerySlow,
};

struct RenderHotspotCounters {
    std::size_t text_node_count = 0;
    std::size_t text_shape_count = 0;
    std::size_t text_shape_cache_hit_count = 0;
    std::size_t text_bitmap_pixel_count = 0;
    std::size_t text_texture_upload_count = 0;
    std::size_t text_texture_cache_hit_count = 0;
    std::size_t image_node_count = 0;
    std::size_t image_source_pixel_count = 0;
    std::size_t image_dest_pixel_count = 0;
    std::size_t image_texture_upload_count = 0;
    std::size_t image_texture_cache_hit_count = 0;
    std::size_t damage_region_count = 0;
    std::size_t gpu_draw_call_count = 0;
    std::size_t gpu_replayed_command_count = 0;
    std::size_t gpu_skipped_command_count = 0;
    std::size_t gpu_present_region_count = 0;
    std::size_t gpu_swapchain_copy_count = 0;
    std::size_t gpu_viewport_pixel_count = 0;
    std::size_t gpu_estimated_draw_pixel_count = 0;
    GpuPresentPath gpu_present_path = GpuPresentPath::None;
    GpuPresentTradeoff gpu_present_tradeoff = GpuPresentTradeoff::None;

    bool operator==(const RenderHotspotCounters&) const = default;
};

struct WidgetHotspotCounters {
    std::size_t measure_count = 0;
    std::size_t allocate_count = 0;
    std::size_t snapshot_count = 0;
    std::size_t text_measure_count = 0;
    std::size_t image_snapshot_count = 0;
    std::size_t model_sync_count = 0;
    std::size_t model_row_materialize_count = 0;
    std::size_t model_row_reuse_count = 0;
    std::size_t model_row_dispose_count = 0;

    bool operator==(const WidgetHotspotCounters&) const = default;
};

struct FrameDiagnostics {
    uint64_t frame_id = 0;
    Size logical_viewport{};
    float scale_factor = 1.0F;
    bool had_layout = false;
    double requested_at_ms = 0.0;
    double started_at_ms = 0.0;
    double queue_delay_ms = 0.0;
    std::size_t widget_count = 0;
    std::size_t render_node_count = 0;
    std::size_t dirty_widget_count = 0;
    std::size_t redraw_request_count = 0;
    std::size_t layout_request_count = 0;
    double layout_ms = 0.0;
    double snapshot_ms = 0.0;
    double render_ms = 0.0;
    double present_ms = 0.0;
    double total_ms = 0.0;
    FramePerformanceMarker performance_marker = FramePerformanceMarker::WithinBudget;
    double budget_overrun_ms = 0.0;
    WidgetHotspotCounters widget_hotspot_totals;
    RenderHotspotCounters render_hotspot_counters;
    std::vector<FrameRequestReason> request_reasons;

    struct RenderSnapshotNode {
        std::string kind;
        Rect bounds{};
        std::string detail;
        std::string source_widget_label;
        std::vector<std::size_t> source_widget_path;
        std::vector<RenderSnapshotNode> children;

        bool operator==(const RenderSnapshotNode&) const = default;
    } render_snapshot;

    std::size_t render_snapshot_node_count = 0;
};

using RenderSnapshotNode = FrameDiagnostics::RenderSnapshotNode;

struct TraceEvent {
    std::string name;
    std::string category;
    double timestamp_ms = 0.0;
    double duration_ms = 0.0;
    std::string detail;
    std::string source_label;

    bool operator==(const TraceEvent&) const = default;
};

struct WidgetDebugNode {
    std::string type_name;
    std::string debug_name;
    std::vector<std::size_t> tree_path;
    std::string accessible_role;
    std::string accessible_name;
    std::string accessible_description;
    std::string accessible_value;
    std::vector<std::string> accessible_actions;
    std::vector<std::string> accessible_relations;
    bool accessible_hidden = false;
    StateFlags accessible_state = StateFlags::None;
    Rect allocation{};
    StateFlags state_flags = StateFlags::None;
    bool visible = true;
    bool sensitive = true;
    bool focusable = false;
    bool focused = false;
    bool hovered = false;
    bool pressed = false;
    bool retain_size_when_hidden = false;
    bool pending_redraw = false;
    bool pending_layout = false;
    bool has_last_measure = false;
    float last_constraint_min_width = 0.0F;
    float last_constraint_min_height = 0.0F;
    float last_constraint_max_width = 0.0F;
    float last_constraint_max_height = 0.0F;
    float last_request_minimum_width = 0.0F;
    float last_request_minimum_height = 0.0F;
    float last_request_natural_width = 0.0F;
    float last_request_natural_height = 0.0F;
    std::string horizontal_size_policy;
    std::string vertical_size_policy;
    uint8_t horizontal_stretch = 0;
    uint8_t vertical_stretch = 0;
    WidgetHotspotCounters hotspot_counters;
    std::vector<std::string> style_classes;
    std::vector<WidgetDebugNode> children;
};

struct FrameDiagnosticsArtifact {
    std::vector<FrameDiagnostics> frames;

    bool operator==(const FrameDiagnosticsArtifact&) const = default;
};

struct TraceCapture {
    std::vector<TraceEvent> events;

    bool operator==(const TraceCapture&) const = default;
};

struct FrameTimeHistogram {
    std::size_t within_budget_count = 0;
    std::size_t over_budget_count = 0;
    std::size_t slow_count = 0;
    std::size_t very_slow_count = 0;

    [[nodiscard]] constexpr std::size_t total_count() const noexcept {
        return within_budget_count + over_budget_count + slow_count + very_slow_count;
    }

    bool operator==(const FrameTimeHistogram&) const = default;
};

[[nodiscard]] constexpr std::string_view render_snapshot_artifact_format() noexcept {
    return "nk-render-snapshot-v1";
}

[[nodiscard]] constexpr std::string_view widget_debug_artifact_format() noexcept {
    return "nk-widget-debug-v1";
}

[[nodiscard]] constexpr std::string_view frame_diagnostics_artifact_format() noexcept {
    return "nk-frame-diagnostics-v1";
}

[[nodiscard]] constexpr std::string_view frame_diagnostics_trace_export_format() noexcept {
    return "chrome-trace-event-array-v1";
}

[[nodiscard]] std::string_view frame_request_reason_name(FrameRequestReason reason) noexcept;
[[nodiscard]] std::string_view gpu_present_path_name(GpuPresentPath path) noexcept;
[[nodiscard]] std::string_view gpu_present_tradeoff_name(GpuPresentTradeoff tradeoff) noexcept;
[[nodiscard]] bool has_frame_request_reason(const FrameDiagnostics& frame,
                                            FrameRequestReason reason) noexcept;

[[nodiscard]] constexpr double frame_budget_ms() noexcept {
    return 16.67;
}

[[nodiscard]] FramePerformanceMarker classify_frame_time(double total_ms) noexcept;
[[nodiscard]] FrameTimeHistogram
build_frame_time_histogram(std::span<const FrameDiagnostics> frames) noexcept;
[[nodiscard]] std::size_t count_render_snapshot_nodes(const RenderSnapshotNode& root) noexcept;
[[nodiscard]] RenderSnapshotNode build_render_snapshot(const RenderNode& root);
[[nodiscard]] std::string format_widget_debug_tree(const WidgetDebugNode& root);
[[nodiscard]] std::string format_widget_debug_json(const WidgetDebugNode& root);
[[nodiscard]] Result<WidgetDebugNode> parse_widget_debug_json(std::string_view json);
[[nodiscard]] Result<WidgetDebugNode> load_widget_debug_json_file(std::string_view path);
[[nodiscard]] Result<void> save_widget_debug_json_file(const WidgetDebugNode& root,
                                                       std::string_view path);
[[nodiscard]] std::string format_render_snapshot_tree(const RenderSnapshotNode& root);
[[nodiscard]] std::string format_render_snapshot_json(const RenderSnapshotNode& root);
[[nodiscard]] Result<RenderSnapshotNode> parse_render_snapshot_json(std::string_view json);
[[nodiscard]] Result<RenderSnapshotNode> load_render_snapshot_json_file(std::string_view path);
[[nodiscard]] Result<void> save_render_snapshot_json_file(const RenderSnapshotNode& root,
                                                          std::string_view path);
[[nodiscard]] std::string
format_frame_diagnostics_artifact_json(std::span<const FrameDiagnostics> frames);
[[nodiscard]] Result<FrameDiagnosticsArtifact>
parse_frame_diagnostics_artifact_json(std::string_view json);
[[nodiscard]] Result<FrameDiagnosticsArtifact>
load_frame_diagnostics_artifact_json_file(std::string_view path);
[[nodiscard]] Result<void>
save_frame_diagnostics_artifact_json_file(const FrameDiagnosticsArtifact& artifact,
                                          std::string_view path);
[[nodiscard]] std::string
format_frame_diagnostics_trace_json(std::span<const FrameDiagnostics> frames,
                                    std::span<const TraceEvent> extra_events = {});
[[nodiscard]] Result<TraceCapture> parse_frame_diagnostics_trace_json(std::string_view json);
[[nodiscard]] Result<TraceCapture> load_frame_diagnostics_trace_json_file(std::string_view path);
[[nodiscard]] Result<void> save_frame_diagnostics_trace_json_file(const TraceCapture& capture,
                                                                  std::string_view path);

} // namespace nk
