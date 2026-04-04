#pragma once

/// @file wayland_input.h
/// @brief Wayland seat / pointer / keyboard input (private header).

struct wl_seat;
struct wl_pointer;
struct wl_keyboard;
struct wl_surface;
struct wl_data_device;
struct wl_data_source;
struct wl_data_offer;
struct wl_array;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;
struct zwp_text_input_v3;
struct zwp_primary_selection_device_v1;
struct zwp_primary_selection_source_v1;
struct zwp_primary_selection_offer_v1;

#include <chrono>
#include <cstdint>
#include <nk/runtime/event_loop.h>
#include <string>
#include <vector>

// wl_fixed_t is int32_t in wayland-util.h; forward-declare the typedef so
// the header compiles without pulling in the full Wayland headers.
using wl_fixed_t = int32_t;

namespace nk {

class WaylandBackend;
class WaylandSurface;

class WaylandInput {
public:
    WaylandInput(WaylandBackend& backend, wl_seat* seat);
    ~WaylandInput();

    WaylandInput(const WaylandInput&) = delete;
    WaylandInput& operator=(const WaylandInput&) = delete;

    [[nodiscard]] std::string primary_selection_text() const;
    void set_primary_selection_text(std::string text);
    [[nodiscard]] std::string clipboard_text() const;
    void set_clipboard_text(std::string text);

    // --- Wayland listener callbacks (public so listener structs can reference them) ---

    static void seat_capabilities(void* data, wl_seat* seat, uint32_t caps);
    static void seat_name(void* data, wl_seat* seat, const char* name);

    static void pointer_enter(void* data,
                              wl_pointer* pointer,
                              uint32_t serial,
                              wl_surface* surface,
                              wl_fixed_t x,
                              wl_fixed_t y);
    static void
    pointer_leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
    static void
    pointer_motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void pointer_button(void* data,
                               wl_pointer* pointer,
                               uint32_t serial,
                               uint32_t time,
                               uint32_t button,
                               uint32_t state);
    static void
    pointer_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);

    static void
    keyboard_keymap(void* data, wl_keyboard* keyboard, uint32_t format, int fd, uint32_t size);
    static void keyboard_enter(
        void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys);
    static void
    keyboard_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
    static void keyboard_key(void* data,
                             wl_keyboard* keyboard,
                             uint32_t serial,
                             uint32_t time,
                             uint32_t key,
                             uint32_t state);
    static void
    keyboard_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);
    static void keyboard_modifiers(void* data,
                                   wl_keyboard* keyboard,
                                   uint32_t serial,
                                   uint32_t mods_depressed,
                                   uint32_t mods_latched,
                                   uint32_t mods_locked,
                                   uint32_t group);
    static void text_input_enter(void* data, zwp_text_input_v3* text_input, wl_surface* surface);
    static void text_input_leave(void* data, zwp_text_input_v3* text_input, wl_surface* surface);
    static void text_input_preedit_string(void* data,
                                          zwp_text_input_v3* text_input,
                                          const char* text,
                                          int32_t cursor_begin,
                                          int32_t cursor_end);
    static void
    text_input_commit_string(void* data, zwp_text_input_v3* text_input, const char* text);
    static void text_input_delete_surrounding_text(void* data,
                                                   zwp_text_input_v3* text_input,
                                                   uint32_t before_length,
                                                   uint32_t after_length);
    static void text_input_done(void* data, zwp_text_input_v3* text_input, uint32_t serial);
    static void primary_selection_data_offer(void* data,
                                             zwp_primary_selection_device_v1* device,
                                             zwp_primary_selection_offer_v1* offer);
    static void primary_selection_selection(void* data,
                                            zwp_primary_selection_device_v1* device,
                                            zwp_primary_selection_offer_v1* offer);
    static void primary_selection_offer_offer(void* data,
                                              zwp_primary_selection_offer_v1* offer,
                                              const char* mime_type);
    static void primary_selection_source_send(void* data,
                                              zwp_primary_selection_source_v1* source,
                                              const char* mime_type,
                                              int32_t fd);
    static void primary_selection_source_cancelled(void* data,
                                                   zwp_primary_selection_source_v1* source);
    static void clipboard_data_offer(void* data, wl_data_device* device, wl_data_offer* offer);
    static void clipboard_enter(void* data,
                                wl_data_device* device,
                                uint32_t serial,
                                wl_surface* surface,
                                wl_fixed_t x,
                                wl_fixed_t y,
                                wl_data_offer* offer);
    static void clipboard_leave(void* data, wl_data_device* device);
    static void
    clipboard_motion(void* data, wl_data_device* device, uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void clipboard_drop(void* data, wl_data_device* device);
    static void clipboard_selection(void* data, wl_data_device* device, wl_data_offer* offer);
    static void clipboard_offer_offer(void* data, wl_data_offer* offer, const char* mime_type);
    static void
    clipboard_offer_source_actions(void* data, wl_data_offer* offer, uint32_t source_actions);
    static void clipboard_offer_action(void* data, wl_data_offer* offer, uint32_t dnd_action);
    static void clipboard_source_target(void* data, wl_data_source* source, const char* mime_type);
    static void
    clipboard_source_send(void* data, wl_data_source* source, const char* mime_type, int32_t fd);
    static void clipboard_source_cancelled(void* data, wl_data_source* source);
    static void clipboard_source_dnd_drop_performed(void* data, wl_data_source* source);
    static void clipboard_source_dnd_finished(void* data, wl_data_source* source);
    static void clipboard_source_action(void* data, wl_data_source* source, uint32_t dnd_action);

private:
    [[nodiscard]] bool can_repeat_key(uint32_t key) const;
    void start_key_repeat(uint32_t key);
    void stop_key_repeat();
    void schedule_key_repeat(std::chrono::milliseconds delay);
    void dispatch_key_press(uint32_t key, bool is_repeat);
    void destroy_clipboard_offer();
    void destroy_clipboard_source();
    [[nodiscard]] std::string receive_clipboard_offer_text() const;
    [[nodiscard]] std::string preferred_clipboard_mime_type() const;
    void destroy_primary_selection_offer();
    void destroy_primary_selection_source();
    [[nodiscard]] std::string receive_primary_selection_offer_text() const;
    [[nodiscard]] std::string preferred_primary_selection_mime_type() const;
    void sync_text_input_state();
    void disable_text_input();

    WaylandBackend& backend_;
    wl_seat* seat_;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    zwp_text_input_v3* text_input_ = nullptr;
    wl_data_device* clipboard_device_ = nullptr;
    wl_data_source* clipboard_source_ = nullptr;
    wl_data_offer* clipboard_offer_ = nullptr;
    zwp_primary_selection_device_v1* primary_selection_device_ = nullptr;
    zwp_primary_selection_source_v1* primary_selection_source_ = nullptr;
    zwp_primary_selection_offer_v1* primary_selection_offer_ = nullptr;

    xkb_context* xkb_ctx_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;
    xkb_compose_table* xkb_compose_table_ = nullptr;
    xkb_compose_state* xkb_compose_state_ = nullptr;

    WaylandSurface* pointer_focus_ = nullptr;
    WaylandSurface* keyboard_focus_ = nullptr;
    WaylandSurface* text_input_surface_ = nullptr;
    float pointer_x_ = 0;
    float pointer_y_ = 0;
    uint32_t last_input_serial_ = 0;
    CallbackHandle key_repeat_handle_{};
    uint32_t repeating_key_ = 0;
    std::chrono::milliseconds key_repeat_delay_{400};
    std::chrono::milliseconds key_repeat_interval_{33};
    std::string clipboard_text_;
    std::vector<std::string> clipboard_offer_mime_types_;
    bool owns_clipboard_ = false;
    std::string primary_selection_text_;
    std::vector<std::string> primary_selection_offer_mime_types_;
    bool owns_primary_selection_ = false;
};

} // namespace nk
