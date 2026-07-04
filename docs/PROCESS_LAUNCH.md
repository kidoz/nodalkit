# Launching and monitoring external processes

NodalKit does not ship a process API. Process management is inherently
platform-specific and application-owned (which executable, which arguments, how
stdout is consumed, what the exit code means), and folding it into the toolkit
would add surface area every app pays for but few need. Instead, NodalKit gives
you the one primitive that matters for a responsive UI — a thread-safe way to
post results back to the UI thread — and this document describes the pattern
around it.

The runnable version is [`examples/process_launch.cpp`](../examples/process_launch.cpp).

## The rule

**Never block the UI/event-loop thread waiting on a child process.** Launch and
wait on a worker thread; marshal every UI update back with `EventLoop::post()`,
which is safe to call from any thread.

```
UI thread                         worker thread
---------                         -------------
start button ── spawn worker ───▶ ChildProcess::start(argv)
                                  post(pid) ─────────────┐
run loop stays responsive ◀───────────────────────────��─┘
                                  loop: try_wait / sleep
                                        terminate on cancel
                                  post(exit_code) ───────┐
show exit code ◀─────────────────────────────────────────┘
```

## What to own in application code

The example's `ChildProcess` helper is intentionally self-contained so you can
copy and adapt it. It exposes what an emulator frontend needs:

| Need                     | Windows                       | POSIX                          |
| ------------------------ | ----------------------------- | ------------------------------ |
| Start                    | `CreateProcessW`              | `posix_spawnp`                 |
| Poll for exit (non-block)| `WaitForSingleObject(h, 0)`   | `waitpid(pid, …, WNOHANG)`     |
| Exit code                | `GetExitCodeProcess`          | `WEXITSTATUS(status)`          |
| Process id               | `PROCESS_INFORMATION.dwProcessId` | the `pid_t` from spawn     |
| Cancel / terminate       | `TerminateProcess`            | `kill(pid, SIGTERM)`           |

## stdout / stderr capture

Capture belongs in application code, not NodalKit. Create pipes at spawn time
(`CreatePipe` + `STARTUPINFO` on Windows; `posix_spawn_file_actions_adddup2` on
POSIX), read them on the worker thread, and stream lines into a
[`LogView`](../include/nk/widgets/log_view.h) via `EventLoop::post()`:

```cpp
app.event_loop().post([log, line] { log->append_line(line, nk::LogSeverity::Normal); },
                      "child-stdout");
```

`LogView` is append-only and virtualized, so following a high-volume child log
does not stall the UI.

## Keeping it responsive during load

For long in-process operations (converting an XBE, enumerating video modes) the
same rule applies — do the work on a worker thread and post progress back. See
[`examples/long_task.cpp`](../examples/long_task.cpp) for the progress/cancel
variant, and use `Window::set_close_policy()` to veto a close while an operation
is in flight (confirm, then call `Window::close()` to force it).
