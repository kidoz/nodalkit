# Case study: building an emulator frontend

This guide assembles a small emulator-style shell from NodalKit parts. It is the
non-trivial counterpart to the counter in
[BUILDING_APPLICATIONS.md](BUILDING_APPLICATIONS.md), and it exercises the pieces
an emulator GUI actually needs: a command surface, recent files, a live log, a
child-process run, and safe shutdown.

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

Keep emulator/core logic **outside** the widget tree. The frontend talks to a
thin application-controller that owns the core; the widgets only render state and
forward intents. (See the ownership model in BUILDING_APPLICATIONS.md.)

## 1. Command routing

Route every command through one handler so the menu bar, native app menu,
toolbar buttons, and shortcuts all share behavior:

```cpp
void FrontendController::handle_command(Command cmd) {
    switch (cmd) {
        case Command::OpenXbe:     open_xbe_dialog(); break;
        case Command::StartRun:    start_emulation(); break;
        case Command::ExportBundle: export_support_bundle(); break;
        // …
    }
}
```

## 2. Recent files and settings

Persist recent XBEs, window geometry, and debug options with
[`nk::Settings`](../include/nk/model/settings.h) — no registry code:

```cpp
settings_.push_recent_file(path.string());          // dedupes + trims
settings_.set_bool("auto_convert", auto_convert_);
settings_.set_window_geometry("main", current_geometry());
settings_.save();

// On launch:
settings_.load();
for (const auto& recent : settings_.recent_files()) add_recent_menu_item(recent);
```

Populate the **Open Recent** submenu from `settings_.recent_files()`, and write
back through the same handler that opens a file.

## 3. The log drawer

Use [`nk::LogView`](../include/nk/widgets/log_view.h) for the debug/HLE trace.
It is append-only and virtualized, so it follows heavy output without stalling:

```cpp
log_->append_line("HLE match: XapiInitProcess", nk::LogSeverity::Success);
log_->append_line("Unimplemented: D3D::SetRenderState", nk::LogSeverity::Warning);
// Search / pause / clear wire to the drawer toolbar:
log_->search(query);            // highlights + steps matches
log_->set_auto_scroll(!paused); // pause to read back
log_->clear();
```

Style lines by `LogSeverity` (warning / exception / HLE match / ordinary), and
reuse `log_->export_text()` when building a support bundle.

## 4. Starting the emulator

Launch and monitor the emulator process from a worker thread, posting status
back with `EventLoop::post()` — never block the UI. Copy the `ChildProcess`
helper and the pattern from
[`examples/process_launch.cpp`](../examples/process_launch.cpp) and
[PROCESS_LAUNCH.md](PROCESS_LAUNCH.md). Stream the child's stdout into the
`LogView`; update the status bar with the pid while running and the exit code
when it ends.

## 5. Output preview

For a title logo or frame preview, use
[`nk::ImageView`](../include/nk/widgets/image_view.h): its
`update_pixel_buffer()` is thread-safe, so an emulation thread can push ARGB
frames. For full accelerated output, host a separate native window rather than a
widget-tree swap chain — see the embedding notes in
[NATIVE_INTEROP.md](NATIVE_INTEROP.md).

## 6. Safe shutdown

While a run or a conversion is in flight, veto the window close and confirm
first:

```cpp
window.set_close_policy([this] {
    if (!is_running()) return true;          // nothing in flight → allow
    confirm_stop_dialog([this] { window_.close(); }); // async; force on confirm
    return false;                            // veto this close request
});
```

`request_close()` (the platform close button) consults the policy; `close()`
forces it once the user confirms. See `Window::set_close_policy` in
[window.h](../include/nk/platform/window.h).

## 7. Native handle for staged migration

If you are porting an existing Win32 frontend incrementally, reach the `HWND`
with [`nk::window_hwnd()`](../include/nk/platform/windows_interop.h) to keep
legacy dialogs alive during the transition — see [NATIVE_INTEROP.md](NATIVE_INTEROP.md).

## Related

- [BUILDING_APPLICATIONS.md](BUILDING_APPLICATIONS.md) — ownership model, command routing.
- [WINDOWS_SUPPORT.md](WINDOWS_SUPPORT.md) — what the Windows backend covers.
- [MESON_INTEGRATION.md](MESON_INTEGRATION.md) — C++23 frontend + C++17 core.
- [DIAGNOSTICS.md](DIAGNOSTICS.md) — support bundles.
- [ACCESSIBILITY_CHECKLIST.md](ACCESSIBILITY_CHECKLIST.md) — validate the dialogs.
