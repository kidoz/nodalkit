#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <nk/debug/diagnostics.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

struct TraceSummary {
    std::size_t event_count = 0;
    std::size_t frame_event_count = 0;
    double max_frame_ms = 0.0;
    std::size_t over_budget_frame_count = 0;
    std::size_t slow_frame_count = 0;
    std::size_t very_slow_frame_count = 0;
    std::size_t posted_task_count = 0;
    std::size_t timeout_count = 0;
    std::size_t interval_count = 0;
    std::size_t idle_count = 0;
    std::size_t unlabeled_runtime_event_count = 0;
    std::size_t unique_runtime_source_count = 0;
};

struct Thresholds {
    double max_frame_ms_regression = 0.0;
    std::size_t max_over_budget_frame_regression = 0;
    std::size_t max_slow_frame_regression = 0;
    std::size_t max_very_slow_frame_regression = 0;
    std::size_t max_posted_task_regression = 0;
    std::size_t max_timeout_regression = 0;
    std::size_t max_interval_regression = 0;
    std::size_t max_idle_regression = 0;
    std::size_t max_unlabeled_runtime_regression = 0;
};

bool is_runtime_category(std::string_view category) {
    return category == "event-loop-task" || category == "event-loop-timer" ||
           category == "event-loop-idle";
}

TraceSummary summarize(const nk::TraceCapture& trace) {
    TraceSummary summary;
    summary.event_count = trace.events.size();
    std::unordered_set<std::string> runtime_sources;

    for (const auto& event : trace.events) {
        if (event.name == "frame") {
            ++summary.frame_event_count;
            summary.max_frame_ms = std::max(summary.max_frame_ms, event.duration_ms);
            switch (nk::classify_frame_time(event.duration_ms)) {
            case nk::FramePerformanceMarker::WithinBudget:
                break;
            case nk::FramePerformanceMarker::OverBudget:
                ++summary.over_budget_frame_count;
                break;
            case nk::FramePerformanceMarker::Slow:
                ++summary.slow_frame_count;
                break;
            case nk::FramePerformanceMarker::VerySlow:
                ++summary.very_slow_frame_count;
                break;
            }
        } else if (event.name == "posted-task") {
            ++summary.posted_task_count;
        } else if (event.name == "timeout") {
            ++summary.timeout_count;
        } else if (event.name == "interval") {
            ++summary.interval_count;
        } else if (event.name == "idle") {
            ++summary.idle_count;
        }

        if (is_runtime_category(event.category)) {
            if (event.source_label.empty()) {
                ++summary.unlabeled_runtime_event_count;
            } else {
                runtime_sources.insert(event.source_label);
            }
        }
    }

    summary.unique_runtime_source_count = runtime_sources.size();
    return summary;
}

void print_summary(std::string_view label, const TraceSummary& summary) {
    std::cout << label << ":\n";
    std::cout << "  events: " << summary.event_count << "\n";
    std::cout << "  frames: " << summary.frame_event_count << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  max frame ms: " << summary.max_frame_ms << "\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "  over budget frames: " << summary.over_budget_frame_count << "\n";
    std::cout << "  slow frames: " << summary.slow_frame_count << "\n";
    std::cout << "  very slow frames: " << summary.very_slow_frame_count << "\n";
    std::cout << "  posted tasks: " << summary.posted_task_count << "\n";
    std::cout << "  timeouts: " << summary.timeout_count << "\n";
    std::cout << "  intervals: " << summary.interval_count << "\n";
    std::cout << "  idles: " << summary.idle_count << "\n";
    std::cout << "  unlabeled runtime events: " << summary.unlabeled_runtime_event_count << "\n";
    std::cout << "  runtime sources: " << summary.unique_runtime_source_count << "\n";
}

template <typename T>
void check_metric(
    std::string_view label, T baseline, T candidate, T allowed_regression, bool& failed) {
    if (candidate <= baseline + allowed_regression) {
        return;
    }

    std::cout << "regression: " << label << " baseline=" << baseline << " candidate=" << candidate
              << " allowed=" << allowed_regression << "\n";
    failed = true;
}

std::optional<double> parse_double_flag(std::string_view value) {
    try {
        return std::stod(std::string(value));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::size_t> parse_size_t_flag(std::string_view value) {
    try {
        return static_cast<std::size_t>(std::stoull(std::string(value)));
    } catch (...) {
        return std::nullopt;
    }
}

bool parse_threshold_arg(std::string_view arg, Thresholds& thresholds) {
    const auto parse_size_value = [&](std::string_view prefix, std::size_t& target) -> bool {
        if (!arg.starts_with(prefix)) {
            return false;
        }
        const auto parsed = parse_size_t_flag(arg.substr(prefix.size()));
        if (!parsed) {
            return false;
        }
        target = *parsed;
        return true;
    };

    if (arg.starts_with("--max-frame-ms-regression=")) {
        const auto parsed =
            parse_double_flag(arg.substr(std::string_view("--max-frame-ms-regression=").size()));
        if (!parsed) {
            return false;
        }
        thresholds.max_frame_ms_regression = *parsed;
        return true;
    }

    return parse_size_value("--max-over-budget-frame-regression=",
                            thresholds.max_over_budget_frame_regression) ||
           parse_size_value("--max-slow-frame-regression=", thresholds.max_slow_frame_regression) ||
           parse_size_value("--max-very-slow-frame-regression=",
                            thresholds.max_very_slow_frame_regression) ||
           parse_size_value("--max-posted-task-regression=",
                            thresholds.max_posted_task_regression) ||
           parse_size_value("--max-timeout-regression=", thresholds.max_timeout_regression) ||
           parse_size_value("--max-interval-regression=", thresholds.max_interval_regression) ||
           parse_size_value("--max-idle-regression=", thresholds.max_idle_regression) ||
           parse_size_value("--max-unlabeled-runtime-regression=",
                            thresholds.max_unlabeled_runtime_regression);
}

void print_usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <baseline-trace.json> <candidate-trace.json> "
              << "[threshold flags]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argc > 0 ? argv[0] : "nk_trace_diff");
        return 2;
    }

    Thresholds thresholds;
    for (int index = 3; index < argc; ++index) {
        if (!parse_threshold_arg(argv[index], thresholds)) {
            std::cerr << "invalid threshold argument: " << argv[index] << "\n";
            return 2;
        }
    }

    const auto baseline = nk::load_frame_diagnostics_trace_json_file(argv[1]);
    if (!baseline) {
        std::cerr << "failed to load baseline trace: " << baseline.error() << "\n";
        return 2;
    }

    const auto candidate = nk::load_frame_diagnostics_trace_json_file(argv[2]);
    if (!candidate) {
        std::cerr << "failed to load candidate trace: " << candidate.error() << "\n";
        return 2;
    }

    const auto baseline_summary = summarize(*baseline);
    const auto candidate_summary = summarize(*candidate);

    print_summary("baseline", baseline_summary);
    print_summary("candidate", candidate_summary);

    bool failed = false;
    check_metric("max frame ms",
                 baseline_summary.max_frame_ms,
                 candidate_summary.max_frame_ms,
                 thresholds.max_frame_ms_regression,
                 failed);
    check_metric("over budget frames",
                 baseline_summary.over_budget_frame_count,
                 candidate_summary.over_budget_frame_count,
                 thresholds.max_over_budget_frame_regression,
                 failed);
    check_metric("slow frames",
                 baseline_summary.slow_frame_count,
                 candidate_summary.slow_frame_count,
                 thresholds.max_slow_frame_regression,
                 failed);
    check_metric("very slow frames",
                 baseline_summary.very_slow_frame_count,
                 candidate_summary.very_slow_frame_count,
                 thresholds.max_very_slow_frame_regression,
                 failed);
    check_metric("posted tasks",
                 baseline_summary.posted_task_count,
                 candidate_summary.posted_task_count,
                 thresholds.max_posted_task_regression,
                 failed);
    check_metric("timeouts",
                 baseline_summary.timeout_count,
                 candidate_summary.timeout_count,
                 thresholds.max_timeout_regression,
                 failed);
    check_metric("intervals",
                 baseline_summary.interval_count,
                 candidate_summary.interval_count,
                 thresholds.max_interval_regression,
                 failed);
    check_metric("idles",
                 baseline_summary.idle_count,
                 candidate_summary.idle_count,
                 thresholds.max_idle_regression,
                 failed);
    check_metric("unlabeled runtime events",
                 baseline_summary.unlabeled_runtime_event_count,
                 candidate_summary.unlabeled_runtime_event_count,
                 thresholds.max_unlabeled_runtime_regression,
                 failed);

    if (failed) {
        return 1;
    }

    std::cout << "no regressions detected\n";
    return 0;
}
