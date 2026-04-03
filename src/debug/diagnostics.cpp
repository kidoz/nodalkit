#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nk/debug/diagnostics.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <optional>
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

void append_json_number_or_null(std::ostringstream& out, float value) {
    if (std::isfinite(value)) {
        out << value;
    } else {
        out << "null";
    }
}

std::string_view frame_performance_marker_name(FramePerformanceMarker marker) {
    switch (marker) {
    case FramePerformanceMarker::WithinBudget:
        return "within-budget";
    case FramePerformanceMarker::OverBudget:
        return "over-budget";
    case FramePerformanceMarker::Slow:
        return "slow";
    case FramePerformanceMarker::VerySlow:
        return "very-slow";
    }
    return "within-budget";
}

std::optional<FramePerformanceMarker> parse_frame_performance_marker_name(std::string_view value) {
    if (value == "within-budget") {
        return FramePerformanceMarker::WithinBudget;
    }
    if (value == "over-budget") {
        return FramePerformanceMarker::OverBudget;
    }
    if (value == "slow") {
        return FramePerformanceMarker::Slow;
    }
    if (value == "very-slow") {
        return FramePerformanceMarker::VerySlow;
    }
    return std::nullopt;
}

std::optional<FrameRequestReason> parse_frame_request_reason_name(std::string_view value) {
    if (value == "manual") {
        return FrameRequestReason::Manual;
    }
    if (value == "present") {
        return FrameRequestReason::Present;
    }
    if (value == "resize") {
        return FrameRequestReason::Resize;
    }
    if (value == "expose") {
        return FrameRequestReason::Expose;
    }
    if (value == "child-changed") {
        return FrameRequestReason::ChildChanged;
    }
    if (value == "overlay-changed") {
        return FrameRequestReason::OverlayChanged;
    }
    if (value == "layout-invalidation") {
        return FrameRequestReason::LayoutInvalidation;
    }
    if (value == "widget-redraw") {
        return FrameRequestReason::WidgetRedraw;
    }
    if (value == "widget-layout") {
        return FrameRequestReason::WidgetLayout;
    }
    if (value == "debug-overlay") {
        return FrameRequestReason::DebugOverlayChanged;
    }
    if (value == "debug-selection") {
        return FrameRequestReason::DebugSelectionChanged;
    }
    if (value == "debug-picker") {
        return FrameRequestReason::PickerChanged;
    }
    return std::nullopt;
}

std::optional<GpuPresentPath> parse_gpu_present_path_name(std::string_view value) {
    if (value == "none") {
        return GpuPresentPath::None;
    }
    if (value == "software-direct") {
        return GpuPresentPath::SoftwareDirect;
    }
    if (value == "software-upload") {
        return GpuPresentPath::SoftwareUpload;
    }
    if (value == "full-redraw-direct") {
        return GpuPresentPath::FullRedrawDirect;
    }
    if (value == "full-redraw-copy-back") {
        return GpuPresentPath::FullRedrawCopyBack;
    }
    if (value == "partial-redraw-copy") {
        return GpuPresentPath::PartialRedrawCopy;
    }
    return std::nullopt;
}

std::optional<GpuPresentTradeoff> parse_gpu_present_tradeoff_name(std::string_view value) {
    if (value == "none") {
        return GpuPresentTradeoff::None;
    }
    if (value == "bandwidth-favored") {
        return GpuPresentTradeoff::BandwidthFavored;
    }
    if (value == "draw-favored") {
        return GpuPresentTradeoff::DrawFavored;
    }
    return std::nullopt;
}

double frame_budget_overrun_ms(double total_ms) {
    return std::max(0.0, total_ms - frame_budget_ms());
}

Result<std::string> load_text_file(std::string_view path, std::string_view label) {
    const auto file_path = std::filesystem::path(std::string(path));
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        return Unexpected("failed to open " + std::string(label) + ": " + file_path.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        return Unexpected("failed to read " + std::string(label) + ": " + file_path.string());
    }
    return buffer.str();
}

Result<void>
save_text_file(std::string_view path, std::string_view contents, std::string_view label) {
    const auto file_path = std::filesystem::path(std::string(path));
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return Unexpected("failed to create " + std::string(label) + ": " + file_path.string());
    }

    out << contents;
    if (!out.good()) {
        return Unexpected("failed to write " + std::string(label) + ": " + file_path.string());
    }
    return {};
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

void append_widget_hotspot_counters_json(std::ostringstream& out,
                                         const WidgetHotspotCounters& counters);

void append_widget_debug_json(std::ostringstream& out, const WidgetDebugNode& node) {
    out << "{\"type_name\":\"" << escape_json_string(node.type_name) << "\"";
    if (!node.debug_name.empty()) {
        out << ",\"debug_name\":\"" << escape_json_string(node.debug_name) << "\"";
    }
    out << ",\"allocation\":{\"x\":" << node.allocation.x << ",\"y\":" << node.allocation.y
        << ",\"width\":" << node.allocation.width << ",\"height\":" << node.allocation.height
        << "}";
    out << ",\"state_flags\":" << static_cast<uint32_t>(node.state_flags);
    out << ",\"visible\":" << (node.visible ? "true" : "false");
    out << ",\"sensitive\":" << (node.sensitive ? "true" : "false");
    out << ",\"focusable\":" << (node.focusable ? "true" : "false");
    out << ",\"focused\":" << (node.focused ? "true" : "false");
    out << ",\"hovered\":" << (node.hovered ? "true" : "false");
    out << ",\"pressed\":" << (node.pressed ? "true" : "false");
    out << ",\"retain_size_when_hidden\":" << (node.retain_size_when_hidden ? "true" : "false");
    out << ",\"pending_redraw\":" << (node.pending_redraw ? "true" : "false");
    out << ",\"pending_layout\":" << (node.pending_layout ? "true" : "false");
    out << ",\"has_last_measure\":" << (node.has_last_measure ? "true" : "false");
    out << ",\"last_measure_constraints\":{\"min_width\":";
    append_json_number_or_null(out, node.last_constraint_min_width);
    out << ",\"min_height\":";
    append_json_number_or_null(out, node.last_constraint_min_height);
    out << ",\"max_width\":";
    append_json_number_or_null(out, node.last_constraint_max_width);
    out << ",\"max_height\":";
    append_json_number_or_null(out, node.last_constraint_max_height);
    out << "}";
    out << ",\"last_size_request\":{\"minimum_width\":" << node.last_request_minimum_width
        << ",\"minimum_height\":" << node.last_request_minimum_height
        << ",\"natural_width\":" << node.last_request_natural_width
        << ",\"natural_height\":" << node.last_request_natural_height << "}";
    out << ",\"horizontal_size_policy\":\"" << escape_json_string(node.horizontal_size_policy)
        << "\"";
    out << ",\"vertical_size_policy\":\"" << escape_json_string(node.vertical_size_policy) << "\"";
    out << ",\"horizontal_stretch\":" << static_cast<unsigned int>(node.horizontal_stretch);
    out << ",\"vertical_stretch\":" << static_cast<unsigned int>(node.vertical_stretch);
    out << ",\"hotspot_counters\":";
    append_widget_hotspot_counters_json(out, node.hotspot_counters);
    out << ",\"style_classes\":[";
    for (std::size_t index = 0; index < node.style_classes.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << "\"" << escape_json_string(node.style_classes[index]) << "\"";
    }
    out << "],\"children\":[";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        append_widget_debug_json(out, node.children[index]);
    }
    out << "]}";
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

void append_render_snapshot_file_json(std::ostringstream& out, const RenderSnapshotNode& node) {
    out << "{\"format\":\"" << render_snapshot_artifact_format() << "\",\"root\":";
    append_render_snapshot_json(out, node);
    out << "}\n";
}

void append_size_json(std::ostringstream& out, Size size) {
    out << "{\"width\":" << size.width << ",\"height\":" << size.height << "}";
}

void append_widget_hotspot_counters_json(std::ostringstream& out,
                                         const WidgetHotspotCounters& counters) {
    out << "{\"measure_count\":" << counters.measure_count
        << ",\"allocate_count\":" << counters.allocate_count
        << ",\"snapshot_count\":" << counters.snapshot_count
        << ",\"text_measure_count\":" << counters.text_measure_count
        << ",\"image_snapshot_count\":" << counters.image_snapshot_count
        << ",\"model_sync_count\":" << counters.model_sync_count
        << ",\"model_row_materialize_count\":" << counters.model_row_materialize_count
        << ",\"model_row_reuse_count\":" << counters.model_row_reuse_count
        << ",\"model_row_dispose_count\":" << counters.model_row_dispose_count << "}";
}

void append_render_hotspot_counters_json(std::ostringstream& out,
                                         const RenderHotspotCounters& counters) {
    out << "{\"text_node_count\":" << counters.text_node_count
        << ",\"text_shape_count\":" << counters.text_shape_count
        << ",\"text_shape_cache_hit_count\":" << counters.text_shape_cache_hit_count
        << ",\"text_bitmap_pixel_count\":" << counters.text_bitmap_pixel_count
        << ",\"text_texture_upload_count\":" << counters.text_texture_upload_count
        << ",\"image_node_count\":" << counters.image_node_count
        << ",\"image_source_pixel_count\":" << counters.image_source_pixel_count
        << ",\"image_dest_pixel_count\":" << counters.image_dest_pixel_count
        << ",\"image_texture_upload_count\":" << counters.image_texture_upload_count
        << ",\"damage_region_count\":" << counters.damage_region_count
        << ",\"gpu_draw_call_count\":" << counters.gpu_draw_call_count
        << ",\"gpu_present_region_count\":" << counters.gpu_present_region_count
        << ",\"gpu_swapchain_copy_count\":" << counters.gpu_swapchain_copy_count
        << ",\"gpu_viewport_pixel_count\":" << counters.gpu_viewport_pixel_count
        << ",\"gpu_estimated_draw_pixel_count\":" << counters.gpu_estimated_draw_pixel_count
        << ",\"gpu_present_path\":\""
        << escape_json_string(gpu_present_path_name(counters.gpu_present_path))
        << "\",\"gpu_present_tradeoff\":\""
        << escape_json_string(gpu_present_tradeoff_name(counters.gpu_present_tradeoff)) << "\"}";
}

void append_frame_request_reasons_json(std::ostringstream& out,
                                       std::span<const FrameRequestReason> reasons) {
    out << "[";
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << "\"" << escape_json_string(frame_request_reason_name(reasons[index])) << "\"";
    }
    out << "]";
}

void append_frame_diagnostics_json(std::ostringstream& out, const FrameDiagnostics& frame) {
    out << "{\"frame_id\":" << frame.frame_id << ",\"logical_viewport\":";
    append_size_json(out, frame.logical_viewport);
    out << ",\"scale_factor\":" << frame.scale_factor
        << ",\"had_layout\":" << (frame.had_layout ? "true" : "false")
        << ",\"requested_at_ms\":" << frame.requested_at_ms
        << ",\"started_at_ms\":" << frame.started_at_ms
        << ",\"queue_delay_ms\":" << frame.queue_delay_ms
        << ",\"widget_count\":" << frame.widget_count
        << ",\"render_node_count\":" << frame.render_node_count
        << ",\"dirty_widget_count\":" << frame.dirty_widget_count
        << ",\"redraw_request_count\":" << frame.redraw_request_count
        << ",\"layout_request_count\":" << frame.layout_request_count
        << ",\"layout_ms\":" << frame.layout_ms << ",\"snapshot_ms\":" << frame.snapshot_ms
        << ",\"render_ms\":" << frame.render_ms << ",\"present_ms\":" << frame.present_ms
        << ",\"total_ms\":" << frame.total_ms << ",\"performance_marker\":\""
        << escape_json_string(frame_performance_marker_name(frame.performance_marker))
        << "\",\"budget_overrun_ms\":" << frame.budget_overrun_ms << ",\"widget_hotspot_totals\":";
    append_widget_hotspot_counters_json(out, frame.widget_hotspot_totals);
    out << ",\"render_hotspot_counters\":";
    append_render_hotspot_counters_json(out, frame.render_hotspot_counters);
    out << ",\"request_reasons\":";
    append_frame_request_reasons_json(out, frame.request_reasons);
    out << ",\"render_snapshot_node_count\":" << frame.render_snapshot_node_count << "}";
}

void append_frame_diagnostics_artifact_json(std::ostringstream& out,
                                            std::span<const FrameDiagnostics> frames) {
    out << "{\"format\":\"" << frame_diagnostics_artifact_format() << "\",\"frames\":[";
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        append_frame_diagnostics_json(out, frames[index]);
    }
    out << "]}\n";
}

class WidgetDebugJsonParser {
public:
    explicit WidgetDebugJsonParser(std::string_view input) : input_(input) {}

    Result<WidgetDebugNode> parse_document() {
        skip_ws();
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }

        std::string format;
        std::optional<WidgetDebugNode> root;

        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("widget debug artifact is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }

            if (*key == "format") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                format = std::move(*value);
            } else if (*key == "root") {
                auto value = parse_node();
                if (!value) {
                    return Unexpected(value.error());
                }
                root = std::move(*value);
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        skip_ws();
        if (!eof()) {
            return Unexpected(make_error("unexpected trailing data"));
        }
        if (!format.empty() && format != widget_debug_artifact_format()) {
            return Unexpected(
                make_error("unsupported widget debug artifact format \"" + format + "\""));
        }
        if (!root.has_value()) {
            return Unexpected(make_error("widget debug artifact is missing root"));
        }
        return *root;
    }

private:
    struct NodeBuilder {
        WidgetDebugNode node;
        bool has_type_name = false;
        bool has_allocation = false;
        bool has_state_flags = false;
        bool has_visible = false;
        bool has_sensitive = false;
        bool has_focusable = false;
        bool has_focused = false;
        bool has_hovered = false;
        bool has_pressed = false;
        bool has_retain_size_when_hidden = false;
        bool has_pending_redraw = false;
        bool has_pending_layout = false;
        bool has_has_last_measure = false;
        bool has_last_measure_constraints = false;
        bool has_last_size_request = false;
        bool has_horizontal_size_policy = false;
        bool has_vertical_size_policy = false;
        bool has_horizontal_stretch = false;
        bool has_vertical_stretch = false;
        bool has_hotspot_counters = false;
        bool has_style_classes = false;
        bool has_children = false;
    };

    [[nodiscard]] bool eof() const noexcept { return index_ >= input_.size(); }

    [[nodiscard]] std::string make_error(std::string_view message) const {
        std::ostringstream out;
        out << message << " at byte " << index_;
        return out.str();
    }

    void skip_ws() {
        while (!eof()) {
            const char ch = input_[index_];
            if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
                break;
            }
            ++index_;
        }
    }

    bool consume_if(char expected) {
        skip_ws();
        if (!eof() && input_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    Result<void> expect(char expected) {
        skip_ws();
        if (eof() || input_[index_] != expected) {
            std::string message = "expected '";
            message += expected;
            message += "'";
            return Unexpected(make_error(message));
        }
        ++index_;
        return {};
    }

    Result<std::string> parse_string() {
        skip_ws();
        if (eof() || input_[index_] != '"') {
            return Unexpected(make_error("expected string"));
        }
        ++index_;

        std::string value;
        while (!eof()) {
            const char ch = input_[index_++];
            if (ch == '"') {
                return value;
            }
            if (ch == '\\') {
                if (eof()) {
                    return Unexpected(make_error("unterminated escape sequence"));
                }
                const char escaped = input_[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value += escaped;
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    return Unexpected(make_error("unsupported escape sequence"));
                }
                continue;
            }
            value += ch;
        }
        return Unexpected(make_error("unterminated string"));
    }

    Result<double> parse_number() {
        skip_ws();
        const std::size_t start = index_;
        while (!eof()) {
            const char ch = input_[index_];
            if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' ||
                ch == 'E') {
                ++index_;
                continue;
            }
            break;
        }
        if (start == index_) {
            return Unexpected(make_error("expected number"));
        }
        const std::string token(input_.substr(start, index_ - start));
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || errno == ERANGE) {
            return Unexpected(make_error("invalid number"));
        }
        return value;
    }

    Result<std::size_t> parse_size_t() {
        auto value = parse_number();
        if (!value) {
            return Unexpected(value.error());
        }
        if (*value < 0.0 || std::floor(*value) != *value) {
            return Unexpected(make_error("expected non-negative integer"));
        }
        return static_cast<std::size_t>(*value);
    }

    Result<bool> parse_bool() {
        skip_ws();
        if (input_.substr(index_, 4) == "true") {
            index_ += 4;
            return true;
        }
        if (input_.substr(index_, 5) == "false") {
            index_ += 5;
            return false;
        }
        return Unexpected(make_error("expected boolean"));
    }

    Result<std::optional<double>> parse_nullable_number() {
        skip_ws();
        if (input_.substr(index_, 4) == "null") {
            index_ += 4;
            return std::optional<double>{};
        }
        auto value = parse_number();
        if (!value) {
            return Unexpected(value.error());
        }
        return std::optional<double>{*value};
    }

    Result<void> skip_value() {
        skip_ws();
        if (eof()) {
            return Unexpected(make_error("unexpected end of input"));
        }
        const char ch = input_[index_];
        if (ch == '"') {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            return {};
        }
        if (ch == '{') {
            ++index_;
            skip_ws();
            if (consume_if('}')) {
                return {};
            }
            while (true) {
                auto key = parse_string();
                if (!key) {
                    return Unexpected(key.error());
                }
                auto colon = expect(':');
                if (!colon) {
                    return Unexpected(colon.error());
                }
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if('}')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == '[') {
            ++index_;
            skip_ws();
            if (consume_if(']')) {
                return {};
            }
            while (true) {
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if(']')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == 't' || ch == 'f') {
            auto value = parse_bool();
            if (!value) {
                return Unexpected(value.error());
            }
            return {};
        }
        auto number = parse_number();
        if (!number) {
            return Unexpected(number.error());
        }
        return {};
    }

    Result<Rect> parse_rect() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        Rect rect{};
        bool has_x = false;
        bool has_y = false;
        bool has_width = false;
        bool has_height = false;
        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("rect object is empty"));
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            auto value = parse_number();
            if (!value) {
                return Unexpected(value.error());
            }
            if (*key == "x") {
                rect.x = static_cast<float>(*value);
                has_x = true;
            } else if (*key == "y") {
                rect.y = static_cast<float>(*value);
                has_y = true;
            } else if (*key == "width") {
                rect.width = static_cast<float>(*value);
                has_width = true;
            } else if (*key == "height") {
                rect.height = static_cast<float>(*value);
                has_height = true;
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        if (!has_x || !has_y || !has_width || !has_height) {
            return Unexpected(make_error("rect object is missing required fields"));
        }
        return rect;
    }

    Result<void> parse_constraints(NodeBuilder& builder) {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        bool has_min_width = false;
        bool has_min_height = false;
        bool has_max_width = false;
        bool has_max_height = false;
        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("constraints object is empty"));
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            auto value = parse_nullable_number();
            if (!value) {
                return Unexpected(value.error());
            }
            const auto maybe_value = *value;
            if (*key == "min_width") {
                builder.node.last_constraint_min_width =
                    maybe_value.has_value() ? static_cast<float>(*maybe_value) : 0.0F;
                has_min_width = true;
            } else if (*key == "min_height") {
                builder.node.last_constraint_min_height =
                    maybe_value.has_value() ? static_cast<float>(*maybe_value) : 0.0F;
                has_min_height = true;
            } else if (*key == "max_width") {
                builder.node.last_constraint_max_width =
                    maybe_value.has_value() ? static_cast<float>(*maybe_value)
                                            : std::numeric_limits<float>::infinity();
                has_max_width = true;
            } else if (*key == "max_height") {
                builder.node.last_constraint_max_height =
                    maybe_value.has_value() ? static_cast<float>(*maybe_value)
                                            : std::numeric_limits<float>::infinity();
                has_max_height = true;
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        if (!has_min_width || !has_min_height || !has_max_width || !has_max_height) {
            return Unexpected(make_error("constraints object is missing required fields"));
        }
        builder.has_last_measure_constraints = true;
        return {};
    }

    Result<void> parse_size_request(NodeBuilder& builder) {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        bool has_minimum_width = false;
        bool has_minimum_height = false;
        bool has_natural_width = false;
        bool has_natural_height = false;
        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("size request object is empty"));
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            auto value = parse_number();
            if (!value) {
                return Unexpected(value.error());
            }
            if (*key == "minimum_width") {
                builder.node.last_request_minimum_width = static_cast<float>(*value);
                has_minimum_width = true;
            } else if (*key == "minimum_height") {
                builder.node.last_request_minimum_height = static_cast<float>(*value);
                has_minimum_height = true;
            } else if (*key == "natural_width") {
                builder.node.last_request_natural_width = static_cast<float>(*value);
                has_natural_width = true;
            } else if (*key == "natural_height") {
                builder.node.last_request_natural_height = static_cast<float>(*value);
                has_natural_height = true;
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        if (!has_minimum_width || !has_minimum_height || !has_natural_width ||
            !has_natural_height) {
            return Unexpected(make_error("size request object is missing required fields"));
        }
        builder.has_last_size_request = true;
        return {};
    }

    Result<WidgetHotspotCounters> parse_widget_hotspot_counters() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        WidgetHotspotCounters counters;
        skip_ws();
        if (consume_if('}')) {
            return counters;
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            auto value = parse_size_t();
            if (!value) {
                return Unexpected(value.error());
            }
            if (*key == "measure_count") {
                counters.measure_count = *value;
            } else if (*key == "allocate_count") {
                counters.allocate_count = *value;
            } else if (*key == "snapshot_count") {
                counters.snapshot_count = *value;
            } else if (*key == "text_measure_count") {
                counters.text_measure_count = *value;
            } else if (*key == "image_snapshot_count") {
                counters.image_snapshot_count = *value;
            } else if (*key == "model_sync_count") {
                counters.model_sync_count = *value;
            } else if (*key == "model_row_materialize_count") {
                counters.model_row_materialize_count = *value;
            } else if (*key == "model_row_reuse_count") {
                counters.model_row_reuse_count = *value;
            } else if (*key == "model_row_dispose_count") {
                counters.model_row_dispose_count = *value;
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return counters;
    }

    Result<std::vector<std::string>> parse_string_array() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }
        std::vector<std::string> values;
        skip_ws();
        if (consume_if(']')) {
            return values;
        }
        while (true) {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            values.push_back(std::move(*value));
            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return values;
    }

    Result<std::vector<WidgetDebugNode>> parse_children() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }
        std::vector<WidgetDebugNode> children;
        skip_ws();
        if (consume_if(']')) {
            return children;
        }
        while (true) {
            auto value = parse_node();
            if (!value) {
                return Unexpected(value.error());
            }
            children.push_back(std::move(*value));
            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return children;
    }

    Result<WidgetDebugNode> parse_node() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        NodeBuilder builder;
        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("widget debug node is empty"));
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "type_name") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.type_name = std::move(*value);
                builder.has_type_name = true;
            } else if (*key == "debug_name") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.debug_name = std::move(*value);
            } else if (*key == "allocation") {
                auto value = parse_rect();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.allocation = *value;
                builder.has_allocation = true;
            } else if (*key == "state_flags") {
                auto value = parse_size_t();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.state_flags = static_cast<StateFlags>(*value);
                builder.has_state_flags = true;
            } else if (*key == "visible") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.visible = *value;
                builder.has_visible = true;
            } else if (*key == "sensitive") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.sensitive = *value;
                builder.has_sensitive = true;
            } else if (*key == "focusable") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.focusable = *value;
                builder.has_focusable = true;
            } else if (*key == "focused") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.focused = *value;
                builder.has_focused = true;
            } else if (*key == "hovered") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.hovered = *value;
                builder.has_hovered = true;
            } else if (*key == "pressed") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.pressed = *value;
                builder.has_pressed = true;
            } else if (*key == "retain_size_when_hidden") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.retain_size_when_hidden = *value;
                builder.has_retain_size_when_hidden = true;
            } else if (*key == "pending_redraw") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.pending_redraw = *value;
                builder.has_pending_redraw = true;
            } else if (*key == "pending_layout") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.pending_layout = *value;
                builder.has_pending_layout = true;
            } else if (*key == "has_last_measure") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.has_last_measure = *value;
                builder.has_has_last_measure = true;
            } else if (*key == "last_measure_constraints") {
                auto value = parse_constraints(builder);
                if (!value) {
                    return Unexpected(value.error());
                }
            } else if (*key == "last_size_request") {
                auto value = parse_size_request(builder);
                if (!value) {
                    return Unexpected(value.error());
                }
            } else if (*key == "horizontal_size_policy") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.horizontal_size_policy = std::move(*value);
                builder.has_horizontal_size_policy = true;
            } else if (*key == "vertical_size_policy") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.vertical_size_policy = std::move(*value);
                builder.has_vertical_size_policy = true;
            } else if (*key == "horizontal_stretch") {
                auto value = parse_size_t();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.horizontal_stretch = static_cast<uint8_t>(*value);
                builder.has_horizontal_stretch = true;
            } else if (*key == "vertical_stretch") {
                auto value = parse_size_t();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.vertical_stretch = static_cast<uint8_t>(*value);
                builder.has_vertical_stretch = true;
            } else if (*key == "hotspot_counters") {
                auto value = parse_widget_hotspot_counters();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.hotspot_counters = *value;
                builder.has_hotspot_counters = true;
            } else if (*key == "style_classes") {
                auto value = parse_string_array();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.style_classes = std::move(*value);
                builder.has_style_classes = true;
            } else if (*key == "children") {
                auto value = parse_children();
                if (!value) {
                    return Unexpected(value.error());
                }
                builder.node.children = std::move(*value);
                builder.has_children = true;
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        if (!builder.has_type_name || !builder.has_allocation || !builder.has_state_flags ||
            !builder.has_visible || !builder.has_sensitive || !builder.has_focusable ||
            !builder.has_focused || !builder.has_hovered || !builder.has_pressed ||
            !builder.has_retain_size_when_hidden || !builder.has_pending_redraw ||
            !builder.has_pending_layout || !builder.has_has_last_measure ||
            !builder.has_last_measure_constraints || !builder.has_last_size_request ||
            !builder.has_horizontal_size_policy || !builder.has_vertical_size_policy ||
            !builder.has_horizontal_stretch || !builder.has_vertical_stretch ||
            !builder.has_hotspot_counters || !builder.has_style_classes || !builder.has_children) {
            return Unexpected(make_error("widget debug node is missing required fields"));
        }
        return std::move(builder.node);
    }

    std::string_view input_;
    std::size_t index_ = 0;
};

class RenderSnapshotJsonParser {
public:
    explicit RenderSnapshotJsonParser(std::string_view input) : input_(input) {}

    Result<RenderSnapshotNode> parse_document() {
        skip_ws();
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }

        NodeBuilder builder;
        bool has_node_fields = false;
        std::string format;
        std::optional<RenderSnapshotNode> wrapped_root;

        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("render snapshot object is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }

            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }

            if (*key == "format") {
                auto parsed_format = parse_string();
                if (!parsed_format) {
                    return Unexpected(parsed_format.error());
                }
                format = std::move(*parsed_format);
            } else if (*key == "root") {
                auto root = parse_node();
                if (!root) {
                    return Unexpected(root.error());
                }
                wrapped_root = std::move(*root);
            } else {
                has_node_fields = true;
                auto field_result = parse_node_field(builder, *key);
                if (!field_result) {
                    return Unexpected(field_result.error());
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        skip_ws();
        if (!eof()) {
            return Unexpected(make_error("unexpected trailing data"));
        }

        if (wrapped_root.has_value()) {
            if (!format.empty() && format != render_snapshot_artifact_format()) {
                return Unexpected(
                    make_error("unsupported render snapshot format \"" + format + "\""));
            }
            if (has_node_fields) {
                return Unexpected(make_error("mixed render snapshot wrapper and root node fields"));
            }
            return std::move(*wrapped_root);
        }

        return finalize_node(std::move(builder));
    }

private:
    struct NodeBuilder {
        RenderSnapshotNode node;
        bool has_kind = false;
        bool has_bounds = false;
    };

    [[nodiscard]] bool eof() const noexcept { return index_ >= input_.size(); }

    [[nodiscard]] std::string make_error(std::string_view message) const {
        std::ostringstream out;
        out << message << " at byte " << index_;
        return out.str();
    }

    void skip_ws() {
        while (!eof()) {
            const char ch = input_[index_];
            if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
                break;
            }
            ++index_;
        }
    }

    bool consume_if(char expected) {
        skip_ws();
        if (!eof() && input_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    Result<void> expect(char expected) {
        skip_ws();
        if (eof() || input_[index_] != expected) {
            std::string message = "expected '";
            message += expected;
            message += "'";
            return Unexpected(make_error(message));
        }
        ++index_;
        return {};
    }

    Result<std::string> parse_string() {
        skip_ws();
        if (eof() || input_[index_] != '"') {
            return Unexpected(make_error("expected string"));
        }
        ++index_;

        std::string value;
        while (!eof()) {
            const char ch = input_[index_++];
            if (ch == '"') {
                return value;
            }
            if (ch == '\\') {
                if (eof()) {
                    return Unexpected(make_error("unterminated escape sequence"));
                }
                const char escaped = input_[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value += escaped;
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    return Unexpected(make_error("unsupported escape sequence"));
                }
                continue;
            }
            value += ch;
        }
        return Unexpected(make_error("unterminated string"));
    }

    Result<double> parse_number() {
        skip_ws();
        const std::size_t start = index_;
        while (!eof()) {
            const char ch = input_[index_];
            if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' ||
                ch == 'E') {
                ++index_;
                continue;
            }
            break;
        }

        if (start == index_) {
            return Unexpected(make_error("expected number"));
        }

        const std::string token(input_.substr(start, index_ - start));
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || errno == ERANGE) {
            return Unexpected(make_error("invalid number"));
        }
        return value;
    }

    Result<std::size_t> parse_size_t() {
        auto parsed = parse_number();
        if (!parsed) {
            return Unexpected(parsed.error());
        }
        if (*parsed < 0.0 || std::floor(*parsed) != *parsed) {
            return Unexpected(make_error("expected non-negative integer"));
        }
        return static_cast<std::size_t>(*parsed);
    }

    Result<void> consume_literal(std::string_view literal) {
        skip_ws();
        if (input_.substr(index_, literal.size()) != literal) {
            return Unexpected(make_error("expected literal"));
        }
        index_ += literal.size();
        return {};
    }

    Result<void> skip_value() {
        skip_ws();
        if (eof()) {
            return Unexpected(make_error("unexpected end of input"));
        }

        const char ch = input_[index_];
        if (ch == '"') {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            return {};
        }
        if (ch == '{') {
            ++index_;
            skip_ws();
            if (consume_if('}')) {
                return {};
            }
            while (true) {
                auto key = parse_string();
                if (!key) {
                    return Unexpected(key.error());
                }
                auto colon = expect(':');
                if (!colon) {
                    return Unexpected(colon.error());
                }
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if('}')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == '[') {
            ++index_;
            skip_ws();
            if (consume_if(']')) {
                return {};
            }
            while (true) {
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if(']')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == 't') {
            return consume_literal("true");
        }
        if (ch == 'f') {
            return consume_literal("false");
        }
        if (ch == 'n') {
            return consume_literal("null");
        }

        auto number = parse_number();
        if (!number) {
            return Unexpected(number.error());
        }
        return {};
    }

    Result<Rect> parse_bounds() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }

        Rect bounds{};
        bool has_x = false;
        bool has_y = false;
        bool has_width = false;
        bool has_height = false;

        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("bounds object is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }

            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }

            if (*key == "x") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                bounds.x = static_cast<float>(*value);
                has_x = true;
            } else if (*key == "y") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                bounds.y = static_cast<float>(*value);
                has_y = true;
            } else if (*key == "width") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                bounds.width = static_cast<float>(*value);
                has_width = true;
            } else if (*key == "height") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                bounds.height = static_cast<float>(*value);
                has_height = true;
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        if (!has_x || !has_y || !has_width || !has_height) {
            return Unexpected(make_error("bounds object is missing required fields"));
        }
        return bounds;
    }

    Result<std::vector<std::size_t>> parse_size_t_array() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }

        std::vector<std::size_t> values;
        skip_ws();
        if (consume_if(']')) {
            return values;
        }

        while (true) {
            auto value = parse_size_t();
            if (!value) {
                return Unexpected(value.error());
            }
            values.push_back(*value);

            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return values;
    }

    Result<std::vector<RenderSnapshotNode>> parse_children() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }

        std::vector<RenderSnapshotNode> children;
        skip_ws();
        if (consume_if(']')) {
            return children;
        }

        while (true) {
            auto child = parse_node();
            if (!child) {
                return Unexpected(child.error());
            }
            children.push_back(std::move(*child));

            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return children;
    }

    Result<void> parse_node_field(NodeBuilder& builder, std::string_view key) {
        if (key == "kind") {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            builder.node.kind = std::move(*value);
            builder.has_kind = true;
            return {};
        }
        if (key == "bounds") {
            auto value = parse_bounds();
            if (!value) {
                return Unexpected(value.error());
            }
            builder.node.bounds = *value;
            builder.has_bounds = true;
            return {};
        }
        if (key == "detail") {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            builder.node.detail = std::move(*value);
            return {};
        }
        if (key == "source_widget_label") {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            builder.node.source_widget_label = std::move(*value);
            return {};
        }
        if (key == "source_widget_path") {
            auto value = parse_size_t_array();
            if (!value) {
                return Unexpected(value.error());
            }
            builder.node.source_widget_path = std::move(*value);
            return {};
        }
        if (key == "children") {
            auto value = parse_children();
            if (!value) {
                return Unexpected(value.error());
            }
            builder.node.children = std::move(*value);
            return {};
        }

        return skip_value();
    }

    Result<RenderSnapshotNode> finalize_node(NodeBuilder builder) {
        if (!builder.has_kind) {
            return Unexpected(make_error("render snapshot node is missing kind"));
        }
        if (!builder.has_bounds) {
            return Unexpected(make_error("render snapshot node is missing bounds"));
        }
        return std::move(builder.node);
    }

    Result<RenderSnapshotNode> parse_node() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }

        NodeBuilder builder;
        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("render snapshot node is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }

            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }

            auto field_result = parse_node_field(builder, *key);
            if (!field_result) {
                return Unexpected(field_result.error());
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        return finalize_node(std::move(builder));
    }

    std::string_view input_;
    std::size_t index_ = 0;
};

class FrameDiagnosticsArtifactJsonParser {
public:
    explicit FrameDiagnosticsArtifactJsonParser(std::string_view input) : input_(input) {}

    Result<FrameDiagnosticsArtifact> parse_document() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }

        FrameDiagnosticsArtifact artifact;
        std::string format;

        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("frame diagnostics artifact is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }

            if (*key == "format") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                format = std::move(*value);
            } else if (*key == "frames") {
                auto frames = parse_frames();
                if (!frames) {
                    return Unexpected(frames.error());
                }
                artifact.frames = std::move(*frames);
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        skip_ws();
        if (!eof()) {
            return Unexpected(make_error("unexpected trailing data"));
        }
        if (!format.empty() && format != frame_diagnostics_artifact_format()) {
            return Unexpected(
                make_error("unsupported frame diagnostics artifact format \"" + format + "\""));
        }
        return artifact;
    }

private:
    [[nodiscard]] bool eof() const noexcept { return index_ >= input_.size(); }

    [[nodiscard]] std::string make_error(std::string_view message) const {
        std::ostringstream out;
        out << message << " at byte " << index_;
        return out.str();
    }

    void skip_ws() {
        while (!eof()) {
            const char ch = input_[index_];
            if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
                break;
            }
            ++index_;
        }
    }

    bool consume_if(char expected) {
        skip_ws();
        if (!eof() && input_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    Result<void> expect(char expected) {
        skip_ws();
        if (eof() || input_[index_] != expected) {
            std::string message = "expected '";
            message += expected;
            message += "'";
            return Unexpected(make_error(message));
        }
        ++index_;
        return {};
    }

    Result<std::string> parse_string() {
        skip_ws();
        if (eof() || input_[index_] != '"') {
            return Unexpected(make_error("expected string"));
        }
        ++index_;

        std::string value;
        while (!eof()) {
            const char ch = input_[index_++];
            if (ch == '"') {
                return value;
            }
            if (ch == '\\') {
                if (eof()) {
                    return Unexpected(make_error("unterminated escape sequence"));
                }
                const char escaped = input_[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value += escaped;
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    return Unexpected(make_error("unsupported escape sequence"));
                }
                continue;
            }
            value += ch;
        }
        return Unexpected(make_error("unterminated string"));
    }

    Result<double> parse_number() {
        skip_ws();
        const std::size_t start = index_;
        while (!eof()) {
            const char ch = input_[index_];
            if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' ||
                ch == 'E') {
                ++index_;
                continue;
            }
            break;
        }

        if (start == index_) {
            return Unexpected(make_error("expected number"));
        }

        const std::string token(input_.substr(start, index_ - start));
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || errno == ERANGE) {
            return Unexpected(make_error("invalid number"));
        }
        return value;
    }

    Result<std::size_t> parse_size_t() {
        auto value = parse_number();
        if (!value) {
            return Unexpected(value.error());
        }
        if (*value < 0.0 || std::floor(*value) != *value) {
            return Unexpected(make_error("expected non-negative integer"));
        }
        return static_cast<std::size_t>(*value);
    }

    Result<bool> parse_bool() {
        skip_ws();
        if (input_.substr(index_, 4) == "true") {
            index_ += 4;
            return true;
        }
        if (input_.substr(index_, 5) == "false") {
            index_ += 5;
            return false;
        }
        return Unexpected(make_error("expected boolean"));
    }

    Result<void> consume_literal(std::string_view literal) {
        skip_ws();
        if (input_.substr(index_, literal.size()) != literal) {
            return Unexpected(make_error("expected literal"));
        }
        index_ += literal.size();
        return {};
    }

    Result<void> skip_value() {
        skip_ws();
        if (eof()) {
            return Unexpected(make_error("unexpected end of input"));
        }

        const char ch = input_[index_];
        if (ch == '"') {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            return {};
        }
        if (ch == '{') {
            ++index_;
            skip_ws();
            if (consume_if('}')) {
                return {};
            }
            while (true) {
                auto key = parse_string();
                if (!key) {
                    return Unexpected(key.error());
                }
                auto colon = expect(':');
                if (!colon) {
                    return Unexpected(colon.error());
                }
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if('}')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == '[') {
            ++index_;
            skip_ws();
            if (consume_if(']')) {
                return {};
            }
            while (true) {
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if(']')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == 't') {
            return consume_literal("true");
        }
        if (ch == 'f') {
            return consume_literal("false");
        }
        if (ch == 'n') {
            return consume_literal("null");
        }
        auto number = parse_number();
        if (!number) {
            return Unexpected(number.error());
        }
        return {};
    }

    Result<Size> parse_size() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        Size size{};
        bool has_width = false;
        bool has_height = false;

        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("size object is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "width") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                size.width = static_cast<float>(*value);
                has_width = true;
            } else if (*key == "height") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                size.height = static_cast<float>(*value);
                has_height = true;
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        if (!has_width || !has_height) {
            return Unexpected(make_error("size object is missing required fields"));
        }
        return size;
    }

    Result<std::vector<FrameRequestReason>> parse_request_reasons() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }
        std::vector<FrameRequestReason> reasons;
        skip_ws();
        if (consume_if(']')) {
            return reasons;
        }
        while (true) {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            if (const auto parsed = parse_frame_request_reason_name(*value); parsed.has_value()) {
                reasons.push_back(*parsed);
            }
            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return reasons;
    }

    Result<WidgetHotspotCounters> parse_widget_hotspot_counters() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        WidgetHotspotCounters counters;
        skip_ws();
        if (consume_if('}')) {
            return counters;
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            auto value = parse_size_t();
            if (!value) {
                return Unexpected(value.error());
            }
            if (*key == "measure_count") {
                counters.measure_count = *value;
            } else if (*key == "allocate_count") {
                counters.allocate_count = *value;
            } else if (*key == "snapshot_count") {
                counters.snapshot_count = *value;
            } else if (*key == "text_measure_count") {
                counters.text_measure_count = *value;
            } else if (*key == "image_snapshot_count") {
                counters.image_snapshot_count = *value;
            } else if (*key == "model_sync_count") {
                counters.model_sync_count = *value;
            } else if (*key == "model_row_materialize_count") {
                counters.model_row_materialize_count = *value;
            } else if (*key == "model_row_reuse_count") {
                counters.model_row_reuse_count = *value;
            } else if (*key == "model_row_dispose_count") {
                counters.model_row_dispose_count = *value;
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return counters;
    }

    Result<RenderHotspotCounters> parse_render_hotspot_counters() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        RenderHotspotCounters counters;
        skip_ws();
        if (consume_if('}')) {
            return counters;
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "gpu_present_path") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                if (const auto parsed = parse_gpu_present_path_name(*value); parsed.has_value()) {
                    counters.gpu_present_path = *parsed;
                }
            } else if (*key == "gpu_present_tradeoff") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                if (const auto parsed = parse_gpu_present_tradeoff_name(*value);
                    parsed.has_value()) {
                    counters.gpu_present_tradeoff = *parsed;
                }
            } else {
                auto value = parse_size_t();
                if (!value) {
                    return Unexpected(value.error());
                }
                if (*key == "text_node_count") {
                    counters.text_node_count = *value;
                } else if (*key == "text_shape_count") {
                    counters.text_shape_count = *value;
                } else if (*key == "text_shape_cache_hit_count") {
                    counters.text_shape_cache_hit_count = *value;
                } else if (*key == "text_bitmap_pixel_count") {
                    counters.text_bitmap_pixel_count = *value;
                } else if (*key == "text_texture_upload_count") {
                    counters.text_texture_upload_count = *value;
                } else if (*key == "image_node_count") {
                    counters.image_node_count = *value;
                } else if (*key == "image_source_pixel_count") {
                    counters.image_source_pixel_count = *value;
                } else if (*key == "image_dest_pixel_count") {
                    counters.image_dest_pixel_count = *value;
                } else if (*key == "image_texture_upload_count") {
                    counters.image_texture_upload_count = *value;
                } else if (*key == "damage_region_count") {
                    counters.damage_region_count = *value;
                } else if (*key == "gpu_draw_call_count") {
                    counters.gpu_draw_call_count = *value;
                } else if (*key == "gpu_present_region_count") {
                    counters.gpu_present_region_count = *value;
                } else if (*key == "gpu_swapchain_copy_count") {
                    counters.gpu_swapchain_copy_count = *value;
                } else if (*key == "gpu_viewport_pixel_count") {
                    counters.gpu_viewport_pixel_count = *value;
                } else if (*key == "gpu_estimated_draw_pixel_count") {
                    counters.gpu_estimated_draw_pixel_count = *value;
                }
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return counters;
    }

    Result<FrameDiagnostics> parse_frame() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        FrameDiagnostics frame;
        skip_ws();
        if (consume_if('}')) {
            return frame;
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "frame_id") {
                auto value = parse_size_t();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.frame_id = static_cast<uint64_t>(*value);
            } else if (*key == "logical_viewport") {
                auto value = parse_size();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.logical_viewport = *value;
            } else if (*key == "scale_factor") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.scale_factor = static_cast<float>(*value);
            } else if (*key == "had_layout") {
                auto value = parse_bool();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.had_layout = *value;
            } else if (*key == "requested_at_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.requested_at_ms = *value;
            } else if (*key == "started_at_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.started_at_ms = *value;
            } else if (*key == "queue_delay_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.queue_delay_ms = *value;
            } else if (*key == "layout_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.layout_ms = *value;
            } else if (*key == "snapshot_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.snapshot_ms = *value;
            } else if (*key == "render_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.render_ms = *value;
            } else if (*key == "present_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.present_ms = *value;
            } else if (*key == "total_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.total_ms = *value;
            } else if (*key == "performance_marker") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                if (const auto parsed = parse_frame_performance_marker_name(*value);
                    parsed.has_value()) {
                    frame.performance_marker = *parsed;
                }
            } else if (*key == "budget_overrun_ms") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.budget_overrun_ms = *value;
            } else if (*key == "widget_hotspot_totals") {
                auto value = parse_widget_hotspot_counters();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.widget_hotspot_totals = *value;
            } else if (*key == "render_hotspot_counters") {
                auto value = parse_render_hotspot_counters();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.render_hotspot_counters = *value;
            } else if (*key == "request_reasons") {
                auto value = parse_request_reasons();
                if (!value) {
                    return Unexpected(value.error());
                }
                frame.request_reasons = std::move(*value);
            } else {
                auto integer_target = [&](std::size_t& target) -> Result<void> {
                    auto value = parse_size_t();
                    if (!value) {
                        return Unexpected(value.error());
                    }
                    target = *value;
                    return {};
                };
                if (*key == "widget_count") {
                    auto result = integer_target(frame.widget_count);
                    if (!result) {
                        return Unexpected(result.error());
                    }
                } else if (*key == "render_node_count") {
                    auto result = integer_target(frame.render_node_count);
                    if (!result) {
                        return Unexpected(result.error());
                    }
                } else if (*key == "dirty_widget_count") {
                    auto result = integer_target(frame.dirty_widget_count);
                    if (!result) {
                        return Unexpected(result.error());
                    }
                } else if (*key == "redraw_request_count") {
                    auto result = integer_target(frame.redraw_request_count);
                    if (!result) {
                        return Unexpected(result.error());
                    }
                } else if (*key == "layout_request_count") {
                    auto result = integer_target(frame.layout_request_count);
                    if (!result) {
                        return Unexpected(result.error());
                    }
                } else if (*key == "render_snapshot_node_count") {
                    auto result = integer_target(frame.render_snapshot_node_count);
                    if (!result) {
                        return Unexpected(result.error());
                    }
                } else {
                    auto skipped = skip_value();
                    if (!skipped) {
                        return Unexpected(skipped.error());
                    }
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        if (frame.performance_marker == FramePerformanceMarker::WithinBudget &&
            frame.total_ms > frame_budget_ms()) {
            frame.performance_marker = classify_frame_time(frame.total_ms);
        }
        if (frame.budget_overrun_ms <= 0.0) {
            frame.budget_overrun_ms = frame_budget_overrun_ms(frame.total_ms);
        }
        return frame;
    }

    Result<std::vector<FrameDiagnostics>> parse_frames() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }
        std::vector<FrameDiagnostics> frames;
        skip_ws();
        if (consume_if(']')) {
            return frames;
        }
        while (true) {
            auto frame = parse_frame();
            if (!frame) {
                return Unexpected(frame.error());
            }
            frames.push_back(std::move(*frame));
            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return frames;
    }

    std::string_view input_;
    std::size_t index_ = 0;
};

class TraceCaptureJsonParser {
public:
    explicit TraceCaptureJsonParser(std::string_view input) : input_(input) {}

    Result<TraceCapture> parse_document() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        TraceCapture capture;
        std::string format;

        skip_ws();
        if (consume_if('}')) {
            return Unexpected(make_error("trace capture is empty"));
        }

        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "format") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                format = std::move(*value);
            } else if (*key == "traceEvents") {
                auto events = parse_trace_events();
                if (!events) {
                    return Unexpected(events.error());
                }
                capture.events = std::move(*events);
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }

            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }

        skip_ws();
        if (!eof()) {
            return Unexpected(make_error("unexpected trailing data"));
        }
        if (!format.empty() && format != frame_diagnostics_trace_export_format()) {
            return Unexpected(make_error("unsupported trace capture format \"" + format + "\""));
        }
        return capture;
    }

private:
    [[nodiscard]] bool eof() const noexcept { return index_ >= input_.size(); }

    [[nodiscard]] std::string make_error(std::string_view message) const {
        std::ostringstream out;
        out << message << " at byte " << index_;
        return out.str();
    }

    void skip_ws() {
        while (!eof()) {
            const char ch = input_[index_];
            if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
                break;
            }
            ++index_;
        }
    }

    bool consume_if(char expected) {
        skip_ws();
        if (!eof() && input_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    Result<void> expect(char expected) {
        skip_ws();
        if (eof() || input_[index_] != expected) {
            std::string message = "expected '";
            message += expected;
            message += "'";
            return Unexpected(make_error(message));
        }
        ++index_;
        return {};
    }

    Result<std::string> parse_string() {
        skip_ws();
        if (eof() || input_[index_] != '"') {
            return Unexpected(make_error("expected string"));
        }
        ++index_;
        std::string value;
        while (!eof()) {
            const char ch = input_[index_++];
            if (ch == '"') {
                return value;
            }
            if (ch == '\\') {
                if (eof()) {
                    return Unexpected(make_error("unterminated escape sequence"));
                }
                const char escaped = input_[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value += escaped;
                    break;
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    return Unexpected(make_error("unsupported escape sequence"));
                }
                continue;
            }
            value += ch;
        }
        return Unexpected(make_error("unterminated string"));
    }

    Result<double> parse_number() {
        skip_ws();
        const std::size_t start = index_;
        while (!eof()) {
            const char ch = input_[index_];
            if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' ||
                ch == 'E') {
                ++index_;
                continue;
            }
            break;
        }
        if (start == index_) {
            return Unexpected(make_error("expected number"));
        }
        const std::string token(input_.substr(start, index_ - start));
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || errno == ERANGE) {
            return Unexpected(make_error("invalid number"));
        }
        return value;
    }

    Result<void> consume_literal(std::string_view literal) {
        skip_ws();
        if (input_.substr(index_, literal.size()) != literal) {
            return Unexpected(make_error("expected literal"));
        }
        index_ += literal.size();
        return {};
    }

    Result<void> skip_value() {
        skip_ws();
        if (eof()) {
            return Unexpected(make_error("unexpected end of input"));
        }
        const char ch = input_[index_];
        if (ch == '"') {
            auto value = parse_string();
            if (!value) {
                return Unexpected(value.error());
            }
            return {};
        }
        if (ch == '{') {
            ++index_;
            skip_ws();
            if (consume_if('}')) {
                return {};
            }
            while (true) {
                auto key = parse_string();
                if (!key) {
                    return Unexpected(key.error());
                }
                auto colon = expect(':');
                if (!colon) {
                    return Unexpected(colon.error());
                }
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if('}')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == '[') {
            ++index_;
            skip_ws();
            if (consume_if(']')) {
                return {};
            }
            while (true) {
                auto nested = skip_value();
                if (!nested) {
                    return Unexpected(nested.error());
                }
                skip_ws();
                if (consume_if(']')) {
                    return {};
                }
                auto comma = expect(',');
                if (!comma) {
                    return Unexpected(comma.error());
                }
            }
        }
        if (ch == 't') {
            return consume_literal("true");
        }
        if (ch == 'f') {
            return consume_literal("false");
        }
        if (ch == 'n') {
            return consume_literal("null");
        }
        auto number = parse_number();
        if (!number) {
            return Unexpected(number.error());
        }
        return {};
    }

    Result<TraceEvent> parse_trace_event() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        TraceEvent event;
        skip_ws();
        if (consume_if('}')) {
            return event;
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "name") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                event.name = std::move(*value);
            } else if (*key == "cat") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                event.category = std::move(*value);
            } else if (*key == "ts") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                event.timestamp_ms = *value / 1000.0;
            } else if (*key == "dur") {
                auto value = parse_number();
                if (!value) {
                    return Unexpected(value.error());
                }
                event.duration_ms = *value / 1000.0;
            } else if (*key == "args") {
                auto value = parse_trace_args();
                if (!value) {
                    return Unexpected(value.error());
                }
                if (!value->first.empty()) {
                    event.detail = std::move(value->first);
                }
                if (!value->second.empty()) {
                    event.source_label = std::move(value->second);
                }
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return event;
    }

    Result<std::pair<std::string, std::string>> parse_trace_args() {
        auto begin = expect('{');
        if (!begin) {
            return Unexpected(begin.error());
        }
        std::pair<std::string, std::string> result;
        skip_ws();
        if (consume_if('}')) {
            return result;
        }
        while (true) {
            auto key = parse_string();
            if (!key) {
                return Unexpected(key.error());
            }
            auto colon = expect(':');
            if (!colon) {
                return Unexpected(colon.error());
            }
            if (*key == "detail") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                result.first = std::move(*value);
            } else if (*key == "source_label") {
                auto value = parse_string();
                if (!value) {
                    return Unexpected(value.error());
                }
                result.second = std::move(*value);
            } else {
                auto skipped = skip_value();
                if (!skipped) {
                    return Unexpected(skipped.error());
                }
            }
            skip_ws();
            if (consume_if('}')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return result;
    }

    Result<std::vector<TraceEvent>> parse_trace_events() {
        auto begin = expect('[');
        if (!begin) {
            return Unexpected(begin.error());
        }
        std::vector<TraceEvent> events;
        skip_ws();
        if (consume_if(']')) {
            return events;
        }
        while (true) {
            auto event = parse_trace_event();
            if (!event) {
                return Unexpected(event.error());
            }
            events.push_back(std::move(*event));
            skip_ws();
            if (consume_if(']')) {
                break;
            }
            auto comma = expect(',');
            if (!comma) {
                return Unexpected(comma.error());
            }
        }
        return events;
    }

    std::string_view input_;
    std::size_t index_ = 0;
};

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

std::string_view gpu_present_path_name(GpuPresentPath path) noexcept {
    switch (path) {
    case GpuPresentPath::None:
        return "none";
    case GpuPresentPath::SoftwareDirect:
        return "software-direct";
    case GpuPresentPath::SoftwareUpload:
        return "software-upload";
    case GpuPresentPath::FullRedrawDirect:
        return "full-redraw-direct";
    case GpuPresentPath::FullRedrawCopyBack:
        return "full-redraw-copy-back";
    case GpuPresentPath::PartialRedrawCopy:
        return "partial-redraw-copy";
    }
    return "unknown";
}

std::string_view gpu_present_tradeoff_name(GpuPresentTradeoff tradeoff) noexcept {
    switch (tradeoff) {
    case GpuPresentTradeoff::None:
        return "none";
    case GpuPresentTradeoff::BandwidthFavored:
        return "bandwidth-favored";
    case GpuPresentTradeoff::DrawFavored:
        return "draw-favored";
    }
    return "unknown";
}

bool has_frame_request_reason(const FrameDiagnostics& frame, FrameRequestReason reason) noexcept {
    return std::find(frame.request_reasons.begin(), frame.request_reasons.end(), reason) !=
           frame.request_reasons.end();
}

FramePerformanceMarker classify_frame_time(double total_ms) noexcept {
    if (total_ms <= frame_budget_ms()) {
        return FramePerformanceMarker::WithinBudget;
    }
    if (total_ms <= 33.34) {
        return FramePerformanceMarker::OverBudget;
    }
    if (total_ms <= 66.68) {
        return FramePerformanceMarker::Slow;
    }
    return FramePerformanceMarker::VerySlow;
}

FrameTimeHistogram build_frame_time_histogram(std::span<const FrameDiagnostics> frames) noexcept {
    FrameTimeHistogram histogram;
    for (const auto& frame : frames) {
        switch (classify_frame_time(frame.total_ms)) {
        case FramePerformanceMarker::WithinBudget:
            ++histogram.within_budget_count;
            break;
        case FramePerformanceMarker::OverBudget:
            ++histogram.over_budget_count;
            break;
        case FramePerformanceMarker::Slow:
            ++histogram.slow_count;
            break;
        case FramePerformanceMarker::VerySlow:
            ++histogram.very_slow_count;
            break;
        }
    }
    return histogram;
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

std::string format_widget_debug_json(const WidgetDebugNode& root) {
    std::ostringstream out;
    out << "{\"format\":\"" << widget_debug_artifact_format() << "\",\"root\":";
    append_widget_debug_json(out, root);
    out << "}\n";
    return out.str();
}

Result<WidgetDebugNode> parse_widget_debug_json(std::string_view json) {
    WidgetDebugJsonParser parser(json);
    return parser.parse_document();
}

Result<WidgetDebugNode> load_widget_debug_json_file(std::string_view path) {
    auto contents = load_text_file(path, "widget debug artifact");
    if (!contents) {
        return Unexpected(contents.error());
    }
    return parse_widget_debug_json(*contents);
}

Result<void> save_widget_debug_json_file(const WidgetDebugNode& root, std::string_view path) {
    return save_text_file(path, format_widget_debug_json(root), "widget debug artifact");
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

Result<RenderSnapshotNode> parse_render_snapshot_json(std::string_view json) {
    RenderSnapshotJsonParser parser(json);
    return parser.parse_document();
}

Result<RenderSnapshotNode> load_render_snapshot_json_file(std::string_view path) {
    const auto file_path = std::filesystem::path(std::string(path));
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        return Unexpected("failed to open render snapshot file: " + file_path.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        return Unexpected("failed to read render snapshot file: " + file_path.string());
    }

    return parse_render_snapshot_json(buffer.str());
}

Result<void> save_render_snapshot_json_file(const RenderSnapshotNode& root, std::string_view path) {
    std::ostringstream buffer;
    append_render_snapshot_file_json(buffer, root);
    return save_text_file(path, buffer.str(), "render snapshot file");
}

std::string format_frame_diagnostics_artifact_json(std::span<const FrameDiagnostics> frames) {
    std::ostringstream out;
    append_frame_diagnostics_artifact_json(out, frames);
    return out.str();
}

Result<FrameDiagnosticsArtifact> parse_frame_diagnostics_artifact_json(std::string_view json) {
    FrameDiagnosticsArtifactJsonParser parser(json);
    return parser.parse_document();
}

Result<FrameDiagnosticsArtifact> load_frame_diagnostics_artifact_json_file(std::string_view path) {
    auto contents = load_text_file(path, "frame diagnostics artifact");
    if (!contents) {
        return Unexpected(contents.error());
    }
    return parse_frame_diagnostics_artifact_json(*contents);
}

Result<void> save_frame_diagnostics_artifact_json_file(const FrameDiagnosticsArtifact& artifact,
                                                       std::string_view path) {
    return save_text_file(path,
                          format_frame_diagnostics_artifact_json(artifact.frames),
                          "frame diagnostics artifact");
}

std::string format_frame_diagnostics_trace_json(std::span<const FrameDiagnostics> frames,
                                                std::span<const TraceEvent> extra_events) {
    std::ostringstream out;
    out << "{\n  \"format\": \"" << frame_diagnostics_trace_export_format()
        << "\",\n  \"traceEvents\": [\n";
    bool first_event = true;

    for (const auto& event : extra_events) {
        append_trace_event_header(out, first_event);
        out << "    {\"name\":\"" << escape_json_string(event.name) << "\",\"cat\":\""
            << escape_json_string(event.category) << "\",\"ph\":\"X\",\"ts\":"
            << static_cast<long long>(std::llround(event.timestamp_ms * 1000.0))
            << ",\"dur\":" << static_cast<long long>(std::llround(event.duration_ms * 1000.0))
            << ",\"pid\":1,\"tid\":2";
        if (!event.detail.empty() || !event.source_label.empty()) {
            out << ",\"args\":{";
            bool first_arg = true;
            if (!event.detail.empty()) {
                out << "\"detail\":\"" << escape_json_string(event.detail) << "\"";
                first_arg = false;
            }
            if (!event.source_label.empty()) {
                if (!first_arg) {
                    out << ",";
                }
                out << "\"source_label\":\"" << escape_json_string(event.source_label) << "\"";
            }
            out << "}";
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
            << ",\"queue_delay_ms\":" << frame.queue_delay_ms << ",\"present_path\":\""
            << escape_json_string(
                   gpu_present_path_name(frame.render_hotspot_counters.gpu_present_path))
            << "\",\"present_tradeoff\":\""
            << escape_json_string(
                   gpu_present_tradeoff_name(frame.render_hotspot_counters.gpu_present_tradeoff))
            << "\",\"gpu_draw_calls\":" << frame.render_hotspot_counters.gpu_draw_call_count
            << ",\"gpu_viewport_pixels\":" << frame.render_hotspot_counters.gpu_viewport_pixel_count
            << ",\"gpu_draw_pixels\":"
            << frame.render_hotspot_counters.gpu_estimated_draw_pixel_count
            << ",\"gpu_present_regions\":" << frame.render_hotspot_counters.gpu_present_region_count
            << ",\"gpu_swapchain_copies\":"
            << frame.render_hotspot_counters.gpu_swapchain_copy_count << "}}";

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

Result<TraceCapture> parse_frame_diagnostics_trace_json(std::string_view json) {
    TraceCaptureJsonParser parser(json);
    return parser.parse_document();
}

Result<TraceCapture> load_frame_diagnostics_trace_json_file(std::string_view path) {
    auto contents = load_text_file(path, "frame diagnostics trace");
    if (!contents) {
        return Unexpected(contents.error());
    }
    return parse_frame_diagnostics_trace_json(*contents);
}

Result<void> save_frame_diagnostics_trace_json_file(const TraceCapture& capture,
                                                    std::string_view path) {
    std::ostringstream out;
    out << "{\n  \"format\": \"" << frame_diagnostics_trace_export_format()
        << "\",\n  \"traceEvents\": [\n";
    bool first_event = true;
    for (const auto& event : capture.events) {
        append_trace_event_header(out, first_event);
        out << "    {\"name\":\"" << escape_json_string(event.name) << "\",\"cat\":\""
            << escape_json_string(event.category) << "\",\"ph\":\"X\",\"ts\":"
            << static_cast<long long>(std::llround(event.timestamp_ms * 1000.0))
            << ",\"dur\":" << static_cast<long long>(std::llround(event.duration_ms * 1000.0))
            << ",\"pid\":1,\"tid\":2";
        if (!event.detail.empty() || !event.source_label.empty()) {
            out << ",\"args\":{";
            bool first_arg = true;
            if (!event.detail.empty()) {
                out << "\"detail\":\"" << escape_json_string(event.detail) << "\"";
                first_arg = false;
            }
            if (!event.source_label.empty()) {
                if (!first_arg) {
                    out << ",";
                }
                out << "\"source_label\":\"" << escape_json_string(event.source_label) << "\"";
            }
            out << "}";
        }
        out << "}";
    }
    out << "\n  ]\n}\n";
    return save_text_file(path, out.str(), "frame diagnostics trace");
}

RenderSnapshotNode build_render_snapshot(const RenderNode& root) {
    return build_render_snapshot_node(root);
}

} // namespace nk
