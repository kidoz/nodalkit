#pragma once

/// @file wayland_surface.h
/// @brief Wayland native surface (private header).

#include <cstdint>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <unordered_set>
#include <vector>

struct wl_surface;
struct wl_buffer;
struct wl_output;
struct xdg_surface;
struct xdg_toplevel;

namespace nk {

class WaylandBackend;

class WaylandSurface : public NativeSurface {
public:
    WaylandSurface(WaylandBackend& backend, const WindowConfig& config, Window& owner);
    ~WaylandSurface() override;

    void show() override;
    void hide() override;
    void set_title(std::string_view title) override;
    void resize(int width, int height) override;
    [[nodiscard]] Size size() const override;
    [[nodiscard]] float scale_factor() const override;
    [[nodiscard]] RendererBackendSupport renderer_backend_support() const override;
    void present(const uint8_t* rgba, int w, int h, std::span<const Rect> damage_regions) override;
    void set_fullscreen(bool fullscreen) override;
    [[nodiscard]] bool is_fullscreen() const override;
    [[nodiscard]] NativeWindowHandle native_handle() const override;
    [[nodiscard]] NativeWindowHandle native_display_handle() const override;
    void set_cursor_shape(CursorShape shape) override;

    /// Access the owning Window for event delivery.
    Window& owner() { return owner_; }

    /// The underlying wl_surface pointer.
    [[nodiscard]] wl_surface* wl_surf() const { return surface_; }

    // Called from XDG callbacks.
    void handle_configure(int width, int height);
    void handle_close();

    // Called from the wl_surface enter/leave callbacks and from the wl_output.done handler in
    // the backend when an output's advertised scale changes.
    void handle_output_enter(wl_output* output);
    void handle_output_leave(wl_output* output);
    void recompute_scale_factor();

private:
    struct ShmBuffer {
        wl_buffer* buffer = nullptr;
        uint8_t* data = nullptr;
        int fd = -1;
        size_t pool_size = 0;
        bool busy = false;
        bool initialized = false;
    };

    void destroy_buffer(ShmBuffer& buf);
    void ensure_buffer(ShmBuffer& buf, int w, int h);

    WaylandBackend& backend_;
    Window& owner_;

    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* xdg_toplevel_ = nullptr;

    ShmBuffer buffers_[2];
    int current_buffer_ = 0;
    int last_presented_buffer_ = -1;

    int width_;
    int height_;
    bool fullscreen_ = false;
    bool configured_ = false;
    int buffer_scale_ = 1;
    std::unordered_set<wl_output*> entered_outputs_;
};

} // namespace nk
