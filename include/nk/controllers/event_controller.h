#pragma once

/// @file event_controller.h
/// @brief Base class for input event controllers attached to widgets.

#include <memory>
#include <nk/foundation/signal.h>

namespace nk {

class Widget;

/// Base class for event controllers. Controllers are attached to widgets
/// via Widget::add_controller() and receive input events dispatched by the
/// window during the normal mouse/keyboard/focus routing pipeline. This
/// follows the GTK4 pattern of separating event handling from widget
/// subclassing, so applications can observe input on any widget without
/// needing to subclass it.
///
/// Built-in controllers: PointerController, KeyboardController,
/// FocusController.
class EventController {
public:
    virtual ~EventController();

    EventController(const EventController&) = delete;
    EventController& operator=(const EventController&) = delete;

    /// The widget this controller is attached to (nullptr if detached).
    [[nodiscard]] Widget* widget() const;

protected:
    EventController();

private:
    friend class Widget;
    void set_widget(Widget* w);

    Widget* widget_ = nullptr;
};

/// Pointer (mouse/touch) event controller.
class PointerController : public EventController {
public:
    PointerController();
    ~PointerController() override;

    Signal<float, float>& on_enter();
    Signal<>& on_leave();
    Signal<float, float>& on_motion();
    Signal<float, float, int>& on_pressed();
    Signal<float, float, int>& on_released();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Keyboard event controller.
class KeyboardController : public EventController {
public:
    KeyboardController();
    ~KeyboardController() override;

    /// key_code, modifiers
    Signal<int, int>& on_key_pressed();
    Signal<int, int>& on_key_released();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Focus event controller.
class FocusController : public EventController {
public:
    FocusController();
    ~FocusController() override;

    Signal<>& on_focus_in();
    Signal<>& on_focus_out();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
