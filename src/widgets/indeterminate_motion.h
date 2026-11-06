#pragma once

#include <chrono>
#include <functional>
#include <nk/foundation/signal.h>
#include <nk/runtime/event_loop.h>
#include <string>

namespace nk {

class Widget;

namespace detail {

/// Clock for an indeterminate indicator.
///
/// The stored phase changes only from the timer, from nudge(), or when the
/// clock stops. snapshot() reads phase() and does not advance it.
class IndeterminateMotion {
public:
    IndeterminateMotion(Widget& owner, std::string timer_label);
    ~IndeterminateMotion();

    IndeterminateMotion(const IndeterminateMotion&) = delete;
    IndeterminateMotion& operator=(const IndeterminateMotion&) = delete;

    void set_active(bool active);
    /// Reports whether the owner is attached to a visible window.
    /// IndeterminateMotion cannot call the protected host accessor itself.
    void set_host_gate(std::function<bool()> gate);
    void nudge(float delta);
    [[nodiscard]] float phase() const;
    void sync();

private:
    void watch_application();
    void freeze();
    void stop_timer();
    void arm(EventLoop& loop);
    void on_tick();
    void rebase(std::chrono::steady_clock::time_point now);
    [[nodiscard]] float phase_at(std::chrono::steady_clock::time_point now) const;
    [[nodiscard]] bool should_run() const;

    Widget& owner_;
    std::string timer_label_;
    std::function<bool()> host_gate_;
    bool active_ = false;
    // Parked where a progress segment sits fully inside the track.
    float phase_ = 0.5F;
    std::chrono::steady_clock::time_point origin_{};
    CallbackHandle handle_{};
    EventLoop* loop_ = nullptr;
    ScopedConnection preferences_connection_;
    ScopedConnection theme_connection_;
    bool watching_application_ = false;

    static constexpr std::chrono::milliseconds kPeriod{1000};
    static constexpr std::chrono::milliseconds kTick{16};
};

} // namespace detail

} // namespace nk
