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
    std::string_view trace_path;
    bool summary_only = false;
    bool slow_only = false;
    std::string reason_filter;
    std::string label_filter;
};

std::string lowercase(std::string_view text) {
    std::string value(text);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_lowered(std::string_view haystack, std::string_view needle_lower) {
    if (needle_lower.empty()) {
        return true;
    }
    return lowercase(haystack).find(needle_lower) != std::string::npos;
}

bool is_slow_frame(const nk::TraceEvent& event) {
    return event.category == "frame" &&
           nk::classify_frame_time(event.duration_ms) != nk::FramePerformanceMarker::WithinBudget;
}

int usage(const char* argv0) {
    std::cerr
        << "usage: " << (argv0 != nullptr ? argv0 : "nk_trace_view")
        << " <trace.json> [--summary-only] [--slow-only] [--reason=<name>] [--label=<text>]\n";
    return 2;
}

bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2) {
        return false;
    }

    options.trace_path = argv[1];
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
        if (arg.starts_with("--label=")) {
            options.label_filter = lowercase(arg.substr(std::string_view("--label=").size()));
            continue;
        }
        return false;
    }
    return true;
}

bool matches_filters(const nk::TraceEvent& event, const Options& options) {
    if (options.slow_only && !is_slow_frame(event)) {
        return false;
    }
    if (!options.reason_filter.empty() && !contains_lowered(event.name, options.reason_filter)) {
        return false;
    }
    if (!options.label_filter.empty() &&
        !contains_lowered(event.source_label, options.label_filter)) {
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return usage(argc > 0 ? argv[0] : "nk_trace_view");
    }

    const auto capture = nk::load_frame_diagnostics_trace_json_file(options.trace_path);
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
    std::vector<const nk::TraceEvent*> filtered_events;

    for (const auto& event : capture->events) {
        if (event.category == "frame") {
            ++frame_event_count;
            slowest_frame_ms = std::max(slowest_frame_ms, event.duration_ms);
        } else {
            ++runtime_event_count;
            ++runtime_event_name_counts[event.name];
            if (event.source_label.empty()) {
                ++unlabeled_runtime_event_count;
            } else {
                ++runtime_event_source_counts[event.source_label];
            }
        }

        if (matches_filters(event, options)) {
            filtered_events.push_back(&event);
        }
    }

    std::cout << "format: " << nk::frame_diagnostics_trace_export_format() << "\n";
    std::cout << "events: " << capture->events.size() << "\n";
    std::cout << "frames: " << frame_event_count << "\n";
    std::cout << "runtime events: " << runtime_event_count << "\n";
    std::cout << "unlabeled runtime events: " << unlabeled_runtime_event_count << "\n";
    std::cout << "slowest frame ms: " << std::fixed << std::setprecision(2) << slowest_frame_ms
              << "\n";
    std::cout << "filtered events: " << filtered_events.size() << "\n";
    if (options.slow_only) {
        std::cout << "filter slow only: yes\n";
    }
    if (!options.reason_filter.empty()) {
        std::cout << "filter reason: " << options.reason_filter << "\n";
    }
    if (!options.label_filter.empty()) {
        std::cout << "filter label: " << options.label_filter << "\n";
    }

    std::cout << "runtime event counts:\n";
    for (const auto& [name, count] : runtime_event_name_counts) {
        std::cout << "  " << name << ": " << count << "\n";
    }

    if (options.summary_only) {
        return 0;
    }

    std::cout << "runtime source labels:\n";
    for (const auto& [label, count] : runtime_event_source_counts) {
        std::cout << "  " << label << ": " << count << "\n";
    }

    std::cout << "filtered events:\n";
    std::size_t frame_index = 0;
    for (const auto* event : filtered_events) {
        if (event->category == "frame") {
            std::cout << "  frame #" << frame_index++ << " " << std::fixed << std::setprecision(2)
                      << event->duration_ms << " ms\n";
            continue;
        }
        std::cout << "  " << event->category << ":" << event->name;
        if (!event->source_label.empty()) {
            std::cout << " [" << event->source_label << "]";
        }
        if (!event->detail.empty()) {
            std::cout << " " << event->detail;
        }
        std::cout << "\n";
    }

    return 0;
}
