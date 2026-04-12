#include <algorithm>
#include <nk/animation/timed_animation.h>

namespace nk {

TimedAnimation::TimedAnimation(std::chrono::milliseconds duration, EasingFunction easing)
    : duration_(duration), easing_(std::move(easing)) {}

void TimedAnimation::start() {
    start_time_ = std::chrono::steady_clock::now();
    running_ = true;
    finished_ = false;
}

void TimedAnimation::stop() {
    running_ = false;
}

bool TimedAnimation::is_running() const {
    return running_ && !finished_;
}

bool TimedAnimation::is_finished() const {
    return finished_;
}

float TimedAnimation::value() const {
    return value_at(std::chrono::steady_clock::now());
}

float TimedAnimation::value_at(std::chrono::steady_clock::time_point now) const {
    if (!running_) {
        return finished_ ? 1.0F : 0.0F;
    }

    if (duration_.count() <= 0) {
        const_cast<TimedAnimation*>(this)->finished_ = true;
        return 1.0F;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    if (elapsed >= duration_) {
        const_cast<TimedAnimation*>(this)->finished_ = true;
        return 1.0F;
    }

    const float t = std::clamp(
        static_cast<float>(elapsed.count()) / static_cast<float>(duration_.count()), 0.0F, 1.0F);
    return easing_ ? easing_(t) : t;
}

std::chrono::milliseconds TimedAnimation::duration() const {
    return duration_;
}

} // namespace nk
