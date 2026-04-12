#pragma once

/// @file timed_animation.h
/// @brief Timestamp-driven value animation with easing.

#include <chrono>
#include <nk/animation/easing.h>

namespace nk {

/// A timed animation that progresses from 0.0 to 1.0 over a duration,
/// shaped by an easing function. Driven by wall-clock timestamps — call
/// value() during each frame's snapshot pass and queue another redraw
/// until is_finished() returns true.
///
/// Usage in a widget:
/// @code
///   // On state change (e.g. hover enter):
///   hover_anim_.start();
///   queue_redraw();
///
///   // In snapshot():
///   float t = hover_anim_.value();
///   Color bg = lerp(normal_bg, hovered_bg, t);
///   if (!hover_anim_.is_finished()) {
///       queue_redraw();
///   }
/// @endcode
///
/// For users who prefer reduced motion (SystemPreferences::motion ==
/// MotionPreference::Reduced), either skip the animation entirely or
/// construct with a zero duration — value() will return 1.0 immediately.
class TimedAnimation {
public:
    /// Create an animation with the given duration and easing.
    /// A zero duration makes value() return 1.0 immediately after start().
    explicit TimedAnimation(std::chrono::milliseconds duration,
                            EasingFunction easing = easing::ease_in_out);

    /// Start (or restart) the animation from 0.0.
    void start();

    /// Stop the animation at its current value.
    void stop();

    /// Whether the animation is currently running (started and not finished).
    [[nodiscard]] bool is_running() const;

    /// Whether the animation has reached its full duration.
    [[nodiscard]] bool is_finished() const;

    /// Current eased value in [0, 1]. Returns 0 before start(), 1 after
    /// completion, and the eased progress in between.
    [[nodiscard]] float value() const;

    /// Current eased value computed at the given timestamp.
    [[nodiscard]] float value_at(std::chrono::steady_clock::time_point now) const;

    /// The animation duration.
    [[nodiscard]] std::chrono::milliseconds duration() const;

private:
    std::chrono::milliseconds duration_;
    EasingFunction easing_;
    std::chrono::steady_clock::time_point start_time_{};
    bool running_ = false;
    bool finished_ = false;
};

} // namespace nk
