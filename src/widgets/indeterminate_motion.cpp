#include "indeterminate_motion.h"

#include <cmath>
#include <nk/platform/application.h>
#include <nk/style/style_class.h>
#include <nk/style/theme.h>
#include <nk/ui_core/widget.h>
#include <utility>

namespace nk::detail {

namespace {

bool theme_requests_reduced_motion() {
    const auto theme = Theme::active();
    if (!theme) {
        return false;
    }
    const StyleValue* value = theme->token("motion-mode");
    return value != nullptr && std::holds_alternative<std::string>(*value) &&
           std::get<std::string>(*value) == "reduced";
}

float wrap_unit(float value) {
    float wrapped = value - std::floor(value);
    if (wrapped >= 1.0F) {
        wrapped = 0.0F;
    }
    return wrapped;
}

} // namespace

IndeterminateMotion::IndeterminateMotion(Widget& owner, std::string timer_label)
    : owner_(owner), timer_label_(std::move(timer_label)) {}

IndeterminateMotion::~IndeterminateMotion() {
    stop_timer();
}

void IndeterminateMotion::set_active(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    sync();
}

void IndeterminateMotion::set_host_gate(std::function<bool()> gate) {
    host_gate_ = std::move(gate);
}

void IndeterminateMotion::nudge(float delta) {
    if (handle_.valid()) {
        phase_ = phase_at(std::chrono::steady_clock::now());
    }
    phase_ = wrap_unit(phase_ + delta);
    if (handle_.valid()) {
        rebase(std::chrono::steady_clock::now());
    }
    owner_.queue_redraw();
}

float IndeterminateMotion::phase() const {
    return phase_;
}

void IndeterminateMotion::watch_application() {
    if (watching_application_) {
        return;
    }
    auto* app = Application::instance();
    if (app == nullptr) {
        return;
    }
    watching_application_ = true;
    preferences_connection_ = ScopedConnection(
        app->on_system_preferences_changed().connect([this](const SystemPreferences&) { sync(); }));
    theme_connection_ = ScopedConnection(
        app->on_theme_selection_changed().connect([this](const ThemeSelection&) { sync(); }));
}

void IndeterminateMotion::sync() {
    watch_application();
    auto* loop = EventLoop::current();
    if (!should_run() || loop == nullptr) {
        freeze();
        stop_timer();
        return;
    }
    if (handle_.valid() && loop_ == loop) {
        return;
    }
    freeze();
    stop_timer();
    arm(*loop);
}

void IndeterminateMotion::freeze() {
    if (!handle_.valid()) {
        return;
    }
    phase_ = phase_at(std::chrono::steady_clock::now());
}

void IndeterminateMotion::stop_timer() {
    auto* current = EventLoop::current();
    if (handle_.valid() && current != nullptr && current == loop_) {
        current->cancel(handle_);
    }
    handle_ = {};
    loop_ = nullptr;
}

void IndeterminateMotion::arm(EventLoop& loop) {
    rebase(std::chrono::steady_clock::now());
    loop_ = &loop;
    handle_ = loop.set_interval(kTick, [this] { on_tick(); }, timer_label_);
}

void IndeterminateMotion::on_tick() {
    const auto locked = owner_.weak_from_this().lock();
    if (!locked || EventLoop::current() != loop_) {
        stop_timer();
        return;
    }

    phase_ = phase_at(std::chrono::steady_clock::now());
    const bool keep = should_run();
    if (!keep) {
        stop_timer();
    }
    owner_.queue_redraw();
}

void IndeterminateMotion::rebase(std::chrono::steady_clock::time_point now) {
    const auto elapsed =
        std::chrono::duration<float, std::milli>(phase_ * static_cast<float>(kPeriod.count()));
    origin_ = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(elapsed);
}

float IndeterminateMotion::phase_at(std::chrono::steady_clock::time_point now) const {
    const auto elapsed = std::chrono::duration<float, std::milli>(now - origin_);
    return wrap_unit(elapsed.count() / static_cast<float>(kPeriod.count()));
}

bool IndeterminateMotion::should_run() const {
    if (!active_ || theme_requests_reduced_motion() || !owner_.is_visible() ||
        owner_.weak_from_this().expired() || !host_gate_) {
        return false;
    }
    return host_gate_();
}

} // namespace nk::detail
