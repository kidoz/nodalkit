#pragma once

/// @file event_loop.h
/// @brief Main event loop for the NodalKit application runtime.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <nk/debug/diagnostics.h>
#include <span>
#include <string_view>

namespace nk {

/// Opaque handle for a scheduled timer or idle callback.
struct CallbackHandle {
    uint64_t id = 0;

    [[nodiscard]] bool valid() const { return id != 0; }
};

/// The central event loop. Processes platform events, timers, idle
/// callbacks, and posted tasks. One EventLoop per Application.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// Run the loop until quit() is called. Returns the exit code.
    int run();

    /// Request the loop to stop. The current iteration finishes first.
    void quit(int exit_code = 0);

    /// Post a task to be executed on the next iteration of the loop.
    void post(std::function<void()> task, std::string_view source_label = {});

    /// Schedule a one-shot timer.
    [[nodiscard]] CallbackHandle set_timeout(std::chrono::milliseconds delay,
                                             std::function<void()> callback,
                                             std::string_view source_label = {});

    /// Schedule a repeating timer.
    [[nodiscard]] CallbackHandle set_interval(std::chrono::milliseconds interval,
                                              std::function<void()> callback,
                                              std::string_view source_label = {});

    /// Cancel a previously scheduled timer or idle callback.
    void cancel(CallbackHandle handle);

    /// Schedule a callback to run when the loop is idle.
    [[nodiscard]] CallbackHandle add_idle(std::function<void()> callback,
                                          std::string_view source_label = {});

    /// Process pending events without blocking. Returns true if events
    /// were processed.
    bool poll();

    /// Wake the event loop from another thread. Thread-safe.
    /// Used by EventLoop::post() and platform backends.
    void wake();

    /// Whether the loop is currently running.
    [[nodiscard]] bool is_running() const;

    /// The exit code set by quit().
    [[nodiscard]] int exit_code() const;

    /// Recent runtime trace events captured from posted tasks, timers, and idles.
    [[nodiscard]] std::span<const TraceEvent> debug_trace_events() const;

    /// Clear retained runtime trace events.
    void clear_debug_trace_events();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
