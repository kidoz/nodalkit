#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void append_render_snapshot_file_json(std::ostringstream& out, const RenderSnapshotNode& node) {
    out << "{\"format\":\"nk-render-snapshot-v1\",\"root\":";
    append_render_snapshot_json(out, node);
    out << "}\n";
}

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
            if (!format.empty() && format != "nk-render-snapshot-v1") {
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
    const auto file_path = std::filesystem::path(std::string(path));
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return Unexpected("failed to create render snapshot file: " + file_path.string());
    }

    std::ostringstream buffer;
    append_render_snapshot_file_json(buffer, root);
    out << buffer.str();
    if (!out.good()) {
        return Unexpected("failed to write render snapshot file: " + file_path.string());
    }
    return {};
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
            << ",\"queue_delay_ms\":" << frame.queue_delay_ms
            << ",\"present_path\":\""
            << escape_json_string(gpu_present_path_name(
                   frame.render_hotspot_counters.gpu_present_path))
            << "\",\"present_tradeoff\":\""
            << escape_json_string(gpu_present_tradeoff_name(
                   frame.render_hotspot_counters.gpu_present_tradeoff))
            << "\",\"gpu_draw_calls\":"
            << frame.render_hotspot_counters.gpu_draw_call_count
            << ",\"gpu_viewport_pixels\":"
            << frame.render_hotspot_counters.gpu_viewport_pixel_count
            << ",\"gpu_draw_pixels\":"
            << frame.render_hotspot_counters.gpu_estimated_draw_pixel_count
            << ",\"gpu_present_regions\":"
            << frame.render_hotspot_counters.gpu_present_region_count
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

RenderSnapshotNode build_render_snapshot(const RenderNode& root) {
    return build_render_snapshot_node(root);
}

} // namespace nk
