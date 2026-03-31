/// @file wayland_input.cpp
/// @brief Wayland seat / pointer / keyboard input implementation.

#include "wayland_input.h"
#include "wayland_backend.h"
#include "wayland_surface.h"

#include <nk/foundation/logging.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

namespace nk {

// ---------------------------------------------------------------------------
// XKB keysym → nk::KeyCode
// ---------------------------------------------------------------------------

static KeyCode xkb_keysym_to_nk(xkb_keysym_t sym) {
    // Lowercase letters.
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
        return static_cast<KeyCode>(
            static_cast<uint16_t>(KeyCode::A) + (sym - XKB_KEY_a));
    }
    // Uppercase letters.
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
        return static_cast<KeyCode>(
            static_cast<uint16_t>(KeyCode::A) + (sym - XKB_KEY_A));
    }

    switch (sym) {
    // Numbers
    case XKB_KEY_1:             return KeyCode::Num1;
    case XKB_KEY_2:             return KeyCode::Num2;
    case XKB_KEY_3:             return KeyCode::Num3;
    case XKB_KEY_4:             return KeyCode::Num4;
    case XKB_KEY_5:             return KeyCode::Num5;
    case XKB_KEY_6:             return KeyCode::Num6;
    case XKB_KEY_7:             return KeyCode::Num7;
    case XKB_KEY_8:             return KeyCode::Num8;
    case XKB_KEY_9:             return KeyCode::Num9;
    case XKB_KEY_0:             return KeyCode::Num0;

    // Control
    case XKB_KEY_Return:        return KeyCode::Return;
    case XKB_KEY_Escape:        return KeyCode::Escape;
    case XKB_KEY_BackSpace:     return KeyCode::Backspace;
    case XKB_KEY_Tab:           return KeyCode::Tab;
    case XKB_KEY_space:         return KeyCode::Space;

    // Punctuation
    case XKB_KEY_minus:         return KeyCode::Minus;
    case XKB_KEY_equal:         return KeyCode::Equals;
    case XKB_KEY_bracketleft:   return KeyCode::LeftBracket;
    case XKB_KEY_bracketright:  return KeyCode::RightBracket;
    case XKB_KEY_backslash:     return KeyCode::Backslash;
    case XKB_KEY_semicolon:     return KeyCode::Semicolon;
    case XKB_KEY_apostrophe:    return KeyCode::Apostrophe;
    case XKB_KEY_grave:         return KeyCode::Grave;
    case XKB_KEY_comma:         return KeyCode::Comma;
    case XKB_KEY_period:        return KeyCode::Period;
    case XKB_KEY_slash:         return KeyCode::Slash;

    case XKB_KEY_Caps_Lock:     return KeyCode::CapsLock;

    // Function keys
    case XKB_KEY_F1:            return KeyCode::F1;
    case XKB_KEY_F2:            return KeyCode::F2;
    case XKB_KEY_F3:            return KeyCode::F3;
    case XKB_KEY_F4:            return KeyCode::F4;
    case XKB_KEY_F5:            return KeyCode::F5;
    case XKB_KEY_F6:            return KeyCode::F6;
    case XKB_KEY_F7:            return KeyCode::F7;
    case XKB_KEY_F8:            return KeyCode::F8;
    case XKB_KEY_F9:            return KeyCode::F9;
    case XKB_KEY_F10:           return KeyCode::F10;
    case XKB_KEY_F11:           return KeyCode::F11;
    case XKB_KEY_F12:           return KeyCode::F12;

    // Navigation
    case XKB_KEY_Print:         return KeyCode::PrintScreen;
    case XKB_KEY_Scroll_Lock:   return KeyCode::ScrollLock;
    case XKB_KEY_Pause:         return KeyCode::Pause;
    case XKB_KEY_Insert:        return KeyCode::Insert;
    case XKB_KEY_Home:          return KeyCode::Home;
    case XKB_KEY_Page_Up:       return KeyCode::PageUp;
    case XKB_KEY_Delete:        return KeyCode::Delete;
    case XKB_KEY_End:           return KeyCode::End;
    case XKB_KEY_Page_Down:     return KeyCode::PageDown;

    // Arrows
    case XKB_KEY_Right:         return KeyCode::Right;
    case XKB_KEY_Left:          return KeyCode::Left;
    case XKB_KEY_Down:          return KeyCode::Down;
    case XKB_KEY_Up:            return KeyCode::Up;

    // Numpad
    case XKB_KEY_Num_Lock:      return KeyCode::NumLock;
    case XKB_KEY_KP_Divide:     return KeyCode::NumpadDivide;
    case XKB_KEY_KP_Multiply:   return KeyCode::NumpadMultiply;
    case XKB_KEY_KP_Subtract:   return KeyCode::NumpadMinus;
    case XKB_KEY_KP_Add:        return KeyCode::NumpadPlus;
    case XKB_KEY_KP_Enter:      return KeyCode::NumpadEnter;
    case XKB_KEY_KP_1:          return KeyCode::Numpad1;
    case XKB_KEY_KP_2:          return KeyCode::Numpad2;
    case XKB_KEY_KP_3:          return KeyCode::Numpad3;
    case XKB_KEY_KP_4:          return KeyCode::Numpad4;
    case XKB_KEY_KP_5:          return KeyCode::Numpad5;
    case XKB_KEY_KP_6:          return KeyCode::Numpad6;
    case XKB_KEY_KP_7:          return KeyCode::Numpad7;
    case XKB_KEY_KP_8:          return KeyCode::Numpad8;
    case XKB_KEY_KP_9:          return KeyCode::Numpad9;
    case XKB_KEY_KP_0:          return KeyCode::Numpad0;
    case XKB_KEY_KP_Decimal:    return KeyCode::NumpadPeriod;

    // Modifiers
    case XKB_KEY_Control_L:     return KeyCode::LeftCtrl;
    case XKB_KEY_Shift_L:       return KeyCode::LeftShift;
    case XKB_KEY_Alt_L:         return KeyCode::LeftAlt;
    case XKB_KEY_Super_L:       return KeyCode::LeftSuper;
    case XKB_KEY_Control_R:     return KeyCode::RightCtrl;
    case XKB_KEY_Shift_R:       return KeyCode::RightShift;
    case XKB_KEY_Alt_R:         return KeyCode::RightAlt;
    case XKB_KEY_Super_R:       return KeyCode::RightSuper;

    default:                    return KeyCode::Unknown;
    }
}

// ---------------------------------------------------------------------------
// Wayland button → nk convention (1=left, 2=right, 3=middle)
// ---------------------------------------------------------------------------

static int wayland_button_to_nk(uint32_t button) {
    switch (button) {
    case BTN_LEFT:   return 1;
    case BTN_RIGHT:  return 2;
    case BTN_MIDDLE: return 3;
    default:         return static_cast<int>(button - BTN_LEFT + 1);
    }
}

// ---------------------------------------------------------------------------
// Listener structs
// ---------------------------------------------------------------------------

static constexpr struct wl_seat_listener seat_listener = {
    .capabilities = WaylandInput::seat_capabilities,
    .name = WaylandInput::seat_name,
};

static void pointer_frame(void* /*data*/, wl_pointer* /*pointer*/) {}
static void pointer_axis_source(void* /*data*/, wl_pointer* /*pointer*/,
                                 uint32_t /*source*/) {}
static void pointer_axis_stop(void* /*data*/, wl_pointer* /*pointer*/,
                               uint32_t /*time*/, uint32_t /*axis*/) {}
static void pointer_axis_discrete(void* /*data*/, wl_pointer* /*pointer*/,
                                   uint32_t /*axis*/, int32_t /*discrete*/) {}
static void pointer_axis_value120(void* /*data*/, wl_pointer* /*pointer*/,
                                   uint32_t /*axis*/, int32_t /*value120*/) {}
static void pointer_axis_relative_direction(void* /*data*/,
                                             wl_pointer* /*pointer*/,
                                             uint32_t /*axis*/,
                                             uint32_t /*direction*/) {}

static constexpr struct wl_pointer_listener pointer_listener = {
    .enter = WaylandInput::pointer_enter,
    .leave = WaylandInput::pointer_leave,
    .motion = WaylandInput::pointer_motion,
    .button = WaylandInput::pointer_button,
    .axis = WaylandInput::pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_relative_direction,
};

static void keyboard_repeat_info(void* /*data*/, wl_keyboard* /*keyboard*/,
                                  int32_t /*rate*/, int32_t /*delay*/) {
    // TODO: implement client-side key repeat using rate/delay.
}

static constexpr struct wl_keyboard_listener keyboard_listener = {
    .keymap = WaylandInput::keyboard_keymap,
    .enter = WaylandInput::keyboard_enter,
    .leave = WaylandInput::keyboard_leave,
    .key = WaylandInput::keyboard_key,
    .modifiers = WaylandInput::keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

// ---------------------------------------------------------------------------
// WaylandInput
// ---------------------------------------------------------------------------

WaylandInput::WaylandInput(WaylandBackend& backend, wl_seat* seat)
    : backend_(backend)
    , seat_(seat) {
    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    wl_seat_add_listener(seat_, &seat_listener, this);
}

WaylandInput::~WaylandInput() {
    if (xkb_state_)   xkb_state_unref(xkb_state_);
    if (xkb_keymap_)  xkb_keymap_unref(xkb_keymap_);
    if (xkb_ctx_)     xkb_context_unref(xkb_ctx_);
    if (keyboard_)    wl_keyboard_destroy(keyboard_);
    if (pointer_)     wl_pointer_destroy(pointer_);
}

// ---------------------------------------------------------------------------
// Seat callbacks
// ---------------------------------------------------------------------------

void WaylandInput::seat_capabilities(void* data, wl_seat* /*seat*/,
                                      uint32_t caps) {
    auto* self = static_cast<WaylandInput*>(data);

    bool const has_pointer =
        (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
    bool const has_keyboard =
        (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

    if (has_pointer && !self->pointer_) {
        self->pointer_ = wl_seat_get_pointer(self->seat_);
        wl_pointer_add_listener(self->pointer_, &pointer_listener, self);
    } else if (!has_pointer && self->pointer_) {
        wl_pointer_destroy(self->pointer_);
        self->pointer_ = nullptr;
    }

    if (has_keyboard && !self->keyboard_) {
        self->keyboard_ = wl_seat_get_keyboard(self->seat_);
        wl_keyboard_add_listener(self->keyboard_, &keyboard_listener, self);
    } else if (!has_keyboard && self->keyboard_) {
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
    }
}

void WaylandInput::seat_name(void* /*data*/, wl_seat* /*seat*/,
                              char const* /*name*/) {
    // Informational — no action needed.
}

// ---------------------------------------------------------------------------
// Pointer callbacks
// ---------------------------------------------------------------------------

void WaylandInput::pointer_enter(void* data, wl_pointer* /*pointer*/,
                                  uint32_t /*serial*/,
                                  wl_surface* surface,
                                  wl_fixed_t x, wl_fixed_t y) {
    auto* self = static_cast<WaylandInput*>(data);
    self->pointer_focus_ = self->backend_.find_surface(surface);
    self->pointer_x_ = static_cast<float>(wl_fixed_to_double(x));
    self->pointer_y_ = static_cast<float>(wl_fixed_to_double(y));

    if (self->pointer_focus_) {
        MouseEvent me{};
        me.type = MouseEvent::Type::Enter;
        me.x = self->pointer_x_;
        me.y = self->pointer_y_;
        self->pointer_focus_->owner().dispatch_mouse_event(me);
    }
}

void WaylandInput::pointer_leave(void* data, wl_pointer* /*pointer*/,
                                  uint32_t /*serial*/,
                                  wl_surface* /*surface*/) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->pointer_focus_) {
        MouseEvent me{};
        me.type = MouseEvent::Type::Leave;
        me.x = self->pointer_x_;
        me.y = self->pointer_y_;
        self->pointer_focus_->owner().dispatch_mouse_event(me);
    }
    self->pointer_focus_ = nullptr;
}

void WaylandInput::pointer_motion(void* data, wl_pointer* /*pointer*/,
                                   uint32_t /*time*/,
                                   wl_fixed_t x, wl_fixed_t y) {
    auto* self = static_cast<WaylandInput*>(data);
    self->pointer_x_ = static_cast<float>(wl_fixed_to_double(x));
    self->pointer_y_ = static_cast<float>(wl_fixed_to_double(y));

    if (self->pointer_focus_) {
        MouseEvent me{};
        me.type = MouseEvent::Type::Move;
        me.x = self->pointer_x_;
        me.y = self->pointer_y_;
        self->pointer_focus_->owner().dispatch_mouse_event(me);
    }
}

void WaylandInput::pointer_button(void* data, wl_pointer* /*pointer*/,
                                   uint32_t /*serial*/, uint32_t /*time*/,
                                   uint32_t button, uint32_t state) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self->pointer_focus_) {
        return;
    }

    MouseEvent me{};
    me.type = (state == WL_POINTER_BUTTON_STATE_PRESSED)
                  ? MouseEvent::Type::Press
                  : MouseEvent::Type::Release;
    me.x = self->pointer_x_;
    me.y = self->pointer_y_;
    me.button = wayland_button_to_nk(button);
    self->pointer_focus_->owner().dispatch_mouse_event(me);
}

void WaylandInput::pointer_axis(void* data, wl_pointer* /*pointer*/,
                                 uint32_t /*time*/,
                                 uint32_t axis, wl_fixed_t value) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self->pointer_focus_) {
        return;
    }

    MouseEvent me{};
    me.type = MouseEvent::Type::Scroll;
    me.x = self->pointer_x_;
    me.y = self->pointer_y_;

    auto const delta = static_cast<float>(wl_fixed_to_double(value));
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        me.scroll_dy = -delta; // Wayland positive = scroll down.
    } else {
        me.scroll_dx = delta;
    }

    self->pointer_focus_->owner().dispatch_mouse_event(me);
}

// ---------------------------------------------------------------------------
// Keyboard callbacks
// ---------------------------------------------------------------------------

void WaylandInput::keyboard_keymap(void* data, wl_keyboard* /*keyboard*/,
                                    uint32_t format, int fd, uint32_t size) {
    auto* self = static_cast<WaylandInput*>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    auto* map_str = static_cast<char*>(
        mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);

    if (map_str == MAP_FAILED) {
        NK_LOG_ERROR("Wayland", "Failed to mmap keymap");
        return;
    }

    if (self->xkb_state_) {
        xkb_state_unref(self->xkb_state_);
        self->xkb_state_ = nullptr;
    }
    if (self->xkb_keymap_) {
        xkb_keymap_unref(self->xkb_keymap_);
        self->xkb_keymap_ = nullptr;
    }

    self->xkb_keymap_ = xkb_keymap_new_from_string(
        self->xkb_ctx_, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);

    if (self->xkb_keymap_) {
        self->xkb_state_ = xkb_state_new(self->xkb_keymap_);
    }
}

void WaylandInput::keyboard_enter(void* data, wl_keyboard* /*keyboard*/,
                                   uint32_t /*serial*/,
                                   wl_surface* surface,
                                   wl_array* /*keys*/) {
    auto* self = static_cast<WaylandInput*>(data);
    self->keyboard_focus_ = self->backend_.find_surface(surface);

    if (self->keyboard_focus_) {
        WindowEvent we{};
        we.type = WindowEvent::Type::FocusIn;
        self->keyboard_focus_->owner().dispatch_window_event(we);
    }
}

void WaylandInput::keyboard_leave(void* data, wl_keyboard* /*keyboard*/,
                                   uint32_t /*serial*/,
                                   wl_surface* /*surface*/) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->keyboard_focus_) {
        WindowEvent we{};
        we.type = WindowEvent::Type::FocusOut;
        self->keyboard_focus_->owner().dispatch_window_event(we);
    }
    self->keyboard_focus_ = nullptr;
}

void WaylandInput::keyboard_key(void* data, wl_keyboard* /*keyboard*/,
                                 uint32_t /*serial*/, uint32_t /*time*/,
                                 uint32_t key, uint32_t state) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self->keyboard_focus_ || !self->xkb_state_) {
        return;
    }

    // Wayland key codes are evdev codes; xkbcommon expects evdev + 8.
    xkb_keysym_t const sym =
        xkb_state_key_get_one_sym(self->xkb_state_, key + 8);

    KeyEvent ke{};
    ke.type = (state == WL_KEYBOARD_KEY_STATE_PRESSED)
                  ? KeyEvent::Type::Press
                  : KeyEvent::Type::Release;
    ke.key = xkb_keysym_to_nk(sym);
    ke.is_repeat = false;

    // Query modifier state from xkbcommon.
    Modifiers mods = Modifiers::None;
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_SHIFT,
                                      XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Shift;
    }
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_CTRL,
                                      XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Ctrl;
    }
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_ALT,
                                      XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Alt;
    }
    if (xkb_state_mod_name_is_active(self->xkb_state_, XKB_MOD_NAME_LOGO,
                                      XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Super;
    }
    ke.modifiers = mods;

    self->keyboard_focus_->owner().dispatch_key_event(ke);
}

void WaylandInput::keyboard_modifiers(void* data,
                                       wl_keyboard* /*keyboard*/,
                                       uint32_t /*serial*/,
                                       uint32_t mods_depressed,
                                       uint32_t mods_latched,
                                       uint32_t mods_locked,
                                       uint32_t group) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->xkb_state_) {
        xkb_state_update_mask(self->xkb_state_,
                              mods_depressed, mods_latched, mods_locked,
                              0, 0, group);
    }
}

} // namespace nk
