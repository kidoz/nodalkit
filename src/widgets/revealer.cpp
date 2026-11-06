#include <algorithm>
#include <limits>
#include <nk/runtime/event_loop.h>
#include <nk/style/style_class.h>
#include <nk/style/theme.h>
#include <nk/widgets/revealer.h>

namespace nk {

namespace {

constexpr auto kDuration = std::chrono::milliseconds{160};
constexpr auto kTick = std::chrono::milliseconds{16};

bool theme_requests_reduced_motion() {
    const auto theme = Theme::active();
    if (!theme) {
        return false;
    }
    const StyleValue* value = theme->token("motion-mode");
    return value != nullptr && std::holds_alternative<std::string>(*value) &&
           std::get<std::string>(*value) == "reduced";
}

} // namespace

struct Revealer::Impl {
    std::shared_ptr<Widget> child;
    bool revealed = true;
    float fraction = 1.0F;
    float fraction_from = 1.0F;
    std::chrono::steady_clock::time_point started{};
    CallbackHandle timer{};
    Signal<> settled;
};

std::shared_ptr<Revealer> Revealer::create() {
    return std::shared_ptr<Revealer>(new Revealer());
}

Revealer::Revealer() : impl_(std::make_unique<Impl>()) {
    add_style_class("revealer");
    ensure_accessible().set_hidden(true);
}

Revealer::~Revealer() {
    cancel_timer();
}

void Revealer::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child == child) {
        return;
    }
    if (impl_->child) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child) {
        append_child(impl_->child);
        impl_->child->set_visible(impl_->fraction > 0.0F);
    }
    queue_layout();
}

Widget* Revealer::child() const {
    return impl_->child.get();
}

bool Revealer::is_revealed() const {
    return impl_->revealed;
}

void Revealer::set_revealed(bool revealed) {
    if (impl_->revealed == revealed && !impl_->timer.valid()) {
        return;
    }
    impl_->revealed = revealed;
    if (revealed && impl_->child) {
        impl_->child->set_visible(true);
    }
    const bool jump =
        prefers_reduced_motion() || EventLoop::current() == nullptr || weak_from_this().expired();
    if (jump) {
        cancel_timer();
        impl_->fraction = revealed ? 1.0F : 0.0F;
        finish_reveal();
        return;
    }
    start_animation();
}

float Revealer::reveal_fraction() const {
    return impl_->fraction;
}

Signal<>& Revealer::on_settled() {
    return impl_->settled;
}

bool Revealer::prefers_reduced_motion() const {
    return theme_requests_reduced_motion();
}

void Revealer::cancel_timer() {
    if (!impl_->timer.valid()) {
        return;
    }
    if (auto* loop = EventLoop::current()) {
        loop->cancel(impl_->timer);
    }
    impl_->timer = {};
}

void Revealer::start_animation() {
    cancel_timer();
    auto* loop = EventLoop::current();
    if (loop == nullptr) {
        impl_->fraction = impl_->revealed ? 1.0F : 0.0F;
        finish_reveal();
        return;
    }
    impl_->fraction_from = impl_->fraction;
    impl_->started = std::chrono::steady_clock::now();
    impl_->timer = loop->set_interval(
        kTick,
        [weak = weak_from_this()] {
            auto widget = std::static_pointer_cast<Revealer>(weak.lock());
            if (widget == nullptr) {
                return;
            }
            const float elapsed = std::chrono::duration<float, std::milli>(
                                      std::chrono::steady_clock::now() - widget->impl_->started)
                                      .count();
            const float t = std::clamp(elapsed / static_cast<float>(kDuration.count()), 0.0F, 1.0F);
            const float target = widget->impl_->revealed ? 1.0F : 0.0F;
            widget->impl_->fraction =
                widget->impl_->fraction_from + ((target - widget->impl_->fraction_from) * t);
            widget->queue_layout();
            widget->queue_redraw();
            if (t >= 1.0F) {
                widget->impl_->fraction = target;
                widget->cancel_timer();
                widget->finish_reveal();
            }
        },
        "revealer");
}

void Revealer::finish_reveal() {
    if (impl_->child) {
        impl_->child->set_visible(impl_->fraction > 0.0F);
    }
    queue_layout();
    queue_redraw();
    impl_->settled.emit();
}

SizeRequest Revealer::measure(const Constraints& constraints) const {
    if (impl_->child == nullptr) {
        return {};
    }
    auto loose = constraints;
    loose.max_height = std::numeric_limits<float>::infinity();
    const auto request = impl_->child->measure_for_diagnostics(loose);
    const float shown = request.natural_height * std::clamp(impl_->fraction, 0.0F, 1.0F);
    return {request.minimum_width, 0.0F, request.natural_width, shown};
}

void Revealer::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->child && impl_->child->is_visible()) {
        impl_->child->allocate(allocation);
    }
}

bool Revealer::hit_test(Point point) const {
    return impl_->fraction > 0.0F && Widget::hit_test(point);
}

} // namespace nk
