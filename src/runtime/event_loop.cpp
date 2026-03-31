#include <nk/runtime/event_loop.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace nk {

struct EventLoop::Impl {
    bool running = false;
    int exit_code = 0;
    uint64_t next_handle_id = 1;

    std::deque<std::function<void()>> posted_tasks;
    std::mutex task_mutex;

    struct TimerEntry {
        CallbackHandle handle;
        std::chrono::steady_clock::time_point fire_at;
        std::chrono::milliseconds interval; // 0 for one-shot
        std::function<void()> callback;
        bool cancelled = false;
    };

    std::vector<TimerEntry> timers;
    std::vector<std::function<void()>> idle_callbacks;

    [[nodiscard]] CallbackHandle next_handle() {
        return {next_handle_id++};
    }
};

EventLoop::EventLoop() : impl_(std::make_unique<Impl>()) {}
EventLoop::~EventLoop() = default;

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

void EventLoop::post(std::function<void()> task) {
    {
        std::lock_guard lock(impl_->task_mutex);
        impl_->posted_tasks.push_back(std::move(task));
    }
    wake();
}

void EventLoop::wake() {
    // Stub: platform backends override the run loop to use a real
    // wake mechanism (eventfd on Linux, CFRunLoopWakeUp on macOS).
    // For the fallback busy-spin loop this is a no-op.
}

bool EventLoop::is_running() const { return impl_->running; }
int EventLoop::exit_code() const { return impl_->exit_code; }

CallbackHandle EventLoop::set_timeout(
    std::chrono::milliseconds delay, std::function<void()> callback) {
    auto h = impl_->next_handle();
    impl_->timers.push_back({
        h,
        std::chrono::steady_clock::now() + delay,
        std::chrono::milliseconds{0},
        std::move(callback),
        false,
    });
    return h;
}

CallbackHandle EventLoop::set_interval(
    std::chrono::milliseconds interval, std::function<void()> callback) {
    auto h = impl_->next_handle();
    impl_->timers.push_back({
        h,
        std::chrono::steady_clock::now() + interval,
        interval,
        std::move(callback),
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
}

CallbackHandle EventLoop::add_idle(std::function<void()> callback) {
    auto h = impl_->next_handle();
    impl_->idle_callbacks.push_back(std::move(callback));
    return h;
}

bool EventLoop::poll() {
    bool did_work = false;
    auto const now = std::chrono::steady_clock::now();

    // Drain posted tasks.
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard lock(impl_->task_mutex);
        tasks.swap(impl_->posted_tasks);
    }
    for (auto& task : tasks) {
        task();
        did_work = true;
    }

    // Fire ready timers.
    for (auto& t : impl_->timers) {
        if (t.cancelled) {
            continue;
        }
        if (now >= t.fire_at) {
            t.callback();
            did_work = true;
            if (t.interval.count() > 0) {
                t.fire_at = now + t.interval;
            } else {
                t.cancelled = true;
            }
        }
    }

    // Purge cancelled timers.
    std::erase_if(impl_->timers, [](auto const& t) { return t.cancelled; });

    // Run idle callbacks if nothing else happened.
    if (!did_work) {
        auto idles = std::move(impl_->idle_callbacks);
        impl_->idle_callbacks.clear();
        for (auto& cb : idles) {
            cb();
            did_work = true;
        }
    }

    return did_work;
}

} // namespace nk
