# Diagnostics and support bundles

NodalKit ships diagnostics tooling that an application can extend with its own
domain data to produce a single, shareable support bundle.

## What NodalKit provides

From [`nk/debug/diagnostics.h`](../include/nk/debug/diagnostics.h) and
[`nk/platform/window_inspector.h`](../include/nk/platform/window_inspector.h):

- **Widget-tree dumps** — `format_widget_debug_tree` / `_json`, including each
  node's accessibility role, name, state, and relations.
- **Frame diagnostics** — `FrameDiagnostics` with per-frame timing and a
  histogram of hotspots (measure/allocate/snapshot/text churn).
- **Render snapshots** — `build_render_snapshot` / `format_render_snapshot_json`.
- **Trace export** — `format_frame_diagnostics_trace_json`, a Chrome
  trace-event array you can load in `chrome://tracing`.
- **One-call bundle** — `window.inspector().save_debug_bundle(directory)` writes
  the above into a directory.

The showcase example exports a bundle from a menu action; see
`examples/showcase/showcase_app.cpp`.

## Attaching application-domain data

`save_debug_bundle()` writes into a directory you choose. The integration
pattern is: call it, then drop your own files **beside** its output so one
directory (or one zip of it) contains everything a bug report needs.

```cpp
void export_support_bundle(nk::Window& window, const CxbxSession& session) {
    const std::filesystem::path dir = chosen_bundle_directory();

    // 1. NodalKit widget/frame/render/trace diagnostics.
    window.inspector().save_debug_bundle(dir);

    // 2. Application-domain logs and summaries, written alongside.
    std::filesystem::copy_file(session.log_path(), dir / "cxbx.log",
                               std::filesystem::copy_options::overwrite_existing);
    write_text(dir / "hle_coverage.json", session.parsed_hle_coverage());
    write_text(dir / "crash_summary.txt", session.last_crash_summary());
    write_text(dir / "platform.txt", collect_build_and_os_metadata());
}
```

Guidelines:

- **Keep NodalKit and app files namespaced** — NodalKit's files use its own
  names; give yours distinct names (`cxbx.log`, `hle_coverage.json`) so nothing
  collides.
- **Stream live logs through [`LogView`](../include/nk/widgets/log_view.h)** and
  reuse its `export_text()` for the log portion of the bundle, so the bundle
  matches exactly what the user saw.
- **Zip on export** if you want a single artifact; NodalKit writes a directory
  and leaves archiving to the app.

## Live log following

For high-volume traces, feed lines into a `LogView` from your worker thread via
`EventLoop::post()` (see [PROCESS_LAUNCH.md](PROCESS_LAUNCH.md)). It is
append-only and virtualized, styles lines by `LogSeverity`, and supports search
— so "follow the HLE trace, filter to warnings, then export the bundle" is a
first-class flow.
