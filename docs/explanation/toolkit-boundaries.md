# Toolkit boundaries

This page explains where NodalKit draws the line between what the toolkit
provides and what stays in application code, and why.

## The rule of thumb

Reusable interaction belongs in the toolkit. Application-specific workflow
belongs in the application.

Add a primitive to NodalKit when you need the same interaction pattern more
than once, when the behavior belongs to widget or runtime contracts, or when
multiple applications would benefit from the same solution. Keep it in
application code when it is domain-specific, when it is only one page's
content, or when it does not generalize cleanly.

## Why there is no process API

NodalKit does not ship a process API. Process management is inherently
platform-specific and application-owned: which executable, which arguments,
how stdout is consumed, what the exit code means. Folding it into the
toolkit would add surface area that every application pays for but few
need.

Instead, NodalKit provides the one primitive that matters for a responsive
UI, a thread-safe way to post results back to the UI thread through
`EventLoop::post()`, and documents the pattern around it in
[Launch and monitor external processes](../how-to/launch-external-processes.md).
The example's `ChildProcess` helper is deliberately self-contained so an
application can copy and adapt it rather than depend on a toolkit
abstraction that would have to cover every platform's process model.

## Why native handles are exposed, and only as far as they are

NodalKit owns its platform surfaces. Real applications, especially ones
migrating from a native toolkit, still sometimes need the underlying window
handle to bridge legacy code or call platform APIs. The toolkit therefore
exposes the handle through a small, stable contract described in the
[Native handle contract](../reference/native-handles.md), with typed
helpers on Windows so consumers do not have to include `<windows.h>` in
their own headers.

What it does not do is let application code reach into private backend
headers, run a second blocking native message loop on the UI thread, or
reparent NodalKit's own surface into another window. Each of those would
tie application code to backend internals that are free to change between
releases, or would starve the toolkit's frame and input dispatch.

## Why capture and diagnostics are split the way they are

NodalKit's diagnostics write a directory of toolkit-level artifacts: widget
trees, frame timings, render snapshots, traces, and screenshots. It does not
know about your logs, crash summaries, or domain state, and it does not
archive. The [support bundle pattern](../how-to/export-a-support-bundle.md)
is therefore additive: the toolkit writes its files, the application writes
its own beside them under distinct names, and the application decides
whether to zip the result. This keeps the toolkit's output stable and lets
an application grow its bundle without waiting for a toolkit change.

## Why the toolkit is C++23 but your core does not have to be

NodalKit is C++23-first because its API relies on features that only exist
there. That should not force C++23 onto an unrelated C++17 core in the same
process. The constraint that actually matters is a single C++ runtime, not a
single `-std` flag; the reasoning is in
[Mixing C++ standards in one process](mixing-cpp-standards.md).
