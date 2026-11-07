# Diagnostics facilities

This page lists the diagnostics NodalKit provides for inspecting a running
window and for producing shareable artifacts. How to combine them with your
own application data is covered in
[Export a support bundle](../how-to/export-a-support-bundle.md).

The declarations are in
[`nk/debug/diagnostics.h`](../../include/nk/debug/diagnostics.h) and
[`nk/platform/window_inspector.h`](../../include/nk/platform/window_inspector.h).
Every window exposes its inspector through `window.inspector()`.

Vulkan's render hotspot counters include `texture_allocation_count`,
`texture_upload_byte_count`, and `texture_upload_batch_count` for the frame,
plus `texture_cache_byte_count` for resident image/text allocation bytes.
Upload bytes count prepared pixel data; the batch count records submitted
uploads. These fields round-trip through frame diagnostics JSON; older
artifacts load them as zero. Use `gpu_present_path` to identify software
fallback before comparing GPU workloads.

## Facilities

| Facility | Entry points | Contents |
| -------- | ------------ | -------- |
| Widget-tree dumps | `window.inspector().debug_tree()`, `format_widget_debug_tree`, `format_widget_debug_json` | Each node's accessibility role, name, state, and relations. The text form is suited to diffing in tests; the JSON form round-trips through `parse_widget_debug_json` and the `load_`/`save_widget_debug_json_file` helpers. |
| Frame diagnostics | `FrameDiagnostics`, `build_frame_time_histogram`, `format_frame_diagnostics_artifact_json` | Per-frame timing and a histogram of hotspots (measure, allocate, snapshot, text churn). |
| Render snapshots | `build_render_snapshot`, `format_render_snapshot_json` | The render node tree for the current scene, with JSON load and save helpers. |
| Trace export | `format_frame_diagnostics_trace_json` | A Chrome trace-event array you can load in `chrome://tracing`. |
| Screenshots | `window.inspector().capture_debug_screenshot()`, `save_debug_screenshot_ppm_file(path)` | The window pixels as RGBA8 plus the `source_backend` they came from, or the same data written as a binary PPM. |
| One-call bundle | `window.inspector().save_debug_bundle(directory)` | Writes the artifacts above into the given directory and returns a `Result<void>`. |

## Screenshot source

The captured frame is the one the live renderer last presented whenever
that renderer can read it back, which today means the software and Vulkan
backends, so GPU output is what you review. Backends without readback fall
back to a software re-render of the current scene. The bundle manifest
records which one you got under `screenshot_source`.

## Log views

[`LogView`](../../include/nk/widgets/log_view.h) is the widget intended for
live diagnostic output. It is append-only and virtualized, styles lines by
`LogSeverity`, supports `search()`, `set_auto_scroll()`, and `clear()`, and
returns its full content through `export_text()` for inclusion in a bundle.

## Example

The showcase example exports a bundle from a menu action; see
[`examples/showcase/showcase_app.cpp`](../../examples/showcase/showcase_app.cpp).

## Performance regression baselines

`meson test -C buildDir` compares the `text-heavy`, `list-heavy`, and
`animation-heavy` harness captures against platform-specific fixtures in
`tests/fixtures/`. Linux and Windows retain the timed comparison policy.
macOS uses `--baseline-policy=counters`: frame counts, rendering/model maxima,
runtime events, and total layout/text work are blocking; wall-clock comparisons
are printed as advisory. Sanitizer builds exercise the scenarios without
baseline comparisons. Native resource cleanup is checked separately by the
macOS bridge and Metal renderer lifetime tests.

The macOS fixtures were captured on 7 November 2026 with macOS 27.2, an Apple
M4 Max, a 2x display, and Apple Clang 21.0.0. Ten debug and ten release captures
per scenario produced identical checked work counters. Each committed fixture
pair is one release capture near the median total layout/snapshot/render cost,
so the artifact and trace come from the same run. Wall-clock times varied
between build types and are not hardware-independent budgets. Physical
footprint is advisory because allocator and driver retention affects it.

Reproduce a macOS comparison with the software renderer used by these fixtures:

```sh
buildDir/tools/nk_trace_capture_harness text-heavy \
  buildDir/candidate.frames.json buildDir/candidate.trace.json \
  --backend=software --baseline-policy=counters \
  --baseline-artifact=tests/fixtures/stable_macos_text_heavy_frame_diagnostics.json \
  --baseline-trace=tests/fixtures/stable_macos_text_heavy_trace.json
```

The `counters` policy permits increases of one total widget measure/allocation,
one total layout/redraw request, and two total text measurements/shapes/nodes.
It also retains the existing maxima and runtime-event checks. Totals detect
repeated work across frames even when the per-frame maximum stays unchanged.
The diagnostics tooling owns these fixtures; comparison logic and tolerances
live in `tools/trace_capture_baseline.cpp`.
`trace_capture_baseline` tests matching captures, improvements, timing-only
drift, and deliberate counter/structure failures for the policies.

To refresh fixtures, first run each scenario repeatedly in both build types
without baseline arguments. Review frame counts and counters across all runs,
then select an unmodified representative artifact/trace pair for each scenario.
Record the host and rationale here, and rerun the comparison tests. A proposed
timing limit needs stable runner history and explicit pass/fail tests before
being promoted to a blocking macOS gate.
