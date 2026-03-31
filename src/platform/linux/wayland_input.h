#pragma once

/// @file wayland_input.h
/// @brief Wayland seat / pointer / keyboard input (private header).

struct wl_seat;
struct wl_pointer;
struct wl_keyboard;
struct wl_surface;
struct wl_array;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

#include <cstdint>

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

    WaylandInput(WaylandInput const&) = delete;
    WaylandInput& operator=(WaylandInput const&) = delete;

    // --- Wayland listener callbacks (public so listener structs can reference them) ---

    static void seat_capabilities(void* data, wl_seat* seat, uint32_t caps);
    static void seat_name(void* data, wl_seat* seat, char const* name);

    static void pointer_enter(void* data, wl_pointer* pointer,
                              uint32_t serial, wl_surface* surface,
                              wl_fixed_t x, wl_fixed_t y);
    static void pointer_leave(void* data, wl_pointer* pointer,
                              uint32_t serial, wl_surface* surface);
    static void pointer_motion(void* data, wl_pointer* pointer,
                               uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void pointer_button(void* data, wl_pointer* pointer,
                               uint32_t serial, uint32_t time,
                               uint32_t button, uint32_t state);
    static void pointer_axis(void* data, wl_pointer* pointer,
                             uint32_t time, uint32_t axis, wl_fixed_t value);

    static void keyboard_keymap(void* data, wl_keyboard* keyboard,
                                uint32_t format, int fd, uint32_t size);
    static void keyboard_enter(void* data, wl_keyboard* keyboard,
                               uint32_t serial, wl_surface* surface,
                               wl_array* keys);
    static void keyboard_leave(void* data, wl_keyboard* keyboard,
                               uint32_t serial, wl_surface* surface);
    static void keyboard_key(void* data, wl_keyboard* keyboard,
                             uint32_t serial, uint32_t time,
                             uint32_t key, uint32_t state);
    static void keyboard_modifiers(void* data, wl_keyboard* keyboard,
                                   uint32_t serial, uint32_t mods_depressed,
                                   uint32_t mods_latched,
                                   uint32_t mods_locked, uint32_t group);

private:
    WaylandBackend& backend_;
    wl_seat* seat_;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;

    xkb_context* xkb_ctx_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;

    WaylandSurface* pointer_focus_ = nullptr;
    WaylandSurface* keyboard_focus_ = nullptr;
    float pointer_x_ = 0;
    float pointer_y_ = 0;
};

} // namespace nk
