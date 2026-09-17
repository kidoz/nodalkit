# Assemble an emulator frontend

This guide assembles a small emulator-style shell from NodalKit parts. It is
the non-trivial counterpart to the counter in
[Your first NodalKit application](../tutorials/your-first-application.md),
and it exercises the pieces an emulator GUI actually needs: a command
surface, recent files, a live log, a child-process run, and safe shutdown.

It maps directly onto the classic CXBX GUI (open XBE, recent files, start
emulation, debug output, video/controller settings) without any Win32 modal
code.

## Shape

```
┌───────────────────────────────────────────────┐
│ Menu bar: File · Emulation · View · Help       │
├───────────────┬───────────────────────────────┤
│ Game / info   │  Log drawer (LogView)          │
│ panel         │  [search] [pause] [clear]      │
│ (ImageView    │  … streaming HLE/debug trace …  │
│  preview)     │                                 │
├───────────────┴───────────────────────────────┤
│ Status bar: loaded title · run state · pid     │
└───────────────────────────────────────────────┘
```

Keep emulator/core logic outside the widget tree. The frontend talks to a
thin application controller that owns the core; the widgets only render
state and forward intents. The ownership model is explained in
[Application architecture](../explanation/application-architecture.md).

## 1. Route every command through one handler

Route every command through one handler so the menu bar, native app menu,
toolbar buttons, and shortcuts all share behavior:

```cpp
void FrontendController::handle_command(Command cmd) {
    switch (cmd) {
        case Command::OpenXbe:      open_xbe_dialog(); break;
        case Command::StartRun:     start_emulation(); break;
        case Command::ExportBundle: export_support_bundle(); break;
        // …
    }
}
```

## 2. Persist recent files and settings

Persist recent XBEs, window geometry, and debug options with
[`nk::Settings`](../../include/nk/model/settings.h), with no registry code:

```cpp
settings_.push_recent_file(path.string());          // dedupes + trims
settings_.set_bool("auto_convert", auto_convert_);
settings_.set_window_geometry("main", current_geometry());
settings_.save();

// On launch:
settings_.load();
for (const auto& recent : settings_.recent_files()) add_recent_menu_item(recent);
```

Populate the **Open Recent** submenu from `settings_.recent_files()`, and
write back through the same handler that opens a file.

## 3. Add the log drawer

Use [`nk::LogView`](../../include/nk/widgets/log_view.h) for the debug/HLE
trace. It is append-only and virtualized, so it follows heavy output
without stalling:

```cpp
log_->append_line("HLE match: XapiInitProcess", nk::LogSeverity::Success);
log_->append_line("Unimplemented: D3D::SetRenderState", nk::LogSeverity::Warning);
// Search / pause / clear wire to the drawer toolbar:
log_->search(query);            // highlights + steps matches
log_->set_auto_scroll(!paused); // pause to read back
log_->clear();
```

Style lines by `LogSeverity` (warning, exception, HLE match, ordinary), and
reuse `log_->export_text()` when building a support bundle.

## 4. Start the emulator

Launch and monitor the emulator process from a worker thread, posting
status back with `EventLoop::post()`; never block the UI. Copy the
`ChildProcess` helper and the pattern from
[`examples/process_launch.cpp`](../../examples/process_launch.cpp) and
[Launch and monitor external processes](launch-external-processes.md).
Stream the child's stdout into the `LogView`; update the status bar with the
pid while running and the exit code when it ends.

## 5. Show an output preview

For a title logo or frame preview, use
[`nk::ImageView`](../../include/nk/widgets/image_view.h): its
`update_pixel_buffer()` is thread-safe, so an emulation thread can push ARGB
frames. For full accelerated output, host a separate native window rather
than a widget-tree swap chain; see
[Use native window handles](use-native-window-handles.md).

## 6. Shut down safely

While a run or a conversion is in flight, veto the window close and confirm
first:

```cpp
window.set_close_policy([this] {
    if (!is_running()) return true;          // nothing in flight: allow
    confirm_stop_dialog([this] { window_.close(); }); // async; force on confirm
    return false;                            // veto this close request
});
```

`request_close()` (the platform close button) consults the policy;
`close()` forces it once the user confirms. See `Window::set_close_policy`
in [window.h](../../include/nk/platform/window.h).

## 7. Keep a native handle for staged migration

If you are porting an existing Win32 frontend incrementally, reach the
`HWND` with [`nk::window_hwnd()`](../../include/nk/platform/windows_interop.h)
to keep legacy dialogs alive during the transition; see
[Use native window handles](use-native-window-handles.md).

## Related

- [Application architecture](../explanation/application-architecture.md):
  ownership model and command routing.
- [Windows support matrix](../reference/windows-support-matrix.md): what
  the Windows backend covers.
- [Integrate NodalKit into a Meson build](integrate-with-meson.md): C++23
  frontend with a C++17 core.
- [Export a support bundle](export-a-support-bundle.md).
- [Validate accessibility and keyboard behavior](validate-accessibility.md):
  validate the dialogs.
