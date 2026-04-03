#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <nk/debug/diagnostics.h>
#include <string_view>

namespace {

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_frame_diag_view")
              << " <frame_diagnostics.json> [--summary-only]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        return usage(argc > 0 ? argv[0] : "nk_frame_diag_view");
    }

    const std::string_view artifact_path = argv[1];
    const bool summary_only = argc == 3 && std::string_view(argv[2]) == "--summary-only";
    if (argc == 3 && !summary_only) {
        return usage(argv[0]);
    }

    const auto artifact = nk::load_frame_diagnostics_artifact_json_file(artifact_path);
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

    for (const auto& frame : artifact->frames) {
        max_widget_count = std::max(max_widget_count, frame.widget_count);
        max_render_node_count = std::max(max_render_node_count, frame.render_node_count);
        max_redraw_request_count = std::max(max_redraw_request_count, frame.redraw_request_count);
        max_layout_request_count = std::max(max_layout_request_count, frame.layout_request_count);
        slowest_frame_ms = std::max(slowest_frame_ms, frame.total_ms);
        for (const auto reason : frame.request_reasons) {
            ++request_reason_counts[std::string(nk::frame_request_reason_name(reason))];
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
    std::cout << "request reasons:\n";
    for (const auto& [reason, count] : request_reason_counts) {
        std::cout << "  " << reason << ": " << count << "\n";
    }

    if (summary_only) {
        return 0;
    }

    std::cout << "frames:\n";
    for (const auto& frame : artifact->frames) {
        std::cout << "  #" << frame.frame_id << " " << std::fixed << std::setprecision(2)
                  << frame.total_ms << " ms"
                  << "  layout " << frame.layout_ms << "  snapshot " << frame.snapshot_ms
                  << "  render " << frame.render_ms << "  present " << frame.present_ms
                  << "  widgets " << frame.widget_count << "  nodes " << frame.render_node_count
                  << "\n";
    }

    return 0;
}
