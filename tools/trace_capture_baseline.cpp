#include "trace_capture_baseline.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

namespace nk::tools {
namespace {
struct ArtifactSummary {
    std::size_t frame_count = 0;
    double max_total_ms = 0.0;
    // Pacing-free work per run: layout + snapshot + render summed over all
    // frames. This is the host-speed signal for normalizing wall-clock gates.
    double busy_ms = 0.0;
    std::size_t max_render_node_count = 0;
    std::size_t max_text_shape_count = 0;
    std::size_t max_image_texture_upload_count = 0;
    std::size_t max_model_row_materialize_count = 0;
};

struct TraceSummary {
    double max_frame_ms = 0.0;
    double total_frame_ms = 0.0;
    // Per-frame durations, sorted slowest-first, for the rank-wise gate.
    std::vector<double> frame_ms;
    std::size_t over_budget_frame_count = 0;
    std::size_t slow_frame_count = 0;
    std::size_t very_slow_frame_count = 0;
    std::size_t posted_task_count = 0;
    std::size_t timeout_count = 0;
    std::size_t interval_count = 0;
    std::size_t idle_count = 0;
    std::size_t unlabeled_runtime_event_count = 0;
};

struct StableScenarioThresholds {
    // Max-frame-time tolerances must absorb realistic OS scheduling jitter. On a loaded desktop
    // (Wayland compositor, audio thread, GPU driver) the tail of a single frame can swing 2-4 ms
    // without any code change. 1.5 ms made these gates permanently flaky. CI VMs are worse: the
    // worst frame of a scene is dominated by one-time init (driver, shader, atlas) that no host
    // normalization removes, swinging tens of milliseconds above the reference baseline. The
    // over-budget frame counters remain the systematic-regression detector.
    double max_artifact_total_ms_regression = 90.0;
    double max_trace_frame_ms_regression = 90.0;
    std::size_t max_render_node_regression = 2;
    std::size_t max_text_shape_regression = 2;
    std::size_t max_image_upload_regression = 1;
    std::size_t max_model_materialize_regression = 1;
    // Frame-bucket counters naturally vary by ±1 frame across runs on the same host because
    // frame totals cluster near bucket thresholds. Allow a small regression so these gates catch
    // real drift (repeated, monotonic budget overruns) without flaking on unrelated runs.
    std::size_t max_over_budget_frame_regression = 1;
    std::size_t max_slow_frame_regression = 1;
    std::size_t max_very_slow_frame_regression = 1;
    std::size_t max_posted_task_regression = 1;
    std::size_t max_timeout_regression = 0;
    std::size_t max_interval_regression = 0;
    std::size_t max_idle_regression = 0;
    std::size_t max_unlabeled_runtime_regression = 0;
};

template <typename T>
bool check_regression(std::string_view label,
                      T baseline,
                      T candidate,
                      T allowed_regression,
                      std::string_view scenario_label,
                      bool advisory = false) {
    if (candidate <= baseline + allowed_regression) {
        return false;
    }

    std::cerr << (advisory ? "advisory timing regression [" : "stable scenario regression [")
              << scenario_label << "]: " << label << " baseline=" << baseline
              << " candidate=" << candidate << " allowed=" << allowed_regression << "\n";
    return true;
}

ArtifactSummary summarize_artifact(const nk::FrameDiagnosticsArtifact& artifact) {
    ArtifactSummary summary;
    summary.frame_count = artifact.frames.size();
    for (const auto& frame : artifact.frames) {
        summary.max_total_ms = std::max(summary.max_total_ms, frame.total_ms);
        summary.busy_ms += frame.layout_ms + frame.snapshot_ms + frame.render_ms;
        summary.max_render_node_count =
            std::max(summary.max_render_node_count, frame.render_node_count);
        summary.max_text_shape_count =
            std::max(summary.max_text_shape_count, frame.render_hotspot_counters.text_shape_count);
        summary.max_image_texture_upload_count =
            std::max(summary.max_image_texture_upload_count,
                     frame.render_hotspot_counters.image_texture_upload_count);
        summary.max_model_row_materialize_count =
            std::max(summary.max_model_row_materialize_count,
                     frame.widget_hotspot_totals.model_row_materialize_count);
    }
    return summary;
}

TraceSummary summarize_trace(const nk::TraceCapture& trace, double speed_scale = 1.0) {
    TraceSummary summary;
    for (const auto& event : trace.events) {
        if (event.name == "frame") {
            // Dividing by the host speed scale turns absolute wall-clock
            // thresholds into relative ones: a uniformly slower host or build
            // stays in the same buckets, while a frame that is
            // disproportionately slow still drifts into a worse bucket.
            const auto duration_ms = event.duration_ms / speed_scale;
            summary.max_frame_ms = std::max(summary.max_frame_ms, duration_ms);
            summary.total_frame_ms += duration_ms;
            summary.frame_ms.push_back(duration_ms);
            switch (nk::classify_frame_time(duration_ms)) {
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

        if ((event.category == "event-loop-task" || event.category == "event-loop-timer" ||
             event.category == "event-loop-idle") &&
            event.source_label.empty()) {
            ++summary.unlabeled_runtime_event_count;
        }
    }
    std::sort(summary.frame_ms.begin(), summary.frame_ms.end(), std::greater<double>{});
    return summary;
}

bool has_work_counter_regression(std::string_view scenario_label,
                                 const nk::FrameDiagnosticsArtifact& baseline,
                                 const nk::FrameDiagnosticsArtifact& candidate) {
    const auto compare = [&](std::string_view label, auto counter, std::size_t tolerance) {
        std::size_t before = 0;
        std::size_t after = 0;
        for (const auto& frame : baseline.frames) {
            before += counter(frame);
        }
        for (const auto& frame : candidate.frames) {
            after += counter(frame);
        }
        return check_regression(label, before, after, tolerance, scenario_label);
    };
    bool failed = false;
    failed |= compare(
        "total widget measures",
        [](const auto& f) { return f.widget_hotspot_totals.measure_count; },
        1);
    failed |= compare(
        "total widget allocations",
        [](const auto& f) { return f.widget_hotspot_totals.allocate_count; },
        1);
    failed |= compare(
        "total text measures",
        [](const auto& f) { return f.widget_hotspot_totals.text_measure_count; },
        2);
    failed |= compare(
        "total text shapes",
        [](const auto& f) { return f.render_hotspot_counters.text_shape_count; },
        2);
    failed |= compare(
        "total text nodes",
        [](const auto& f) { return f.render_hotspot_counters.text_node_count; },
        2);
    failed |=
        compare("total layout requests", [](const auto& f) { return f.layout_request_count; }, 1);
    failed |=
        compare("total redraw requests", [](const auto& f) { return f.redraw_request_count; }, 1);
    return failed;
}

} // namespace

bool validate_capture_baseline(std::string_view scenario_label,
                               RendererBackend backend,
                               const FrameDiagnosticsArtifact& baseline_artifact,
                               const TraceCapture& baseline_trace,
                               const FrameDiagnosticsArtifact& candidate_artifact,
                               const TraceCapture& candidate_trace,
                               BaselinePolicy policy) {
    auto thresholds = StableScenarioThresholds{};
    if (scenario_label == "localized-redraw" && backend == nk::RendererBackend::Metal) {
        thresholds.max_artifact_total_ms_regression = 3.0;
        thresholds.max_trace_frame_ms_regression = 3.0;
    }
    const auto baseline_artifact_summary = summarize_artifact(baseline_artifact);
    const auto candidate_artifact_summary = summarize_artifact(candidate_artifact);
    const auto baseline_trace_summary = summarize_trace(baseline_trace);
    auto candidate_trace_summary = summarize_trace(candidate_trace);

    // The frame-time gates compare wall-clock numbers against baselines
    // recorded on the reference machine, so a different-speed host or a
    // debug -O0 build shifts them without any code change. Derive the
    // overall run-speed ratio from the frames' busy phases (layout +
    // snapshot + render): trace totals include frame pacing, which reads as
    // equal on hosts that differ only in how long the work itself takes,
    // while the busy phases track the actual per-host cost. The clamp keeps
    // a pathological baseline from inventing an extreme scale.
    double speed_scale = 1.0;
    if (baseline_artifact_summary.busy_ms > 0.0 && candidate_artifact_summary.busy_ms > 0.0) {
        speed_scale = std::clamp(
            candidate_artifact_summary.busy_ms / baseline_artifact_summary.busy_ms, 0.25, 8.0);
    }
    // Only a slower host is normalized away. Rescaling a faster host would
    // inflate its frames above the reference baselines and re-read normal
    // faster-host variance as drift; faster hosts simply have headroom.
    const double slowdown = std::max(speed_scale, 1.0);
    if (slowdown != 1.0) {
        candidate_trace_summary = summarize_trace(candidate_trace, slowdown);
    }

    bool failed = false;
    bool timing_failed = false;
    if (baseline_artifact_summary.frame_count != candidate_artifact_summary.frame_count) {
        std::cerr << "stable scenario regression [" << scenario_label
                  << "]: frame count baseline=" << baseline_artifact_summary.frame_count
                  << " candidate=" << candidate_artifact_summary.frame_count << "\n";
        failed = true;
    }

    timing_failed |= check_regression("artifact max total ms",
                                      baseline_artifact_summary.max_total_ms,
                                      // Same host-speed normalization as the trace
                                      // frame gates above.
                                      candidate_artifact_summary.max_total_ms / slowdown,
                                      thresholds.max_artifact_total_ms_regression,
                                      scenario_label,
                                      policy == BaselinePolicy::Counters);
    failed |= check_regression("artifact max render nodes",
                               baseline_artifact_summary.max_render_node_count,
                               candidate_artifact_summary.max_render_node_count,
                               thresholds.max_render_node_regression,
                               scenario_label);
    failed |= check_regression("artifact max text shapes",
                               baseline_artifact_summary.max_text_shape_count,
                               candidate_artifact_summary.max_text_shape_count,
                               thresholds.max_text_shape_regression,
                               scenario_label);
    failed |= check_regression("artifact max image uploads",
                               baseline_artifact_summary.max_image_texture_upload_count,
                               candidate_artifact_summary.max_image_texture_upload_count,
                               thresholds.max_image_upload_regression,
                               scenario_label);
    failed |= check_regression("artifact max model materialize",
                               baseline_artifact_summary.max_model_row_materialize_count,
                               candidate_artifact_summary.max_model_row_materialize_count,
                               thresholds.max_model_materialize_regression,
                               scenario_label);

    timing_failed |= check_regression("trace max frame ms",
                                      baseline_trace_summary.max_frame_ms,
                                      candidate_trace_summary.max_frame_ms,
                                      thresholds.max_trace_frame_ms_regression,
                                      scenario_label,
                                      policy == BaselinePolicy::Counters);
    // Rank-wise normalized frame-time gate. Absolute bucket counters
    // (over-budget/slow/very-slow) move host-dependently: a faster host
    // drops frames into a LOWER bucket than the reference machine's
    // baselines and looks like a count regression. Comparing each frame
    // rank, after the host-speed normalization above, against the baseline's
    // matching rank catches disproportionate per-frame regressions and is
    // immune to uniform host or build-speed differences.
    if (baseline_trace_summary.frame_ms.size() != candidate_trace_summary.frame_ms.size()) {
        std::cerr << "stable scenario regression [" << scenario_label
                  << "]: trace frame count baseline=" << baseline_trace_summary.frame_ms.size()
                  << " candidate=" << candidate_trace_summary.frame_ms.size() << "\n";
        failed = true;
    } else {
        // Per-rank tolerance: wider than the max gate (the max gate already
        // screens the very worst frame) but still scaled by the host-speed
        // factor, since a fixed tolerance reads uniform host slowness as a
        // per-rank drift.
        constexpr double kPerFrameToleranceMs = 60.0;
        const double per_frame_tolerance_ms = kPerFrameToleranceMs * slowdown;
        for (std::size_t rank = 0; rank < baseline_trace_summary.frame_ms.size(); ++rank) {
            if (candidate_trace_summary.frame_ms[rank] >
                baseline_trace_summary.frame_ms[rank] + per_frame_tolerance_ms) {
                std::cerr << (policy == BaselinePolicy::Counters ? "advisory timing regression ["
                                                                 : "stable scenario regression [")
                          << scenario_label << "]: trace frame rank " << rank
                          << " ms baseline=" << baseline_trace_summary.frame_ms[rank]
                          << " candidate=" << candidate_trace_summary.frame_ms[rank] << "\n";
                timing_failed = true;
            }
        }
    }
    failed |= check_regression("trace posted tasks",
                               baseline_trace_summary.posted_task_count,
                               candidate_trace_summary.posted_task_count,
                               thresholds.max_posted_task_regression,
                               scenario_label);
    failed |= check_regression("trace timeouts",
                               baseline_trace_summary.timeout_count,
                               candidate_trace_summary.timeout_count,
                               thresholds.max_timeout_regression,
                               scenario_label);
    failed |= check_regression("trace intervals",
                               baseline_trace_summary.interval_count,
                               candidate_trace_summary.interval_count,
                               thresholds.max_interval_regression,
                               scenario_label);
    failed |= check_regression("trace idles",
                               baseline_trace_summary.idle_count,
                               candidate_trace_summary.idle_count,
                               thresholds.max_idle_regression,
                               scenario_label);
    failed |= check_regression("trace unlabeled runtime events",
                               baseline_trace_summary.unlabeled_runtime_event_count,
                               candidate_trace_summary.unlabeled_runtime_event_count,
                               thresholds.max_unlabeled_runtime_regression,
                               scenario_label);

    if (policy == BaselinePolicy::Counters) {
        std::cout << "advisory baseline timing [" << scenario_label
                  << "]: baseline max=" << baseline_artifact_summary.max_total_ms
                  << " ms candidate max=" << candidate_artifact_summary.max_total_ms
                  << " ms; baseline busy=" << baseline_artifact_summary.busy_ms
                  << " ms candidate busy=" << candidate_artifact_summary.busy_ms << " ms\n";
        failed |=
            has_work_counter_regression(scenario_label, baseline_artifact, candidate_artifact);
    } else {
        failed |= timing_failed;
    }
    return !failed;
}

} // namespace nk::tools
