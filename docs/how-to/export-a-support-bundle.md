# Export a support bundle

This guide shows how to produce one shareable directory that contains
NodalKit's widget, frame, render, and trace diagnostics together with your
application's own logs and summaries. What each NodalKit diagnostic contains
is listed in [Diagnostics facilities](../reference/diagnostics.md).

The showcase example exports a bundle from a menu action; see
[`examples/showcase/showcase_app.cpp`](../../examples/showcase/showcase_app.cpp).

## Write the NodalKit bundle, then add your files beside it

`window.inspector().save_debug_bundle(directory)` writes into a directory
you choose. The integration pattern is: call it, then drop your own files
beside its output so one directory (or one zip of it) contains everything a
bug report needs.

```cpp
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>

#include <filesystem>

void export_support_bundle(nk::Window& window, const CxbxSession& session) {
    const std::filesystem::path dir = chosen_bundle_directory();

    // 1. NodalKit widget/frame/render/trace diagnostics.
    (void)window.inspector().save_debug_bundle(dir.string());

    // 2. Application-domain logs and summaries, written alongside.
    std::filesystem::copy_file(session.log_path(), dir / "cxbx.log",
                               std::filesystem::copy_options::overwrite_existing);
    write_text(dir / "hle_coverage.json", session.parsed_hle_coverage());
    write_text(dir / "crash_summary.txt", session.last_crash_summary());
    write_text(dir / "platform.txt", collect_build_and_os_metadata());
}
```

`CxbxSession`, `chosen_bundle_directory()`, and `write_text()` are
placeholders for your application's own types and helpers.
`save_debug_bundle()` takes a string view and returns a `Result<void>`;
check it if you want to report a failed export to the user.

## Keep the files apart

- Keep NodalKit and app files namespaced. NodalKit's files use its own
  names; give yours distinct names (`cxbx.log`, `hle_coverage.json`) so
  nothing collides.
- Stream live logs through [`LogView`](../../include/nk/widgets/log_view.h)
  and reuse its `export_text()` for the log portion of the bundle, so the
  bundle matches exactly what the user saw.
- Zip on export if you want a single artifact. NodalKit writes a directory
  and leaves archiving to the app.

## Follow a live log before exporting

For high-volume traces, feed lines into a `LogView` from your worker thread
via `EventLoop::post()`; see
[Launch and monitor external processes](launch-external-processes.md). The
view is append-only and virtualized, styles lines by `LogSeverity`, and
supports search, so "follow the HLE trace, filter to warnings, then export
the bundle" is a first-class flow.
