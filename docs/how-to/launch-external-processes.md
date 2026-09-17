# Launch and monitor external processes

NodalKit does not ship a process API. This guide shows the supported pattern
for launching a child process, streaming its output, and reporting its exit
without blocking the UI. The runnable version is
[`examples/process_launch.cpp`](../../examples/process_launch.cpp). The
reasoning behind leaving process management to the application is in
[Toolkit boundaries](../explanation/toolkit-boundaries.md).

## The rule

Never block the UI/event-loop thread waiting on a child process. Launch and
wait on a worker thread; marshal every UI update back with
`EventLoop::post()`, which is safe to call from any thread.

```
UI thread                         worker thread
---------                         -------------
start button ── spawn worker ───▶ ChildProcess::start(argv)
                                  post(pid) ─────────────┐
run loop stays responsive ◀──────────────────────────────┘
                                  loop: try_wait / sleep
                                        terminate on cancel
                                  post(exit_code) ───────┐
show exit code ◀─────────────────────────────────────────┘
```

## Own the process helper in application code

The example's `ChildProcess` helper is intentionally self-contained so you
can copy and adapt it. It exposes what an emulator frontend needs:

| Need                     | Windows                       | POSIX                          |
| ------------------------ | ----------------------------- | ------------------------------ |
| Start                    | `CreateProcessW`              | `posix_spawnp`                 |
| Poll for exit (non-block)| `WaitForSingleObject(h, 0)`   | `waitpid(pid, …, WNOHANG)`     |
| Exit code                | `GetExitCodeProcess`          | `WEXITSTATUS(status)`          |
| Process id               | `PROCESS_INFORMATION.dwProcessId` | the `pid_t` from spawn     |
| Cancel / terminate       | `TerminateProcess`            | `kill(pid, SIGTERM)`           |

## Capture stdout and stderr

Capture belongs in application code, not NodalKit. Create pipes at spawn
time (`CreatePipe` + `STARTUPINFO` on Windows;
`posix_spawn_file_actions_adddup2` on POSIX), read them on the worker
thread, and stream lines into a
[`LogView`](../../include/nk/widgets/log_view.h) via `EventLoop::post()`:

```cpp
app.event_loop().post([log, line] { log->append_line(line, nk::LogSeverity::Normal); },
                      "child-stdout");
```

`LogView` is append-only and virtualized, so following a high-volume child
log does not stall the UI.

## Stay responsive during long in-process work

For long in-process operations (converting an XBE, enumerating video modes)
the same rule applies: do the work on a worker thread and post progress
back. See [`examples/long_task.cpp`](../../examples/long_task.cpp) for the
progress/cancel variant, and use `Window::set_close_policy()` to veto a
close while an operation is in flight (confirm, then call `Window::close()`
to force it).
