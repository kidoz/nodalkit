#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <nk/debug/diagnostics.h>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct ArtifactSummary {
    std::size_t frame_count = 0;
    double max_total_ms = 0.0;
    std::size_t max_render_node_count = 0;
    std::size_t max_redraw_request_count = 0;
    std::size_t max_layout_request_count = 0;
    std::size_t max_text_shape_count = 0;
    std::size_t max_image_texture_upload_count = 0;
    std::size_t max_model_row_materialize_count = 0;
};

struct Thresholds {
    double max_total_ms_regression = 0.0;
    double max_frame_total_ms_regression = 0.0;
    double max_frame_layout_ms_regression = 0.0;
    double max_frame_snapshot_ms_regression = 0.0;
    double max_frame_render_ms_regression = 0.0;
    double max_frame_present_ms_regression = 0.0;
    std::size_t max_render_node_regression = 0;
    std::size_t max_redraw_request_regression = 0;
    std::size_t max_layout_request_regression = 0;
    std::size_t max_text_shape_regression = 0;
    std::size_t max_image_upload_regression = 0;
    std::size_t max_model_materialize_regression = 0;
    std::size_t max_frame_redraw_request_regression = 0;
    std::size_t max_frame_layout_request_regression = 0;
};

ArtifactSummary summarize(const nk::FrameDiagnosticsArtifact& artifact) {
    ArtifactSummary summary;
    summary.frame_count = artifact.frames.size();
    for (const auto& frame : artifact.frames) {
        summary.max_total_ms = std::max(summary.max_total_ms, frame.total_ms);
        summary.max_render_node_count =
            std::max(summary.max_render_node_count, frame.render_node_count);
        summary.max_redraw_request_count =
            std::max(summary.max_redraw_request_count, frame.redraw_request_count);
        summary.max_layout_request_count =
            std::max(summary.max_layout_request_count, frame.layout_request_count);
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

void print_summary(std::string_view label, const ArtifactSummary& summary) {
    std::cout << label << ":\n";
    std::cout << "  frames: " << summary.frame_count << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  max total ms: " << summary.max_total_ms << "\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "  max render nodes: " << summary.max_render_node_count << "\n";
    std::cout << "  max redraw requests: " << summary.max_redraw_request_count << "\n";
    std::cout << "  max layout requests: " << summary.max_layout_request_count << "\n";
    std::cout << "  max text shapes: " << summary.max_text_shape_count << "\n";
    std::cout << "  max image uploads: " << summary.max_image_texture_upload_count << "\n";
    std::cout << "  max model materialize: " << summary.max_model_row_materialize_count << "\n";
}

template <typename T>
bool check_metric(
    std::string_view label, T baseline, T candidate, T allowed_regression, bool& failed) {
    if (candidate <= baseline + allowed_regression) {
        return false;
    }

    std::cout << "regression: " << label << " baseline=" << baseline << " candidate=" << candidate
              << " allowed=" << allowed_regression << "\n";
    failed = true;
    return true;
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
        const auto parsed = std::stoull(std::string(value));
        return static_cast<std::size_t>(parsed);
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
        if (!parsed.has_value()) {
            return false;
        }
        target = *parsed;
        return true;
    };

    if (arg.starts_with("--max-total-ms-regression=")) {
        const auto parsed =
            parse_double_flag(arg.substr(std::string_view("--max-total-ms-regression=").size()));
        if (!parsed.has_value()) {
            return false;
        }
        thresholds.max_total_ms_regression = *parsed;
        return true;
    }

    const auto parse_double_value = [&](std::string_view prefix, double& target) -> bool {
        if (!arg.starts_with(prefix)) {
            return false;
        }
        const auto parsed = parse_double_flag(arg.substr(prefix.size()));
        if (!parsed.has_value()) {
            return false;
        }
        target = *parsed;
        return true;
    };

    if (parse_double_value("--max-frame-total-ms-regression=",
                           thresholds.max_frame_total_ms_regression) ||
        parse_double_value("--max-frame-layout-ms-regression=",
                           thresholds.max_frame_layout_ms_regression) ||
        parse_double_value("--max-frame-snapshot-ms-regression=",
                           thresholds.max_frame_snapshot_ms_regression) ||
        parse_double_value("--max-frame-render-ms-regression=",
                           thresholds.max_frame_render_ms_regression) ||
        parse_double_value("--max-frame-present-ms-regression=",
                           thresholds.max_frame_present_ms_regression)) {
        return true;
    }

    return parse_size_value("--max-render-node-regression=",
                            thresholds.max_render_node_regression) ||
           parse_size_value("--max-redraw-request-regression=",
                            thresholds.max_redraw_request_regression) ||
           parse_size_value("--max-layout-request-regression=",
                            thresholds.max_layout_request_regression) ||
           parse_size_value("--max-text-shape-regression=", thresholds.max_text_shape_regression) ||
           parse_size_value("--max-image-upload-regression=",
                            thresholds.max_image_upload_regression) ||
           parse_size_value("--max-model-materialize-regression=",
                            thresholds.max_model_materialize_regression) ||
           parse_size_value("--max-frame-redraw-request-regression=",
                            thresholds.max_frame_redraw_request_regression) ||
           parse_size_value("--max-frame-layout-request-regression=",
                            thresholds.max_frame_layout_request_regression);
}

void print_usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <baseline.json> <candidate.json> [threshold flags]\n";
}

void check_frame_count(const ArtifactSummary& baseline_summary,
                       const ArtifactSummary& candidate_summary,
                       bool& failed) {
    if (baseline_summary.frame_count == candidate_summary.frame_count) {
        return;
    }

    std::cout << "regression: frame count baseline=" << baseline_summary.frame_count
              << " candidate=" << candidate_summary.frame_count << "\n";
    failed = true;
}

void check_frame_metric(std::string_view label,
                        double baseline,
                        double candidate,
                        double allowed_regression,
                        std::size_t index,
                        uint64_t baseline_frame_id,
                        uint64_t candidate_frame_id,
                        bool& failed) {
    if (candidate <= baseline + allowed_regression) {
        return;
    }

    std::cout << "regression: " << label << " frame[" << index << "] "
              << "baseline(frame_id=" << baseline_frame_id << ")=" << baseline << " "
              << "candidate(frame_id=" << candidate_frame_id << ")=" << candidate << " "
              << "allowed=" << allowed_regression << "\n";
    failed = true;
}

void check_frame_metric(std::string_view label,
                        std::size_t baseline,
                        std::size_t candidate,
                        std::size_t allowed_regression,
                        std::size_t index,
                        uint64_t baseline_frame_id,
                        uint64_t candidate_frame_id,
                        bool& failed) {
    if (candidate <= baseline + allowed_regression) {
        return;
    }

    std::cout << "regression: " << label << " frame[" << index << "] "
              << "baseline(frame_id=" << baseline_frame_id << ")=" << baseline << " "
              << "candidate(frame_id=" << candidate_frame_id << ")=" << candidate << " "
              << "allowed=" << allowed_regression << "\n";
    failed = true;
}

void check_frame_by_frame(const nk::FrameDiagnosticsArtifact& baseline,
                          const nk::FrameDiagnosticsArtifact& candidate,
                          const Thresholds& thresholds,
                          bool& failed) {
    const auto frame_count = std::min(baseline.frames.size(), candidate.frames.size());
    for (std::size_t index = 0; index < frame_count; ++index) {
        const auto& baseline_frame = baseline.frames[index];
        const auto& candidate_frame = candidate.frames[index];

        check_frame_metric("frame total ms",
                           baseline_frame.total_ms,
                           candidate_frame.total_ms,
                           thresholds.max_frame_total_ms_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
        check_frame_metric("frame layout ms",
                           baseline_frame.layout_ms,
                           candidate_frame.layout_ms,
                           thresholds.max_frame_layout_ms_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
        check_frame_metric("frame snapshot ms",
                           baseline_frame.snapshot_ms,
                           candidate_frame.snapshot_ms,
                           thresholds.max_frame_snapshot_ms_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
        check_frame_metric("frame render ms",
                           baseline_frame.render_ms,
                           candidate_frame.render_ms,
                           thresholds.max_frame_render_ms_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
        check_frame_metric("frame present ms",
                           baseline_frame.present_ms,
                           candidate_frame.present_ms,
                           thresholds.max_frame_present_ms_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
        check_frame_metric("frame redraw requests",
                           baseline_frame.redraw_request_count,
                           candidate_frame.redraw_request_count,
                           thresholds.max_frame_redraw_request_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
        check_frame_metric("frame layout requests",
                           baseline_frame.layout_request_count,
                           candidate_frame.layout_request_count,
                           thresholds.max_frame_layout_request_regression,
                           index,
                           baseline_frame.frame_id,
                           candidate_frame.frame_id,
                           failed);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argc > 0 ? argv[0] : "nk_frame_diag_diff");
        return 2;
    }

    Thresholds thresholds;
    for (int index = 3; index < argc; ++index) {
        if (!parse_threshold_arg(argv[index], thresholds)) {
            std::cerr << "invalid threshold argument: " << argv[index] << "\n";
            return 2;
        }
    }

    const auto baseline = nk::load_frame_diagnostics_artifact_json_file(argv[1]);
    if (!baseline) {
        std::cerr << "failed to load baseline: " << baseline.error() << "\n";
        return 2;
    }

    const auto candidate = nk::load_frame_diagnostics_artifact_json_file(argv[2]);
    if (!candidate) {
        std::cerr << "failed to load candidate: " << candidate.error() << "\n";
        return 2;
    }

    const auto baseline_summary = summarize(*baseline);
    const auto candidate_summary = summarize(*candidate);

    print_summary("baseline", baseline_summary);
    print_summary("candidate", candidate_summary);

    bool failed = false;
    check_frame_count(baseline_summary, candidate_summary, failed);
    check_metric("max total ms",
                 baseline_summary.max_total_ms,
                 candidate_summary.max_total_ms,
                 thresholds.max_total_ms_regression,
                 failed);
    check_metric("max render nodes",
                 baseline_summary.max_render_node_count,
                 candidate_summary.max_render_node_count,
                 thresholds.max_render_node_regression,
                 failed);
    check_metric("max redraw requests",
                 baseline_summary.max_redraw_request_count,
                 candidate_summary.max_redraw_request_count,
                 thresholds.max_redraw_request_regression,
                 failed);
    check_metric("max layout requests",
                 baseline_summary.max_layout_request_count,
                 candidate_summary.max_layout_request_count,
                 thresholds.max_layout_request_regression,
                 failed);
    check_metric("max text shapes",
                 baseline_summary.max_text_shape_count,
                 candidate_summary.max_text_shape_count,
                 thresholds.max_text_shape_regression,
                 failed);
    check_metric("max image uploads",
                 baseline_summary.max_image_texture_upload_count,
                 candidate_summary.max_image_texture_upload_count,
                 thresholds.max_image_upload_regression,
                 failed);
    check_metric("max model materialize",
                 baseline_summary.max_model_row_materialize_count,
                 candidate_summary.max_model_row_materialize_count,
                 thresholds.max_model_materialize_regression,
                 failed);
    check_frame_by_frame(*baseline, *candidate, thresholds, failed);

    if (failed) {
        return 1;
    }

    std::cout << "no regressions detected\n";
    return 0;
}
