#pragma once

#include <nk/debug/diagnostics.h>
#include <nk/render/renderer.h>
#include <string_view>

namespace nk::tools {

/// Timed preserves the existing wall-clock gates. Counters gates reproducible work totals
/// and reports wall-clock comparisons as advisory.
enum class BaselinePolicy { Timed, Counters };

/// Returns true when all blocking comparisons pass.
bool validate_capture_baseline(std::string_view scenario_label,
                               RendererBackend backend,
                               const FrameDiagnosticsArtifact& baseline_artifact,
                               const TraceCapture& baseline_trace,
                               const FrameDiagnosticsArtifact& candidate_artifact,
                               const TraceCapture& candidate_trace,
                               BaselinePolicy policy = BaselinePolicy::Timed);

} // namespace nk::tools
