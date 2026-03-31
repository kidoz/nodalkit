#pragma once

/// @file event_controller.h
/// @brief Base class for input event controllers attached to widgets.

#include <nk/foundation/signal.h>

#include <memory>

namespace nk {

class Widget;

/// Base class for event controllers. Controllers are attached to widgets
/// and receive input events. This follows the GTK4 pattern of separating
/// event handling from widget subclassing.
///
/// Built-in controllers: PointerController, KeyboardController,
/// FocusController, ShortcutController, GestureController.
class EventController {
public:
    virtual ~EventController();

    EventController(EventController const&) = delete;
    EventController& operator=(EventController const&) = delete;

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

    Signal<>& on_focus_in();
    Signal<>& on_focus_out();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
