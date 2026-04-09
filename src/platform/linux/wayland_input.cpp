/// @file wayland_input.cpp
/// @brief Wayland seat / pointer / keyboard input implementation.

#include "wayland_input.h"

#include "primary-selection-unstable-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "wayland_backend.h"
#include "wayland_surface.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <nk/foundation/logging.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

namespace nk {

// ---------------------------------------------------------------------------
// XKB keysym → nk::KeyCode
// ---------------------------------------------------------------------------

static KeyCode xkb_keysym_to_nk(xkb_keysym_t sym) {
    // Lowercase letters.
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
        return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (sym - XKB_KEY_a));
    }
    // Uppercase letters.
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
        return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (sym - XKB_KEY_A));
    }

    switch (sym) {
    // Numbers
    case XKB_KEY_1:
        return KeyCode::Num1;
    case XKB_KEY_2:
        return KeyCode::Num2;
    case XKB_KEY_3:
        return KeyCode::Num3;
    case XKB_KEY_4:
        return KeyCode::Num4;
    case XKB_KEY_5:
        return KeyCode::Num5;
    case XKB_KEY_6:
        return KeyCode::Num6;
    case XKB_KEY_7:
        return KeyCode::Num7;
    case XKB_KEY_8:
        return KeyCode::Num8;
    case XKB_KEY_9:
        return KeyCode::Num9;
    case XKB_KEY_0:
        return KeyCode::Num0;

    // Control
    case XKB_KEY_Return:
        return KeyCode::Return;
    case XKB_KEY_Escape:
        return KeyCode::Escape;
    case XKB_KEY_BackSpace:
        return KeyCode::Backspace;
    case XKB_KEY_Tab:
        return KeyCode::Tab;
    case XKB_KEY_space:
        return KeyCode::Space;

    // Punctuation
    case XKB_KEY_minus:
        return KeyCode::Minus;
    case XKB_KEY_equal:
        return KeyCode::Equals;
    case XKB_KEY_bracketleft:
        return KeyCode::LeftBracket;
    case XKB_KEY_bracketright:
        return KeyCode::RightBracket;
    case XKB_KEY_backslash:
        return KeyCode::Backslash;
    case XKB_KEY_semicolon:
        return KeyCode::Semicolon;
    case XKB_KEY_apostrophe:
        return KeyCode::Apostrophe;
    case XKB_KEY_grave:
        return KeyCode::Grave;
    case XKB_KEY_comma:
        return KeyCode::Comma;
    case XKB_KEY_period:
        return KeyCode::Period;
    case XKB_KEY_slash:
        return KeyCode::Slash;

    case XKB_KEY_Caps_Lock:
        return KeyCode::CapsLock;

    // Function keys
    case XKB_KEY_F1:
        return KeyCode::F1;
    case XKB_KEY_F2:
        return KeyCode::F2;
    case XKB_KEY_F3:
        return KeyCode::F3;
    case XKB_KEY_F4:
        return KeyCode::F4;
    case XKB_KEY_F5:
        return KeyCode::F5;
    case XKB_KEY_F6:
        return KeyCode::F6;
    case XKB_KEY_F7:
        return KeyCode::F7;
    case XKB_KEY_F8:
        return KeyCode::F8;
    case XKB_KEY_F9:
        return KeyCode::F9;
    case XKB_KEY_F10:
        return KeyCode::F10;
    case XKB_KEY_F11:
        return KeyCode::F11;
    case XKB_KEY_F12:
        return KeyCode::F12;

    // Navigation
    case XKB_KEY_Print:
        return KeyCode::PrintScreen;
    case XKB_KEY_Scroll_Lock:
        return KeyCode::ScrollLock;
    case XKB_KEY_Pause:
        return KeyCode::Pause;
    case XKB_KEY_Insert:
        return KeyCode::Insert;
    case XKB_KEY_Home:
        return KeyCode::Home;
    case XKB_KEY_Page_Up:
        return KeyCode::PageUp;
    case XKB_KEY_Delete:
        return KeyCode::Delete;
    case XKB_KEY_End:
        return KeyCode::End;
    case XKB_KEY_Page_Down:
        return KeyCode::PageDown;

    // Arrows
    case XKB_KEY_Right:
        return KeyCode::Right;
    case XKB_KEY_Left:
        return KeyCode::Left;
    case XKB_KEY_Down:
        return KeyCode::Down;
    case XKB_KEY_Up:
        return KeyCode::Up;

    // Numpad
    case XKB_KEY_Num_Lock:
        return KeyCode::NumLock;
    case XKB_KEY_KP_Divide:
        return KeyCode::NumpadDivide;
    case XKB_KEY_KP_Multiply:
        return KeyCode::NumpadMultiply;
    case XKB_KEY_KP_Subtract:
        return KeyCode::NumpadMinus;
    case XKB_KEY_KP_Add:
        return KeyCode::NumpadPlus;
    case XKB_KEY_KP_Enter:
        return KeyCode::NumpadEnter;
    case XKB_KEY_KP_1:
        return KeyCode::Numpad1;
    case XKB_KEY_KP_2:
        return KeyCode::Numpad2;
    case XKB_KEY_KP_3:
        return KeyCode::Numpad3;
    case XKB_KEY_KP_4:
        return KeyCode::Numpad4;
    case XKB_KEY_KP_5:
        return KeyCode::Numpad5;
    case XKB_KEY_KP_6:
        return KeyCode::Numpad6;
    case XKB_KEY_KP_7:
        return KeyCode::Numpad7;
    case XKB_KEY_KP_8:
        return KeyCode::Numpad8;
    case XKB_KEY_KP_9:
        return KeyCode::Numpad9;
    case XKB_KEY_KP_0:
        return KeyCode::Numpad0;
    case XKB_KEY_KP_Decimal:
        return KeyCode::NumpadPeriod;

    // Modifiers
    case XKB_KEY_Control_L:
        return KeyCode::LeftCtrl;
    case XKB_KEY_Shift_L:
        return KeyCode::LeftShift;
    case XKB_KEY_Alt_L:
        return KeyCode::LeftAlt;
    case XKB_KEY_Super_L:
        return KeyCode::LeftSuper;
    case XKB_KEY_Control_R:
        return KeyCode::RightCtrl;
    case XKB_KEY_Shift_R:
        return KeyCode::RightShift;
    case XKB_KEY_Alt_R:
        return KeyCode::RightAlt;
    case XKB_KEY_Super_R:
        return KeyCode::RightSuper;

    default:
        return KeyCode::Unknown;
    }
}

// ---------------------------------------------------------------------------
// Wayland button → nk convention (1=left, 2=right, 3=middle)
// ---------------------------------------------------------------------------

static int wayland_button_to_nk(uint32_t button) {
    switch (button) {
    case BTN_LEFT:
        return 1;
    case BTN_RIGHT:
        return 2;
    case BTN_MIDDLE:
        return 3;
    default:
        return static_cast<int>(button - BTN_LEFT + 1);
    }
}

static const char* wayland_input_locale() {
    if (const char* value = std::getenv("LC_ALL"); value != nullptr && value[0] != '\0') {
        return value;
    }
    if (const char* value = std::getenv("LC_CTYPE"); value != nullptr && value[0] != '\0') {
        return value;
    }
    if (const char* value = std::getenv("LANG"); value != nullptr && value[0] != '\0') {
        return value;
    }
    return "C";
}

static Modifiers wayland_effective_modifiers(xkb_state* state) {
    Modifiers mods = Modifiers::None;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Shift;
    }
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Ctrl;
    }
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Alt;
    }
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
        mods = mods | Modifiers::Super;
    }
    return mods;
}

static bool utf8_contains_control(std::string_view text) {
    for (unsigned char byte : text) {
        if ((byte < 0x20U && byte != '\t') || byte == 0x7FU) {
            return true;
        }
    }
    return false;
}

static std::string xkb_state_key_utf8(xkb_state* state, uint32_t keycode) {
    const int size = xkb_state_key_get_utf8(state, keycode, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    xkb_state_key_get_utf8(state, keycode, text.data(), text.size() + 1);
    if (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

static std::string xkb_compose_state_utf8(xkb_compose_state* state) {
    const int size = xkb_compose_state_get_utf8(state, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    xkb_compose_state_get_utf8(state, text.data(), text.size() + 1);
    if (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

// ---------------------------------------------------------------------------
// Listener structs
// ---------------------------------------------------------------------------

static constexpr struct wl_seat_listener seat_listener = {
    .capabilities = WaylandInput::seat_capabilities,
    .name = WaylandInput::seat_name,
};

static void pointer_frame(void* /*data*/, wl_pointer* /*pointer*/) {}

static void pointer_axis_source(void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*source*/) {}

static void
pointer_axis_stop(void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*time*/, uint32_t /*axis*/) {}

static void pointer_axis_discrete(void* /*data*/,
                                  wl_pointer* /*pointer*/,
                                  uint32_t /*axis*/,
                                  int32_t /*discrete*/) {}

static void pointer_axis_value120(void* /*data*/,
                                  wl_pointer* /*pointer*/,
                                  uint32_t /*axis*/,
                                  int32_t /*value120*/) {}

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

void WaylandInput::keyboard_repeat_info(void* data,
                                        wl_keyboard* /*keyboard*/,
                                        int32_t rate,
                                        int32_t delay) {
    auto* self = static_cast<WaylandInput*>(data);
    if (rate <= 0) {
        self->key_repeat_interval_ = std::chrono::milliseconds{0};
        self->stop_key_repeat();
    } else {
        self->key_repeat_interval_ =
            std::chrono::milliseconds{std::max(1, static_cast<int>(1000 / rate))};
    }
    self->key_repeat_delay_ = std::chrono::milliseconds{std::max(0, delay)};
}

static constexpr struct wl_keyboard_listener keyboard_listener = {
    .keymap = WaylandInput::keyboard_keymap,
    .enter = WaylandInput::keyboard_enter,
    .leave = WaylandInput::keyboard_leave,
    .key = WaylandInput::keyboard_key,
    .modifiers = WaylandInput::keyboard_modifiers,
    .repeat_info = WaylandInput::keyboard_repeat_info,
};

static constexpr struct zwp_text_input_v3_listener text_input_listener = {
    .enter = WaylandInput::text_input_enter,
    .leave = WaylandInput::text_input_leave,
    .preedit_string = WaylandInput::text_input_preedit_string,
    .commit_string = WaylandInput::text_input_commit_string,
    .delete_surrounding_text = WaylandInput::text_input_delete_surrounding_text,
    .done = WaylandInput::text_input_done,
    .action = nullptr,
    .language = nullptr,
    .preedit_hint = nullptr,
};

static constexpr const char* PrimarySelectionMimeTextUtf8 = "text/plain;charset=utf-8";
static constexpr const char* PrimarySelectionMimeTextPlain = "text/plain";
static constexpr const char* PrimarySelectionMimeUtf8String = "UTF8_STRING";
static constexpr const char* ClipboardMimeTextUtf8 = "text/plain;charset=utf-8";
static constexpr const char* ClipboardMimeTextPlain = "text/plain";
static constexpr const char* ClipboardMimeUtf8String = "UTF8_STRING";

static constexpr struct zwp_primary_selection_device_v1_listener primary_selection_device_listener =
    {
        .data_offer = WaylandInput::primary_selection_data_offer,
        .selection = WaylandInput::primary_selection_selection,
};

static constexpr struct zwp_primary_selection_offer_v1_listener primary_selection_offer_listener = {
    .offer = WaylandInput::primary_selection_offer_offer,
};

static constexpr struct zwp_primary_selection_source_v1_listener primary_selection_source_listener =
    {
        .send = WaylandInput::primary_selection_source_send,
        .cancelled = WaylandInput::primary_selection_source_cancelled,
};

static constexpr struct wl_data_device_listener clipboard_device_listener = {
    .data_offer = WaylandInput::clipboard_data_offer,
    .enter = WaylandInput::clipboard_enter,
    .leave = WaylandInput::clipboard_leave,
    .motion = WaylandInput::clipboard_motion,
    .drop = WaylandInput::clipboard_drop,
    .selection = WaylandInput::clipboard_selection,
};

static constexpr struct wl_data_offer_listener clipboard_offer_listener = {
    .offer = WaylandInput::clipboard_offer_offer,
    .source_actions = WaylandInput::clipboard_offer_source_actions,
    .action = WaylandInput::clipboard_offer_action,
};

static constexpr struct wl_data_source_listener clipboard_source_listener = {
    .target = WaylandInput::clipboard_source_target,
    .send = WaylandInput::clipboard_source_send,
    .cancelled = WaylandInput::clipboard_source_cancelled,
    .dnd_drop_performed = WaylandInput::clipboard_source_dnd_drop_performed,
    .dnd_finished = WaylandInput::clipboard_source_dnd_finished,
    .action = WaylandInput::clipboard_source_action,
};

// ---------------------------------------------------------------------------
// WaylandInput
// ---------------------------------------------------------------------------

WaylandInput::WaylandInput(WaylandBackend& backend, wl_seat* seat)
    : backend_(backend), seat_(seat) {
    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (xkb_ctx_ != nullptr) {
        xkb_compose_table_ = xkb_compose_table_new_from_locale(
            xkb_ctx_, wayland_input_locale(), XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (xkb_compose_table_ != nullptr) {
            xkb_compose_state_ =
                xkb_compose_state_new(xkb_compose_table_, XKB_COMPOSE_STATE_NO_FLAGS);
        }
    }
    if (backend_.text_input_manager() != nullptr) {
        text_input_ =
            zwp_text_input_manager_v3_get_text_input(backend_.text_input_manager(), seat_);
        if (text_input_ != nullptr) {
            zwp_text_input_v3_add_listener(text_input_, &text_input_listener, this);
        }
    }
    if (backend_.data_device_manager() != nullptr) {
        clipboard_device_ =
            wl_data_device_manager_get_data_device(backend_.data_device_manager(), seat_);
        if (clipboard_device_ != nullptr) {
            wl_data_device_add_listener(clipboard_device_, &clipboard_device_listener, this);
        }
    }
    if (backend_.primary_selection_manager() != nullptr) {
        primary_selection_device_ = zwp_primary_selection_device_manager_v1_get_device(
            backend_.primary_selection_manager(), seat_);
        if (primary_selection_device_ != nullptr) {
            zwp_primary_selection_device_v1_add_listener(
                primary_selection_device_, &primary_selection_device_listener, this);
        }
    }
    wl_seat_add_listener(seat_, &seat_listener, this);
}

WaylandInput::~WaylandInput() {
    stop_key_repeat();
    destroy_clipboard_offer();
    destroy_clipboard_source();
    if (clipboard_device_ != nullptr) {
        wl_data_device_destroy(clipboard_device_);
    }
    destroy_primary_selection_offer();
    destroy_primary_selection_source();
    if (primary_selection_device_ != nullptr) {
        zwp_primary_selection_device_v1_destroy(primary_selection_device_);
    }
    if (text_input_) {
        zwp_text_input_v3_destroy(text_input_);
    }
    if (xkb_compose_state_) {
        xkb_compose_state_unref(xkb_compose_state_);
    }
    if (xkb_compose_table_) {
        xkb_compose_table_unref(xkb_compose_table_);
    }
    if (xkb_state_) {
        xkb_state_unref(xkb_state_);
    }
    if (xkb_keymap_) {
        xkb_keymap_unref(xkb_keymap_);
    }
    if (xkb_ctx_) {
        xkb_context_unref(xkb_ctx_);
    }
    if (keyboard_) {
        wl_keyboard_destroy(keyboard_);
    }
    if (pointer_) {
        wl_pointer_destroy(pointer_);
    }
}

bool WaylandInput::can_repeat_key(uint32_t key) const {
    if (keyboard_focus_ == nullptr || xkb_keymap_ == nullptr || key_repeat_interval_.count() <= 0 ||
        key_repeat_delay_.count() < 0) {
        return false;
    }
    return xkb_keymap_key_repeats(xkb_keymap_, key + 8) != 0;
}

void WaylandInput::start_key_repeat(uint32_t key) {
    if (!can_repeat_key(key)) {
        return;
    }
    repeating_key_ = key;
    schedule_key_repeat(key_repeat_delay_);
}

void WaylandInput::stop_key_repeat() {
    if (key_repeat_handle_.valid()) {
        if (auto* app = Application::instance(); app != nullptr) {
            app->event_loop().cancel(key_repeat_handle_);
        }
        key_repeat_handle_ = {};
    }
    repeating_key_ = 0;
}

void WaylandInput::schedule_key_repeat(std::chrono::milliseconds delay) {
    if (repeating_key_ == 0) {
        return;
    }
    auto* app = Application::instance();
    if (app == nullptr) {
        return;
    }
    if (key_repeat_handle_.valid()) {
        app->event_loop().cancel(key_repeat_handle_);
    }
    key_repeat_handle_ = app->event_loop().set_timeout(
        delay,
        [this] {
            key_repeat_handle_ = {};
            if (repeating_key_ == 0 || keyboard_focus_ == nullptr || xkb_state_ == nullptr) {
                return;
            }
            dispatch_key_press(repeating_key_, true);
            if (repeating_key_ != 0 && keyboard_focus_ != nullptr) {
                schedule_key_repeat(key_repeat_interval_);
            }
        },
        "wayland.key-repeat");
}

void WaylandInput::dispatch_key_press(uint32_t key, bool is_repeat) {
    if (keyboard_focus_ == nullptr || xkb_state_ == nullptr) {
        return;
    }

    const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_, key + 8);
    auto& owner = keyboard_focus_->owner();

    KeyEvent ke{};
    ke.type = KeyEvent::Type::Press;
    ke.key = xkb_keysym_to_nk(sym);
    ke.is_repeat = is_repeat;
    const Modifiers mods = wayland_effective_modifiers(xkb_state_);
    ke.modifiers = mods;

    owner.dispatch_key_event(ke);
    sync_text_input_state();

    if (is_repeat) {
        if ((mods & Modifiers::Ctrl) == Modifiers::Ctrl ||
            (mods & Modifiers::Super) == Modifiers::Super) {
            return;
        }

        const auto committed = xkb_state_key_utf8(xkb_state_, key + 8);
        if (committed.empty() || utf8_contains_control(committed)) {
            return;
        }

        TextInputEvent te{};
        te.type = TextInputEvent::Type::Commit;
        te.text = committed;
        owner.dispatch_text_input_event(te);
        sync_text_input_state();
        return;
    }

    if (xkb_compose_state_ != nullptr) {
        if (xkb_compose_state_feed(xkb_compose_state_, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(xkb_compose_state_)) {
            case XKB_COMPOSE_COMPOSING: {
                const auto preedit = xkb_compose_state_utf8(xkb_compose_state_);
                TextInputEvent te{};
                te.type = preedit.empty() ? TextInputEvent::Type::ClearPreedit
                                          : TextInputEvent::Type::Preedit;
                te.text = preedit;
                te.selection_start = te.text.size();
                te.selection_end = te.text.size();
                owner.dispatch_text_input_event(te);
                return;
            }
            case XKB_COMPOSE_COMPOSED: {
                const auto committed = xkb_compose_state_utf8(xkb_compose_state_);
                xkb_compose_state_reset(xkb_compose_state_);
                if (!committed.empty() && !utf8_contains_control(committed) &&
                    (mods & Modifiers::Ctrl) != Modifiers::Ctrl &&
                    (mods & Modifiers::Super) != Modifiers::Super) {
                    TextInputEvent te{};
                    te.type = TextInputEvent::Type::Commit;
                    te.text = committed;
                    owner.dispatch_text_input_event(te);
                } else {
                    TextInputEvent te{};
                    te.type = TextInputEvent::Type::ClearPreedit;
                    owner.dispatch_text_input_event(te);
                }
                return;
            }
            case XKB_COMPOSE_CANCELLED: {
                xkb_compose_state_reset(xkb_compose_state_);
                TextInputEvent te{};
                te.type = TextInputEvent::Type::ClearPreedit;
                owner.dispatch_text_input_event(te);
                return;
            }
            case XKB_COMPOSE_NOTHING:
                break;
            }
        }
    }

    if ((mods & Modifiers::Ctrl) == Modifiers::Ctrl ||
        (mods & Modifiers::Super) == Modifiers::Super) {
        return;
    }

    const auto committed = xkb_state_key_utf8(xkb_state_, key + 8);
    if (committed.empty() || utf8_contains_control(committed)) {
        return;
    }

    TextInputEvent te{};
    te.type = TextInputEvent::Type::Commit;
    te.text = committed;
    owner.dispatch_text_input_event(te);
    sync_text_input_state();
}

std::string WaylandInput::clipboard_text() const {
    if (owns_clipboard_) {
        return clipboard_text_;
    }
    if (clipboard_offer_ != nullptr) {
        if (const auto offered_text = receive_clipboard_offer_text(); !offered_text.empty()) {
            return offered_text;
        }
    }
    return clipboard_text_;
}

void WaylandInput::set_clipboard_text(std::string text) {
    clipboard_text_ = std::move(text);
    if (clipboard_device_ == nullptr) {
        owns_clipboard_ = !clipboard_text_.empty();
        return;
    }

    destroy_clipboard_source();
    if (clipboard_text_.empty()) {
        owns_clipboard_ = false;
        if (last_input_serial_ != 0) {
            wl_data_device_set_selection(clipboard_device_, nullptr, last_input_serial_);
            wl_display_flush(backend_.display());
        }
        return;
    }

    clipboard_source_ = wl_data_device_manager_create_data_source(backend_.data_device_manager());
    if (clipboard_source_ == nullptr) {
        owns_clipboard_ = true;
        return;
    }

    wl_data_source_add_listener(clipboard_source_, &clipboard_source_listener, this);
    wl_data_source_offer(clipboard_source_, ClipboardMimeTextUtf8);
    wl_data_source_offer(clipboard_source_, ClipboardMimeTextPlain);
    wl_data_source_offer(clipboard_source_, ClipboardMimeUtf8String);
    owns_clipboard_ = true;
    if (last_input_serial_ != 0) {
        wl_data_device_set_selection(clipboard_device_, clipboard_source_, last_input_serial_);
        wl_display_flush(backend_.display());
    }
}

void WaylandInput::destroy_clipboard_offer() {
    if (clipboard_offer_ != nullptr) {
        wl_data_offer_destroy(clipboard_offer_);
        clipboard_offer_ = nullptr;
    }
    clipboard_offer_mime_types_.clear();
}

void WaylandInput::destroy_clipboard_source() {
    if (clipboard_source_ != nullptr) {
        wl_data_source_destroy(clipboard_source_);
        clipboard_source_ = nullptr;
    }
}

std::string WaylandInput::preferred_clipboard_mime_type() const {
    for (const char* mime_type :
         {ClipboardMimeTextUtf8, ClipboardMimeUtf8String, ClipboardMimeTextPlain}) {
        for (const auto& offered : clipboard_offer_mime_types_) {
            if (offered == mime_type) {
                return offered;
            }
        }
    }
    return {};
}

std::string WaylandInput::receive_clipboard_offer_text() const {
    if (clipboard_offer_ == nullptr) {
        return {};
    }

    const auto mime_type = preferred_clipboard_mime_type();
    if (mime_type.empty()) {
        return {};
    }

    std::array<int, 2> pipe_fds{-1, -1};
    if (pipe(pipe_fds.data()) != 0) {
        NK_LOG_ERROR("Wayland", "Failed to create clipboard pipe");
        return {};
    }

    wl_data_offer_receive(clipboard_offer_, mime_type.c_str(), pipe_fds[1]);
    close(pipe_fds[1]);
    wl_display_flush(backend_.display());

    std::string text;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto bytes = read(pipe_fds[0], buffer.data(), buffer.size());
        if (bytes > 0) {
            text.append(buffer.data(), static_cast<std::size_t>(bytes));
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        NK_LOG_ERROR("Wayland", "Failed to read clipboard offer");
        text.clear();
        break;
    }
    close(pipe_fds[0]);
    return text;
}

std::string WaylandInput::primary_selection_text() const {
    if (owns_primary_selection_) {
        return primary_selection_text_;
    }
    if (primary_selection_offer_ != nullptr) {
        if (const auto offered_text = receive_primary_selection_offer_text();
            !offered_text.empty()) {
            return offered_text;
        }
    }
    return primary_selection_text_;
}

void WaylandInput::set_primary_selection_text(std::string text) {
    primary_selection_text_ = std::move(text);
    if (primary_selection_device_ == nullptr) {
        owns_primary_selection_ = !primary_selection_text_.empty();
        return;
    }

    destroy_primary_selection_source();
    if (primary_selection_text_.empty()) {
        owns_primary_selection_ = false;
        if (last_input_serial_ != 0) {
            zwp_primary_selection_device_v1_set_selection(
                primary_selection_device_, nullptr, last_input_serial_);
            wl_display_flush(backend_.display());
        }
        return;
    }

    primary_selection_source_ =
        zwp_primary_selection_device_manager_v1_create_source(backend_.primary_selection_manager());
    if (primary_selection_source_ == nullptr) {
        owns_primary_selection_ = true;
        return;
    }

    zwp_primary_selection_source_v1_add_listener(
        primary_selection_source_, &primary_selection_source_listener, this);
    zwp_primary_selection_source_v1_offer(primary_selection_source_, PrimarySelectionMimeTextUtf8);
    zwp_primary_selection_source_v1_offer(primary_selection_source_, PrimarySelectionMimeTextPlain);
    zwp_primary_selection_source_v1_offer(primary_selection_source_,
                                          PrimarySelectionMimeUtf8String);
    owns_primary_selection_ = true;
    if (last_input_serial_ != 0) {
        zwp_primary_selection_device_v1_set_selection(
            primary_selection_device_, primary_selection_source_, last_input_serial_);
        wl_display_flush(backend_.display());
    }
}

void WaylandInput::destroy_primary_selection_offer() {
    if (primary_selection_offer_ != nullptr) {
        zwp_primary_selection_offer_v1_destroy(primary_selection_offer_);
        primary_selection_offer_ = nullptr;
    }
    primary_selection_offer_mime_types_.clear();
}

void WaylandInput::destroy_primary_selection_source() {
    if (primary_selection_source_ != nullptr) {
        zwp_primary_selection_source_v1_destroy(primary_selection_source_);
        primary_selection_source_ = nullptr;
    }
}

std::string WaylandInput::preferred_primary_selection_mime_type() const {
    for (const char* mime_type : {PrimarySelectionMimeTextUtf8,
                                  PrimarySelectionMimeUtf8String,
                                  PrimarySelectionMimeTextPlain}) {
        for (const auto& offered : primary_selection_offer_mime_types_) {
            if (offered == mime_type) {
                return offered;
            }
        }
    }
    return {};
}

std::string WaylandInput::receive_primary_selection_offer_text() const {
    if (primary_selection_offer_ == nullptr) {
        return {};
    }

    const auto mime_type = preferred_primary_selection_mime_type();
    if (mime_type.empty()) {
        return {};
    }

    std::array<int, 2> pipe_fds{-1, -1};
    if (pipe(pipe_fds.data()) != 0) {
        NK_LOG_ERROR("Wayland", "Failed to create primary-selection pipe");
        return {};
    }

    zwp_primary_selection_offer_v1_receive(
        primary_selection_offer_, mime_type.c_str(), pipe_fds[1]);
    close(pipe_fds[1]);
    wl_display_flush(backend_.display());

    std::string text;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto bytes = read(pipe_fds[0], buffer.data(), buffer.size());
        if (bytes > 0) {
            text.append(buffer.data(), static_cast<std::size_t>(bytes));
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        NK_LOG_ERROR("Wayland", "Failed to read primary-selection offer");
        text.clear();
        break;
    }
    close(pipe_fds[0]);
    return text;
}

void WaylandInput::disable_text_input() {
    if (text_input_ == nullptr || text_input_surface_ == nullptr) {
        return;
    }
    zwp_text_input_v3_disable(text_input_);
    zwp_text_input_v3_commit(text_input_);
    text_input_surface_ = nullptr;
}

void WaylandInput::sync_text_input_state() {
    if (text_input_ == nullptr) {
        return;
    }

    if (keyboard_focus_ == nullptr) {
        disable_text_input();
        return;
    }

    const auto state = keyboard_focus_->owner().current_text_input_state();
    if (!state.has_value()) {
        disable_text_input();
        return;
    }

    if (text_input_surface_ != keyboard_focus_) {
        if (text_input_surface_ != nullptr) {
            zwp_text_input_v3_disable(text_input_);
        }
        zwp_text_input_v3_enable(text_input_);
        zwp_text_input_v3_set_content_type(text_input_,
                                           ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                           ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        text_input_surface_ = keyboard_focus_;
    }

    const int x = static_cast<int>(std::floor(state->caret_rect.x));
    const int y = static_cast<int>(std::floor(state->caret_rect.y));
    const int width = std::max(1, static_cast<int>(std::ceil(state->caret_rect.width)));
    const int height = std::max(1, static_cast<int>(std::ceil(state->caret_rect.height)));
    zwp_text_input_v3_set_cursor_rectangle(text_input_, x, y, width, height);
    zwp_text_input_v3_set_surrounding_text(
        text_input_, state->text.c_str(), state->cursor, state->anchor);
    zwp_text_input_v3_commit(text_input_);
}

// ---------------------------------------------------------------------------
// Seat callbacks
// ---------------------------------------------------------------------------

void WaylandInput::seat_capabilities(void* data, wl_seat* /*seat*/, uint32_t caps) {
    auto* self = static_cast<WaylandInput*>(data);

    const bool has_pointer = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
    const bool has_keyboard = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

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
        self->stop_key_repeat();
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
    }
}

void WaylandInput::seat_name(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) {
    // Informational — no action needed.
}

// ---------------------------------------------------------------------------
// Pointer callbacks
// ---------------------------------------------------------------------------

void WaylandInput::pointer_enter(void* data,
                                 wl_pointer* /*pointer*/,
                                 uint32_t /*serial*/,
                                 wl_surface* surface,
                                 wl_fixed_t x,
                                 wl_fixed_t y) {
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

void WaylandInput::pointer_leave(void* data,
                                 wl_pointer* /*pointer*/,
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

void WaylandInput::pointer_motion(
    void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, wl_fixed_t x, wl_fixed_t y) {
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

void WaylandInput::pointer_button(void* data,
                                  wl_pointer* /*pointer*/,
                                  uint32_t serial,
                                  uint32_t /*time*/,
                                  uint32_t button,
                                  uint32_t state) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self->pointer_focus_) {
        return;
    }

    MouseEvent me{};
    me.type = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? MouseEvent::Type::Press
                                                         : MouseEvent::Type::Release;
    me.x = self->pointer_x_;
    me.y = self->pointer_y_;
    me.button = wayland_button_to_nk(button);
    self->last_input_serial_ = serial;
    self->pointer_focus_->owner().dispatch_mouse_event(me);
    if (self->pointer_focus_ == self->keyboard_focus_) {
        self->sync_text_input_state();
    }
}

void WaylandInput::pointer_axis(
    void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, uint32_t axis, wl_fixed_t value) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self->pointer_focus_) {
        return;
    }

    MouseEvent me{};
    me.type = MouseEvent::Type::Scroll;
    me.x = self->pointer_x_;
    me.y = self->pointer_y_;

    const auto delta = static_cast<float>(wl_fixed_to_double(value));
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

void WaylandInput::keyboard_keymap(
    void* data, wl_keyboard* /*keyboard*/, uint32_t format, int fd, uint32_t size) {
    auto* self = static_cast<WaylandInput*>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    auto* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
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
        self->xkb_ctx_, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);

    if (self->xkb_keymap_) {
        self->xkb_state_ = xkb_state_new(self->xkb_keymap_);
    }
}

void WaylandInput::keyboard_enter(void* data,
                                  wl_keyboard* /*keyboard*/,
                                  uint32_t serial,
                                  wl_surface* surface,
                                  wl_array* /*keys*/) {
    auto* self = static_cast<WaylandInput*>(data);
    self->keyboard_focus_ = self->backend_.find_surface(surface);
    self->last_input_serial_ = serial;

    if (self->keyboard_focus_) {
        WindowEvent we{};
        we.type = WindowEvent::Type::FocusIn;
        self->keyboard_focus_->owner().dispatch_window_event(we);
        self->sync_text_input_state();
    }
}

void WaylandInput::keyboard_leave(void* data,
                                  wl_keyboard* /*keyboard*/,
                                  uint32_t /*serial*/,
                                  wl_surface* /*surface*/) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->keyboard_focus_) {
        self->stop_key_repeat();
        self->disable_text_input();
        TextInputEvent te{};
        te.type = TextInputEvent::Type::ClearPreedit;
        self->keyboard_focus_->owner().dispatch_text_input_event(te);
        WindowEvent we{};
        we.type = WindowEvent::Type::FocusOut;
        self->keyboard_focus_->owner().dispatch_window_event(we);
    }
    self->keyboard_focus_ = nullptr;
}

void WaylandInput::keyboard_key(void* data,
                                wl_keyboard* /*keyboard*/,
                                uint32_t serial,
                                uint32_t /*time*/,
                                uint32_t key,
                                uint32_t state) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self->keyboard_focus_ || !self->xkb_state_) {
        return;
    }

    self->last_input_serial_ = serial;

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(self->xkb_state_, key + 8);
        KeyEvent ke{};
        ke.type = KeyEvent::Type::Release;
        ke.key = xkb_keysym_to_nk(sym);
        ke.is_repeat = false;
        ke.modifiers = wayland_effective_modifiers(self->xkb_state_);
        self->keyboard_focus_->owner().dispatch_key_event(ke);
        self->sync_text_input_state();
        if (self->repeating_key_ == key) {
            self->stop_key_repeat();
        }
        return;
    }

    self->dispatch_key_press(key, false);
    if (self->can_repeat_key(key)) {
        self->start_key_repeat(key);
    }
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
        xkb_state_update_mask(
            self->xkb_state_, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }
}

void WaylandInput::text_input_enter(void* data,
                                    zwp_text_input_v3* /*text_input*/,
                                    wl_surface* surface) {
    auto* self = static_cast<WaylandInput*>(data);
    self->text_input_surface_ = self->backend_.find_surface(surface);
    self->sync_text_input_state();
}

void WaylandInput::text_input_leave(void* data,
                                    zwp_text_input_v3* /*text_input*/,
                                    wl_surface* /*surface*/) {
    auto* self = static_cast<WaylandInput*>(data);
    self->text_input_surface_ = nullptr;
    if (self->keyboard_focus_ != nullptr) {
        TextInputEvent te{};
        te.type = TextInputEvent::Type::ClearPreedit;
        self->keyboard_focus_->owner().dispatch_text_input_event(te);
    }
}

void WaylandInput::text_input_preedit_string(void* data,
                                             zwp_text_input_v3* /*text_input*/,
                                             const char* text,
                                             int32_t cursor_begin,
                                             int32_t cursor_end) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->keyboard_focus_ == nullptr) {
        return;
    }

    const std::string preedit = text != nullptr ? std::string(text) : std::string{};
    TextInputEvent te{};
    te.type = preedit.empty() ? TextInputEvent::Type::ClearPreedit : TextInputEvent::Type::Preedit;
    te.text = preedit;
    const std::size_t caret =
        cursor_end < 0 ? preedit.size() : std::min<std::size_t>(cursor_end, preedit.size());
    const std::size_t anchor =
        cursor_begin < 0 ? caret : std::min<std::size_t>(cursor_begin, preedit.size());
    te.selection_start = std::min(anchor, caret);
    te.selection_end = std::max(anchor, caret);
    self->keyboard_focus_->owner().dispatch_text_input_event(te);
}

void WaylandInput::text_input_commit_string(void* data,
                                            zwp_text_input_v3* /*text_input*/,
                                            const char* text) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->keyboard_focus_ == nullptr || text == nullptr || text[0] == '\0') {
        return;
    }

    TextInputEvent te{};
    te.type = TextInputEvent::Type::Commit;
    te.text = text;
    self->keyboard_focus_->owner().dispatch_text_input_event(te);
}

void WaylandInput::text_input_delete_surrounding_text(void* data,
                                                      zwp_text_input_v3* /*text_input*/,
                                                      uint32_t before_length,
                                                      uint32_t after_length) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->keyboard_focus_ == nullptr) {
        return;
    }

    TextInputEvent te{};
    te.type = TextInputEvent::Type::DeleteSurrounding;
    te.delete_before_length = before_length;
    te.delete_after_length = after_length;
    self->keyboard_focus_->owner().dispatch_text_input_event(te);
}

void WaylandInput::text_input_done(void* data,
                                   zwp_text_input_v3* /*text_input*/,
                                   uint32_t /*serial*/) {
    auto* self = static_cast<WaylandInput*>(data);
    self->sync_text_input_state();
}

void WaylandInput::clipboard_data_offer(void* data,
                                        wl_data_device* /*device*/,
                                        wl_data_offer* offer) {
    auto* self = static_cast<WaylandInput*>(data);
    if (offer == nullptr) {
        return;
    }
    wl_data_offer_add_listener(offer, &clipboard_offer_listener, self);
}

void WaylandInput::clipboard_enter(void* /*data*/,
                                   wl_data_device* /*device*/,
                                   uint32_t /*serial*/,
                                   wl_surface* /*surface*/,
                                   wl_fixed_t /*x*/,
                                   wl_fixed_t /*y*/,
                                   wl_data_offer* /*offer*/) {}

void WaylandInput::clipboard_leave(void* /*data*/, wl_data_device* /*device*/) {}

void WaylandInput::clipboard_motion(void* /*data*/,
                                    wl_data_device* /*device*/,
                                    uint32_t /*time*/,
                                    wl_fixed_t /*x*/,
                                    wl_fixed_t /*y*/) {}

void WaylandInput::clipboard_drop(void* /*data*/, wl_data_device* /*device*/) {}

void WaylandInput::clipboard_selection(void* data,
                                       wl_data_device* /*device*/,
                                       wl_data_offer* offer) {
    auto* self = static_cast<WaylandInput*>(data);
    self->destroy_clipboard_offer();
    self->clipboard_offer_ = offer;
    if (offer == nullptr) {
        if (!self->owns_clipboard_) {
            self->clipboard_text_.clear();
        }
        return;
    }
    if (!self->owns_clipboard_) {
        self->clipboard_text_.clear();
    }
}

void WaylandInput::clipboard_offer_offer(void* data,
                                         wl_data_offer* /*offer*/,
                                         const char* mime_type) {
    auto* self = static_cast<WaylandInput*>(data);
    if (mime_type == nullptr) {
        return;
    }
    self->clipboard_offer_mime_types_.emplace_back(mime_type);
}

void WaylandInput::clipboard_offer_source_actions(void* /*data*/,
                                                  wl_data_offer* /*offer*/,
                                                  uint32_t /*source_actions*/) {}

void WaylandInput::clipboard_offer_action(void* /*data*/,
                                          wl_data_offer* /*offer*/,
                                          uint32_t /*dnd_action*/) {}

void WaylandInput::clipboard_source_target(void* /*data*/,
                                           wl_data_source* /*source*/,
                                           const char* /*mime_type*/) {}

void WaylandInput::clipboard_source_send(void* data,
                                         wl_data_source* /*source*/,
                                         const char* /*mime_type*/,
                                         int32_t fd) {
    auto* self = static_cast<WaylandInput*>(data);
    const char* buffer = self->clipboard_text_.data();
    std::size_t remaining = self->clipboard_text_.size();
    while (remaining > 0) {
        const auto written = write(fd, buffer, remaining);
        if (written > 0) {
            buffer += written;
            remaining -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        NK_LOG_ERROR("Wayland", "Failed to write clipboard payload");
        break;
    }
    close(fd);
}

void WaylandInput::clipboard_source_cancelled(void* data, wl_data_source* source) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->clipboard_source_ == source) {
        self->clipboard_source_ = nullptr;
    }
    self->owns_clipboard_ = false;
}

void WaylandInput::clipboard_source_dnd_drop_performed(void* /*data*/, wl_data_source* /*source*/) {
}

void WaylandInput::clipboard_source_dnd_finished(void* /*data*/, wl_data_source* /*source*/) {}

void WaylandInput::clipboard_source_action(void* /*data*/,
                                           wl_data_source* /*source*/,
                                           uint32_t /*dnd_action*/) {}

void WaylandInput::primary_selection_data_offer(void* data,
                                                zwp_primary_selection_device_v1* /*device*/,
                                                zwp_primary_selection_offer_v1* offer) {
    auto* self = static_cast<WaylandInput*>(data);
    if (offer == nullptr) {
        return;
    }
    zwp_primary_selection_offer_v1_add_listener(offer, &primary_selection_offer_listener, self);
}

void WaylandInput::primary_selection_selection(void* data,
                                               zwp_primary_selection_device_v1* /*device*/,
                                               zwp_primary_selection_offer_v1* offer) {
    auto* self = static_cast<WaylandInput*>(data);
    self->destroy_primary_selection_offer();
    self->primary_selection_offer_ = offer;
    if (offer == nullptr) {
        if (!self->owns_primary_selection_) {
            self->primary_selection_text_.clear();
        }
        return;
    }
    if (!self->owns_primary_selection_) {
        self->primary_selection_text_.clear();
    }
}

void WaylandInput::primary_selection_offer_offer(void* data,
                                                 zwp_primary_selection_offer_v1* /*offer*/,
                                                 const char* mime_type) {
    auto* self = static_cast<WaylandInput*>(data);
    if (mime_type == nullptr) {
        return;
    }
    self->primary_selection_offer_mime_types_.emplace_back(mime_type);
}

void WaylandInput::primary_selection_source_send(void* data,
                                                 zwp_primary_selection_source_v1* /*source*/,
                                                 const char* /*mime_type*/,
                                                 int32_t fd) {
    auto* self = static_cast<WaylandInput*>(data);
    const char* buffer = self->primary_selection_text_.data();
    std::size_t remaining = self->primary_selection_text_.size();
    while (remaining > 0) {
        const auto written = write(fd, buffer, remaining);
        if (written > 0) {
            buffer += written;
            remaining -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        NK_LOG_ERROR("Wayland", "Failed to write primary-selection payload");
        break;
    }
    close(fd);
}

void WaylandInput::primary_selection_source_cancelled(void* data,
                                                      zwp_primary_selection_source_v1* source) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self->primary_selection_source_ == source) {
        self->primary_selection_source_ = nullptr;
    }
    self->owns_primary_selection_ = false;
}

} // namespace nk
