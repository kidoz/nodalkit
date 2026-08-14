#include <chrono>
#include <deque>
#include <mutex>
#include <nk/runtime/event_loop.h>
#include <string>
#include <thread>
#include <vector>

namespace nk {

struct EventLoop::Impl {
    struct PostedTask {
        std::chrono::steady_clock::time_point queued_at;
        std::function<void()> callback;
        std::string source_label;
    };

    bool running = false;
    int exit_code = 0;
    uint64_t next_handle_id = 1;

    std::chrono::steady_clock::time_point diagnostics_origin = std::chrono::steady_clock::now();
    std::deque<PostedTask> posted_tasks;
    std::mutex task_mutex;

    struct TimerEntry {
        CallbackHandle handle;
        std::chrono::steady_clock::time_point fire_at;
        std::chrono::milliseconds interval; // 0 for one-shot
        std::function<void()> callback;
        std::string source_label;
        bool cancelled = false;
    };

    std::vector<TimerEntry> timers;

    struct IdleEntry {
        CallbackHandle handle;
        std::chrono::steady_clock::time_point queued_at;
        std::function<void()> callback;
        std::string source_label;
        bool cancelled = false;
    };

    std::vector<IdleEntry> idle_callbacks;
    std::vector<TraceEvent> trace_events;

    [[nodiscard]] CallbackHandle next_handle() { return {next_handle_id++}; }
};

namespace {

thread_local EventLoop* current_event_loop = nullptr;

constexpr std::size_t kTraceEventHistoryLimit = 256;

double elapsed_ms(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void append_trace_event(std::vector<TraceEvent>& trace_events,
                        std::chrono::steady_clock::time_point diagnostics_origin,
                        std::string name,
                        std::string category,
                        std::chrono::steady_clock::time_point start,
                        std::chrono::steady_clock::time_point end,
                        std::string detail = {},
                        std::string source_label = {}) {
    trace_events.push_back({
        .name = std::move(name),
        .category = std::move(category),
        .timestamp_ms = elapsed_ms(diagnostics_origin, start),
        .duration_ms = elapsed_ms(start, end),
        .detail = std::move(detail),
        .source_label = std::move(source_label),
    });
    if (trace_events.size() > kTraceEventHistoryLimit) {
        trace_events.erase(
            trace_events.begin(),
            trace_events.begin() +
                static_cast<std::ptrdiff_t>(trace_events.size() - kTraceEventHistoryLimit));
    }
}

} // namespace

EventLoop::EventLoop() : impl_(std::make_unique<Impl>()) {
    if (!current_event_loop) {
        current_event_loop = this;
    }
}

EventLoop::~EventLoop() {
    if (current_event_loop == this) {
        current_event_loop = nullptr;
    }
}

EventLoop* EventLoop::current() {
    return current_event_loop;
}

int EventLoop::run() {
    impl_->running = true;
    impl_->exit_code = 0;

    while (impl_->running) {
        poll();

        // Avoid busy-waiting in this stub. A real implementation would
        // use epoll/kqueue/Wayland fd polling.
        if (impl_->running) {
            std::this_thread::yield();
        }
    }

    return impl_->exit_code;
}

void EventLoop::quit(int exit_code) {
    impl_->exit_code = exit_code;
    impl_->running = false;
}

void EventLoop::post(std::function<void()> task, std::string_view source_label) {
    {
        std::lock_guard lock(impl_->task_mutex);
        impl_->posted_tasks.push_back({
            .queued_at = std::chrono::steady_clock::now(),
            .callback = std::move(task),
            .source_label = std::string(source_label),
        });
    }
    wake();
}

void EventLoop::wake() {
    // Stub: platform backends override the run loop to use a real
    // wake mechanism (eventfd on Linux, CFRunLoopWakeUp on macOS).
    // For the fallback busy-spin loop this is a no-op.
}

bool EventLoop::is_running() const {
    return impl_->running;
}

int EventLoop::exit_code() const {
    return impl_->exit_code;
}

CallbackHandle EventLoop::set_timeout(std::chrono::milliseconds delay,
                                      std::function<void()> callback,
                                      std::string_view source_label) {
    auto h = impl_->next_handle();
    impl_->timers.push_back({
        h,
        std::chrono::steady_clock::now() + delay,
        std::chrono::milliseconds{0},
        std::move(callback),
        std::string(source_label),
        false,
    });
    return h;
}

CallbackHandle EventLoop::set_interval(std::chrono::milliseconds interval,
                                       std::function<void()> callback,
                                       std::string_view source_label) {
    auto h = impl_->next_handle();
    impl_->timers.push_back({
        h,
        std::chrono::steady_clock::now() + interval,
        interval,
        std::move(callback),
        std::string(source_label),
        false,
    });
    return h;
}

void EventLoop::cancel(CallbackHandle handle) {
    for (auto& t : impl_->timers) {
        if (t.handle.id == handle.id) {
            t.cancelled = true;
        }
    }
    for (auto& idle : impl_->idle_callbacks) {
        if (idle.handle.id == handle.id) {
            idle.cancelled = true;
        }
    }
}

CallbackHandle EventLoop::add_idle(std::function<void()> callback, std::string_view source_label) {
    auto h = impl_->next_handle();
    impl_->idle_callbacks.push_back({
        h,
        std::chrono::steady_clock::now(),
        std::move(callback),
        std::string(source_label),
        false,
    });
    return h;
}

bool EventLoop::poll() {
    bool did_work = false;
    const auto now = std::chrono::steady_clock::now();

    // Drain posted tasks.
    std::deque<Impl::PostedTask> tasks;
    {
        std::lock_guard lock(impl_->task_mutex);
        tasks.swap(impl_->posted_tasks);
    }
    for (auto& task : tasks) {
        const auto started_at = std::chrono::steady_clock::now();
        task.callback();
        const auto finished_at = std::chrono::steady_clock::now();
        append_trace_event(impl_->trace_events,
                           impl_->diagnostics_origin,
                           "posted-task",
                           "event-loop-task",
                           started_at,
                           finished_at,
                           "queued_ms=" + std::to_string(elapsed_ms(task.queued_at, started_at)),
                           task.source_label);
        did_work = true;
    }

    // Fire ready timers. Iterate a snapshot of the due timers: a callback may
    // schedule or cancel timers, or run a nested poll(), all of which mutate
    // impl_->timers and would invalidate live iterators. Each entry is
    // fenced (one-shots consumed, intervals parked at a far-future fire_at)
    // so a nested poll() cannot fire it again, then re-found by handle after
    // the callback so cancellation from inside a callback is honored.
    const auto find_timer = [](std::vector<Impl::TimerEntry>& timers, uint64_t id) {
        for (auto& t : timers) {
            if (t.handle.id == id) {
                return &t;
            }
        }
        return static_cast<Impl::TimerEntry*>(nullptr);
    };
    std::vector<Impl::TimerEntry> due;
    due.reserve(impl_->timers.size());
    for (const auto& t : impl_->timers) {
        if (!t.cancelled && now >= t.fire_at) {
            due.push_back(t);
        }
    }
    for (auto& firing : due) {
        auto* live = find_timer(impl_->timers, firing.handle.id);
        if (live == nullptr || live->cancelled) {
            continue;
        }
        const bool is_interval = live->interval.count() > 0;
        live->fire_at = std::chrono::steady_clock::time_point::max();
        if (!is_interval) {
            live->cancelled = true;
        }
        const auto started_at = std::chrono::steady_clock::now();
        firing.callback();
        const auto finished_at = std::chrono::steady_clock::now();
        append_trace_event(impl_->trace_events,
                           impl_->diagnostics_origin,
                           is_interval ? "interval" : "timeout",
                           "event-loop-timer",
                           started_at,
                           finished_at,
                           "lateness_ms=" + std::to_string(elapsed_ms(firing.fire_at, started_at)),
                           firing.source_label);
        did_work = true;
        if (auto* rearmed = find_timer(impl_->timers, firing.handle.id);
            rearmed != nullptr && is_interval && !rearmed->cancelled) {
            rearmed->fire_at = now + rearmed->interval;
        }
    }

    // Purge cancelled timers.
    std::erase_if(impl_->timers, [](const auto& t) { return t.cancelled; });

    // Run idle callbacks if nothing else happened.
    if (!did_work) {
        auto idles = std::move(impl_->idle_callbacks);
        impl_->idle_callbacks.clear();
        for (auto& idle : idles) {
            if (idle.cancelled) {
                continue;
            }
            const auto started_at = std::chrono::steady_clock::now();
            idle.callback();
            const auto finished_at = std::chrono::steady_clock::now();
            append_trace_event(impl_->trace_events,
                               impl_->diagnostics_origin,
                               "idle",
                               "event-loop-idle",
                               started_at,
                               finished_at,
                               "queued_ms=" +
                                   std::to_string(elapsed_ms(idle.queued_at, started_at)),
                               idle.source_label);
            did_work = true;
        }
    }

    return did_work;
}

std::span<const TraceEvent> EventLoop::debug_trace_events() const {
    return impl_->trace_events;
}

void EventLoop::clear_debug_trace_events() {
    impl_->trace_events.clear();
}

} // namespace nk
