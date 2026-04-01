#include <algorithm>
#include <cmath>
#include <nk/debug/diagnostics.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <sstream>
#include <string>

namespace nk {

namespace {

std::string escape_json_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

void append_trace_event_header(std::ostringstream& out, bool& first_event) {
    if (!first_event) {
        out << ",\n";
    }
    first_event = false;
}

void append_widget_debug_node(std::ostringstream& out,
                              const WidgetDebugNode& node,
                              std::size_t depth) {
    for (std::size_t index = 0; index < depth; ++index) {
        out << "  ";
    }

    out << node.type_name;
    if (!node.debug_name.empty()) {
        out << " \"" << node.debug_name << "\"";
    }
    out << " [" << node.allocation.x << ", " << node.allocation.y << ", " << node.allocation.width
        << " x " << node.allocation.height << "]";

    if (!node.style_classes.empty()) {
        out << " classes=";
        for (std::size_t index = 0; index < node.style_classes.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            out << "." << node.style_classes[index];
        }
    }

    out << " visible=" << (node.visible ? "true" : "false")
        << " sensitive=" << (node.sensitive ? "true" : "false")
        << " focusable=" << (node.focusable ? "true" : "false")
        << " state=" << static_cast<uint32_t>(node.state_flags) << "\n";

    for (const auto& child : node.children) {
        append_widget_debug_node(out, child, depth + 1);
    }
}

void append_render_snapshot_node(std::ostringstream& out,
                                 const RenderSnapshotNode& node,
                                 std::size_t depth) {
    for (std::size_t index = 0; index < depth; ++index) {
        out << "  ";
    }
    out << node.kind << " [" << node.bounds.x << ", " << node.bounds.y << ", " << node.bounds.width
        << " x " << node.bounds.height << "]";
    if (!node.detail.empty()) {
        out << " " << node.detail;
    }
    if (!node.source_widget_label.empty()) {
        out << " widget=\"" << node.source_widget_label << "\"";
    }
    out << "\n";
    for (const auto& child : node.children) {
        append_render_snapshot_node(out, child, depth + 1);
    }
}

void append_render_snapshot_json(std::ostringstream& out, const RenderSnapshotNode& node) {
    out << "{\"kind\":\"" << escape_json_string(node.kind)
        << "\",\"bounds\":{\"x\":" << node.bounds.x << ",\"y\":" << node.bounds.y
        << ",\"width\":" << node.bounds.width << ",\"height\":" << node.bounds.height << "}";
    if (!node.detail.empty()) {
        out << ",\"detail\":\"" << escape_json_string(node.detail) << "\"";
    }
    if (!node.source_widget_label.empty()) {
        out << ",\"source_widget_label\":\"" << escape_json_string(node.source_widget_label)
            << "\"";
    }
    if (!node.source_widget_path.empty()) {
        out << ",\"source_widget_path\":[";
        for (std::size_t index = 0; index < node.source_widget_path.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            out << node.source_widget_path[index];
        }
        out << "]";
    }
    out << ",\"children\":[";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        append_render_snapshot_json(out, node.children[index]);
    }
    out << "]}";
}

std::string render_node_kind_name(RenderNodeKind kind) {
    switch (kind) {
    case RenderNodeKind::Container:
        return "Container";
    case RenderNodeKind::ColorRect:
        return "ColorRect";
    case RenderNodeKind::RoundedRect:
        return "RoundedRect";
    case RenderNodeKind::Text:
        return "Text";
    case RenderNodeKind::Image:
        return "Image";
    case RenderNodeKind::Border:
        return "Border";
    case RenderNodeKind::RoundedClip:
        return "RoundedClip";
    }
    return "Unknown";
}

std::string summarize_color(Color color) {
    std::ostringstream out;
    out << "rgba(" << color.r << "," << color.g << "," << color.b << "," << color.a << ")";
    return out.str();
}

std::string truncate_snapshot_text(std::string_view value, std::size_t max_chars) {
    if (value.size() <= max_chars) {
        return std::string(value);
    }
    if (max_chars <= 3) {
        return std::string(value.substr(0, max_chars));
    }
    return std::string(value.substr(0, max_chars - 3)) + "...";
}

RenderSnapshotNode build_render_snapshot_node(const RenderNode& node) {
    RenderSnapshotNode snapshot;
    snapshot.kind = render_node_kind_name(node.kind());
    snapshot.bounds = node.bounds();
    snapshot.source_widget_label = std::string(node.debug_source_label());
    snapshot.source_widget_path.assign(node.debug_source_path().begin(),
                                       node.debug_source_path().end());

    switch (node.kind()) {
    case RenderNodeKind::ColorRect: {
        const auto& color_node = static_cast<const ColorRectNode&>(node);
        snapshot.detail = summarize_color(color_node.color());
        break;
    }
    case RenderNodeKind::RoundedRect: {
        const auto& rounded_node = static_cast<const RoundedRectNode&>(node);
        std::ostringstream detail;
        detail << summarize_color(rounded_node.color())
               << " radius=" << rounded_node.corner_radius();
        snapshot.detail = detail.str();
        break;
    }
    case RenderNodeKind::Border: {
        const auto& border_node = static_cast<const BorderNode&>(node);
        std::ostringstream detail;
        detail << summarize_color(border_node.color()) << " thickness=" << border_node.thickness()
               << " radius=" << border_node.corner_radius();
        snapshot.detail = detail.str();
        break;
    }
    case RenderNodeKind::RoundedClip: {
        const auto& clip_node = static_cast<const RoundedClipNode&>(node);
        std::ostringstream detail;
        detail << "radius=" << clip_node.corner_radius();
        snapshot.detail = detail.str();
        break;
    }
    case RenderNodeKind::Text: {
        const auto& text_node = static_cast<const TextNode&>(node);
        std::ostringstream detail;
        detail << "\"" << truncate_snapshot_text(text_node.text(), 36) << "\" "
               << summarize_color(text_node.text_color()) << " size=" << text_node.font().size;
        snapshot.detail = detail.str();
        break;
    }
    case RenderNodeKind::Image: {
        const auto& image_node = static_cast<const ImageNode&>(node);
        std::ostringstream detail;
        detail << image_node.src_width() << "x" << image_node.src_height() << " "
               << (image_node.scale_mode() == ScaleMode::NearestNeighbor ? "nearest" : "bilinear");
        snapshot.detail = detail.str();
        break;
    }
    case RenderNodeKind::Container:
        break;
    }

    snapshot.children.reserve(node.children().size());
    for (const auto& child : node.children()) {
        if (child != nullptr) {
            snapshot.children.push_back(build_render_snapshot_node(*child));
        }
    }
    return snapshot;
}

} // namespace

std::string_view frame_request_reason_name(FrameRequestReason reason) noexcept {
    switch (reason) {
    case FrameRequestReason::Manual:
        return "manual";
    case FrameRequestReason::Present:
        return "present";
    case FrameRequestReason::Resize:
        return "resize";
    case FrameRequestReason::Expose:
        return "expose";
    case FrameRequestReason::ChildChanged:
        return "child-changed";
    case FrameRequestReason::OverlayChanged:
        return "overlay-changed";
    case FrameRequestReason::LayoutInvalidation:
        return "layout-invalidation";
    case FrameRequestReason::WidgetRedraw:
        return "widget-redraw";
    case FrameRequestReason::WidgetLayout:
        return "widget-layout";
    case FrameRequestReason::DebugOverlayChanged:
        return "debug-overlay";
    case FrameRequestReason::DebugSelectionChanged:
        return "debug-selection";
    case FrameRequestReason::PickerChanged:
        return "debug-picker";
    }
    return "unknown";
}

bool has_frame_request_reason(const FrameDiagnostics& frame, FrameRequestReason reason) noexcept {
    return std::find(frame.request_reasons.begin(), frame.request_reasons.end(), reason) !=
           frame.request_reasons.end();
}

std::size_t count_render_snapshot_nodes(const RenderSnapshotNode& root) noexcept {
    std::size_t count = 1;
    for (const auto& child : root.children) {
        count += count_render_snapshot_nodes(child);
    }
    return count;
}

std::string format_widget_debug_tree(const WidgetDebugNode& root) {
    std::ostringstream out;
    append_widget_debug_node(out, root, 0);
    return out.str();
}

std::string format_render_snapshot_tree(const RenderSnapshotNode& root) {
    std::ostringstream out;
    append_render_snapshot_node(out, root, 0);
    return out.str();
}

std::string format_render_snapshot_json(const RenderSnapshotNode& root) {
    std::ostringstream out;
    append_render_snapshot_json(out, root);
    return out.str();
}

std::string format_frame_diagnostics_trace_json(std::span<const FrameDiagnostics> frames,
                                                std::span<const TraceEvent> extra_events) {
    std::ostringstream out;
    out << "{\n  \"traceEvents\": [\n";
    bool first_event = true;

    for (const auto& event : extra_events) {
        append_trace_event_header(out, first_event);
        out << "    {\"name\":\"" << escape_json_string(event.name) << "\",\"cat\":\""
            << escape_json_string(event.category) << "\",\"ph\":\"X\",\"ts\":"
            << static_cast<long long>(std::llround(event.timestamp_ms * 1000.0))
            << ",\"dur\":" << static_cast<long long>(std::llround(event.duration_ms * 1000.0))
            << ",\"pid\":1,\"tid\":2";
        if (!event.detail.empty()) {
            out << ",\"args\":{\"detail\":\"" << escape_json_string(event.detail) << "\"}";
        }
        out << "}";
    }

    for (const auto& frame : frames) {
        const auto requested_ts_us =
            static_cast<long long>(std::llround(frame.requested_at_ms * 1000.0));
        const auto started_ts_us =
            static_cast<long long>(std::llround(frame.started_at_ms * 1000.0));

        for (const auto reason : frame.request_reasons) {
            append_trace_event_header(out, first_event);
            out << "    {\"name\":\"" << escape_json_string(frame_request_reason_name(reason))
                << "\",\"cat\":\"frame-request\",\"ph\":\"i\",\"s\":\"t\",\"ts\":"
                << requested_ts_us << ",\"pid\":1,\"tid\":1,\"args\":{\"frame\":" << frame.frame_id
                << "}}";
        }

        append_trace_event_header(out, first_event);
        out << "    {\"name\":\"frame\",\"cat\":\"frame\",\"ph\":\"X\",\"ts\":" << started_ts_us
            << ",\"dur\":" << static_cast<long long>(std::llround(frame.total_ms * 1000.0))
            << ",\"pid\":1,\"tid\":1,\"args\":{\"frame\":" << frame.frame_id
            << ",\"queue_delay_ms\":" << frame.queue_delay_ms << "}}";

        double phase_start_ms = frame.started_at_ms;

        const struct PhaseInfo {
            std::string_view name;
            double duration_ms;
        } phases[] = {
            {"layout", frame.layout_ms},
            {"snapshot", frame.snapshot_ms},
            {"render", frame.render_ms},
            {"present", frame.present_ms},
        };

        for (const auto& phase : phases) {
            append_trace_event_header(out, first_event);
            out << "    {\"name\":\"" << phase.name
                << "\",\"cat\":\"frame-phase\",\"ph\":\"X\",\"ts\":"
                << static_cast<long long>(std::llround(phase_start_ms * 1000.0))
                << ",\"dur\":" << static_cast<long long>(std::llround(phase.duration_ms * 1000.0))
                << ",\"pid\":1,\"tid\":1,\"args\":{\"frame\":" << frame.frame_id << "}}";
            phase_start_ms += phase.duration_ms;
        }
    }

    out << "\n  ]\n}\n";
    return out.str();
}

RenderSnapshotNode build_render_snapshot(const RenderNode& root) {
    return build_render_snapshot_node(root);
}

} // namespace nk
