#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <map>
#include <nk/debug/diagnostics.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string_view artifact_path;
    bool summary_only = false;
    bool slow_only = false;
    std::string reason_filter;
};

std::string lowercase(std::string_view text) {
    std::string value(text);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool matches_reason(const nk::FrameDiagnostics& frame, std::string_view reason_lower) {
    if (reason_lower.empty()) {
        return true;
    }
    for (const auto reason : frame.request_reasons) {
        if (lowercase(nk::frame_request_reason_name(reason)).find(reason_lower) !=
            std::string::npos) {
            return true;
        }
    }
    return false;
}

bool is_slow_frame(const nk::FrameDiagnostics& frame) {
    return frame.performance_marker != nk::FramePerformanceMarker::WithinBudget;
}

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_frame_diag_view")
              << " <frame_diagnostics.json> [--summary-only] [--slow-only] [--reason=<name>]\n";
    return 2;
}

bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2) {
        return false;
    }

    options.artifact_path = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--summary-only") {
            options.summary_only = true;
            continue;
        }
        if (arg == "--slow-only") {
            options.slow_only = true;
            continue;
        }
        if (arg.starts_with("--reason=")) {
            options.reason_filter = lowercase(arg.substr(std::string_view("--reason=").size()));
            continue;
        }
        return false;
    }
    return true;
}

bool matches_filters(const nk::FrameDiagnostics& frame, const Options& options) {
    if (options.slow_only && !is_slow_frame(frame)) {
        return false;
    }
    return matches_reason(frame, options.reason_filter);
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return usage(argc > 0 ? argv[0] : "nk_frame_diag_view");
    }

    const auto artifact = nk::load_frame_diagnostics_artifact_json_file(options.artifact_path);
    if (!artifact) {
        std::cerr << artifact.error() << "\n";
        return 1;
    }

    const auto histogram = nk::build_frame_time_histogram(artifact->frames);
    std::size_t max_widget_count = 0;
    std::size_t max_render_node_count = 0;
    std::size_t max_redraw_request_count = 0;
    std::size_t max_layout_request_count = 0;
    double slowest_frame_ms = 0.0;
    std::map<std::string, std::size_t, std::less<>> request_reason_counts;
    std::vector<const nk::FrameDiagnostics*> filtered_frames;

    for (const auto& frame : artifact->frames) {
        max_widget_count = std::max(max_widget_count, frame.widget_count);
        max_render_node_count = std::max(max_render_node_count, frame.render_node_count);
        max_redraw_request_count = std::max(max_redraw_request_count, frame.redraw_request_count);
        max_layout_request_count = std::max(max_layout_request_count, frame.layout_request_count);
        slowest_frame_ms = std::max(slowest_frame_ms, frame.total_ms);
        for (const auto reason : frame.request_reasons) {
            ++request_reason_counts[std::string(nk::frame_request_reason_name(reason))];
        }
        if (matches_filters(frame, options)) {
            filtered_frames.push_back(&frame);
        }
    }

    std::cout << "format: " << nk::frame_diagnostics_artifact_format() << "\n";
    std::cout << "frames: " << artifact->frames.size() << "\n";
    std::cout << "within budget: " << histogram.within_budget_count << "\n";
    std::cout << "over budget: " << histogram.over_budget_count << "\n";
    std::cout << "slow: " << histogram.slow_count << "\n";
    std::cout << "very slow: " << histogram.very_slow_count << "\n";
    std::cout << "slowest frame ms: " << std::fixed << std::setprecision(2) << slowest_frame_ms
              << "\n";
    std::cout << "max widget count: " << max_widget_count << "\n";
    std::cout << "max render node count: " << max_render_node_count << "\n";
    std::cout << "max redraw requests: " << max_redraw_request_count << "\n";
    std::cout << "max layout requests: " << max_layout_request_count << "\n";
    std::cout << "filtered frames: " << filtered_frames.size() << "\n";
    if (options.slow_only) {
        std::cout << "filter slow only: yes\n";
    }
    if (!options.reason_filter.empty()) {
        std::cout << "filter reason: " << options.reason_filter << "\n";
    }
    std::cout << "request reasons:\n";
    for (const auto& [reason, count] : request_reason_counts) {
        std::cout << "  " << reason << ": " << count << "\n";
    }

    if (options.summary_only) {
        return 0;
    }

    std::cout << "filtered frames:\n";
    for (const auto* frame : filtered_frames) {
        std::cout << "  #" << frame->frame_id << " " << std::fixed << std::setprecision(2)
                  << frame->total_ms << " ms"
                  << "  layout " << frame->layout_ms << "  snapshot " << frame->snapshot_ms
                  << "  render " << frame->render_ms << "  present " << frame->present_ms
                  << "  widgets " << frame->widget_count << "  nodes " << frame->render_node_count
                  << "\n";
    }

    return 0;
}
