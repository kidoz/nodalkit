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
    Expose,
    ChildChanged,
    OverlayChanged,
    LayoutInvalidation,
    WidgetRedraw,
    WidgetLayout,
    DebugOverlayChanged,
    DebugSelectionChanged,
    PickerChanged,
};

enum class GpuPresentPath : uint8_t {
    None,
    SoftwareDirect,
    SoftwareUpload,
    FullRedrawDirect,
    FullRedrawCopyBack,
    PartialRedrawCopy,
};

struct RenderHotspotCounters {
    std::size_t text_node_count = 0;
    std::size_t text_shape_count = 0;
    std::size_t text_shape_cache_hit_count = 0;
    std::size_t text_bitmap_pixel_count = 0;
    std::size_t text_texture_upload_count = 0;
    std::size_t image_node_count = 0;
    std::size_t image_source_pixel_count = 0;
    std::size_t image_dest_pixel_count = 0;
    std::size_t image_texture_upload_count = 0;
    std::size_t damage_region_count = 0;
    std::size_t gpu_draw_call_count = 0;
    std::size_t gpu_present_region_count = 0;
    std::size_t gpu_swapchain_copy_count = 0;
    std::size_t gpu_estimated_draw_pixel_count = 0;
    GpuPresentPath gpu_present_path = GpuPresentPath::None;

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
};

struct WidgetDebugNode {
    std::string type_name;
    std::string debug_name;
    Rect allocation{};
    StateFlags state_flags = StateFlags::None;
    bool visible = true;
    bool sensitive = true;
    bool focusable = false;
    WidgetHotspotCounters hotspot_counters;
    std::vector<std::string> style_classes;
    std::vector<WidgetDebugNode> children;
};

[[nodiscard]] std::string_view frame_request_reason_name(FrameRequestReason reason) noexcept;
[[nodiscard]] std::string_view gpu_present_path_name(GpuPresentPath path) noexcept;
[[nodiscard]] bool has_frame_request_reason(const FrameDiagnostics& frame,
                                            FrameRequestReason reason) noexcept;
[[nodiscard]] std::size_t count_render_snapshot_nodes(const RenderSnapshotNode& root) noexcept;
[[nodiscard]] RenderSnapshotNode build_render_snapshot(const RenderNode& root);
[[nodiscard]] std::string format_widget_debug_tree(const WidgetDebugNode& root);
[[nodiscard]] std::string format_render_snapshot_tree(const RenderSnapshotNode& root);
[[nodiscard]] std::string format_render_snapshot_json(const RenderSnapshotNode& root);
[[nodiscard]] Result<RenderSnapshotNode> parse_render_snapshot_json(std::string_view json);
[[nodiscard]] Result<RenderSnapshotNode> load_render_snapshot_json_file(std::string_view path);
[[nodiscard]] Result<void> save_render_snapshot_json_file(const RenderSnapshotNode& root,
                                                          std::string_view path);
[[nodiscard]] std::string
format_frame_diagnostics_trace_json(std::span<const FrameDiagnostics> frames,
                                    std::span<const TraceEvent> extra_events = {});

} // namespace nk
