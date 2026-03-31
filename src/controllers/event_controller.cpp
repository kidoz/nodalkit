#include <nk/controllers/event_controller.h>

namespace nk {

EventController::EventController() = default;
EventController::~EventController() = default;

Widget* EventController::widget() const { return widget_; }
void EventController::set_widget(Widget* w) { widget_ = w; }

// --- PointerController ---

struct PointerController::Impl {
    Signal<float, float> on_enter;
    Signal<> on_leave;
    Signal<float, float> on_motion;
    Signal<float, float, int> on_pressed;
    Signal<float, float, int> on_released;
};

PointerController::PointerController()
    : impl_(std::make_unique<Impl>()) {}

Signal<float, float>& PointerController::on_enter() {
    return impl_->on_enter;
}
Signal<>& PointerController::on_leave() { return impl_->on_leave; }
Signal<float, float>& PointerController::on_motion() {
    return impl_->on_motion;
}
Signal<float, float, int>& PointerController::on_pressed() {
    return impl_->on_pressed;
}
Signal<float, float, int>& PointerController::on_released() {
    return impl_->on_released;
}

// --- KeyboardController ---

struct KeyboardController::Impl {
    Signal<int, int> on_key_pressed;
    Signal<int, int> on_key_released;
};

KeyboardController::KeyboardController()
    : impl_(std::make_unique<Impl>()) {}

Signal<int, int>& KeyboardController::on_key_pressed() {
    return impl_->on_key_pressed;
}
Signal<int, int>& KeyboardController::on_key_released() {
    return impl_->on_key_released;
}

// --- FocusController ---

struct FocusController::Impl {
    Signal<> on_focus_in;
    Signal<> on_focus_out;
};

FocusController::FocusController()
    : impl_(std::make_unique<Impl>()) {}

Signal<>& FocusController::on_focus_in() { return impl_->on_focus_in; }
Signal<>& FocusController::on_focus_out() { return impl_->on_focus_out; }

} // namespace nk
