#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <nk/debug/diagnostics.h>
#include <string_view>

namespace {

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_trace_view")
              << " <trace.json> [--summary-only]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        return usage(argc > 0 ? argv[0] : "nk_trace_view");
    }

    const std::string_view trace_path = argv[1];
    const bool summary_only = argc == 3 && std::string_view(argv[2]) == "--summary-only";
    if (argc == 3 && !summary_only) {
        return usage(argv[0]);
    }

    const auto capture = nk::load_frame_diagnostics_trace_json_file(trace_path);
    if (!capture) {
        std::cerr << capture.error() << "\n";
        return 1;
    }

    std::size_t frame_event_count = 0;
    std::size_t runtime_event_count = 0;
    std::size_t unlabeled_runtime_event_count = 0;
    double slowest_frame_ms = 0.0;
    std::map<std::string, std::size_t, std::less<>> runtime_event_name_counts;
    std::map<std::string, std::size_t, std::less<>> runtime_event_source_counts;

    for (const auto& event : capture->events) {
        if (event.category == "frame") {
            ++frame_event_count;
            slowest_frame_ms = std::max(slowest_frame_ms, event.duration_ms);
            continue;
        }

        ++runtime_event_count;
        ++runtime_event_name_counts[event.name];
        if (event.source_label.empty()) {
            ++unlabeled_runtime_event_count;
        } else {
            ++runtime_event_source_counts[event.source_label];
        }
    }

    std::cout << "format: " << nk::frame_diagnostics_trace_export_format() << "\n";
    std::cout << "events: " << capture->events.size() << "\n";
    std::cout << "frames: " << frame_event_count << "\n";
    std::cout << "runtime events: " << runtime_event_count << "\n";
    std::cout << "unlabeled runtime events: " << unlabeled_runtime_event_count << "\n";
    std::cout << "slowest frame ms: " << std::fixed << std::setprecision(2) << slowest_frame_ms
              << "\n";

    std::cout << "runtime event counts:\n";
    for (const auto& [name, count] : runtime_event_name_counts) {
        std::cout << "  " << name << ": " << count << "\n";
    }

    if (summary_only) {
        return 0;
    }

    std::cout << "runtime source labels:\n";
    for (const auto& [label, count] : runtime_event_source_counts) {
        std::cout << "  " << label << ": " << count << "\n";
    }

    std::cout << "frame events:\n";
    std::size_t frame_index = 0;
    for (const auto& event : capture->events) {
        if (event.category != "frame") {
            continue;
        }
        std::cout << "  #" << frame_index++ << " " << std::fixed << std::setprecision(2)
                  << event.duration_ms << " ms\n";
    }

    return 0;
}
