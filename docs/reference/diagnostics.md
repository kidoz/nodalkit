# Diagnostics facilities

This page lists the diagnostics NodalKit provides for inspecting a running
window and for producing shareable artifacts. How to combine them with your
own application data is covered in
[Export a support bundle](../how-to/export-a-support-bundle.md).

The declarations are in
[`nk/debug/diagnostics.h`](../../include/nk/debug/diagnostics.h) and
[`nk/platform/window_inspector.h`](../../include/nk/platform/window_inspector.h).
Every window exposes its inspector through `window.inspector()`.

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
