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
struct wl_array;
struct xdg_surface;
struct xdg_toplevel;
struct wp_fractional_scale_v1;
struct wp_viewport;
struct zxdg_toplevel_decoration_v1;

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
    void minimize() override;
    void toggle_maximize() override;
    [[nodiscard]] bool is_maximized() const override;
    [[nodiscard]] bool uses_client_side_decorations() const override;
    [[nodiscard]] bool begin_system_move(std::uint32_t serial) override;
    [[nodiscard]] NativeWindowHandle native_handle() const override;
    [[nodiscard]] NativeWindowHandle native_display_handle() const override;
    void set_cursor_shape(CursorShape shape) override;
    void set_titlebar_style(TitlebarStyle style) override;

    /// Access the owning Window for event delivery.
    Window& owner() { return owner_; }

    /// The underlying wl_surface pointer.
    [[nodiscard]] wl_surface* wl_surf() const { return surface_; }

    // Called from XDG callbacks.
    void handle_configure(int width, int height, const wl_array* states);
    void handle_decoration_configure(std::uint32_t mode);
    void handle_close();
    [[nodiscard]] bool begin_resize_if_needed(float x, float y, std::uint32_t serial);

    // Called from the wl_surface enter/leave callbacks and from the wl_output.done handler in
    // the backend when an output's advertised scale changes.
    void handle_output_enter(wl_output* output);
    void handle_output_leave(wl_output* output);
    void recompute_scale_factor();

    // Called from the wp_fractional_scale_v1.preferred_scale listener. The value is the
    // compositor's preferred scale numerator with an implicit denominator of 120 (e.g. 180 =
    // 1.5x). The surface switches to fractional-scale mode on the first event and stays there.
    void handle_preferred_fractional_scale(uint32_t scale_numerator);

private:
    struct ShmBuffer {
        wl_buffer* buffer = nullptr;
        uint8_t* data = nullptr;
        int fd = -1;
        size_t pool_size = 0;
        int width = 0;
        int height = 0;
        bool busy = false;
        bool initialized = false;
    };

    void destroy_buffer(ShmBuffer& buf);
    void ensure_buffer(ShmBuffer& buf, int w, int h);
    void ensure_decoration_object();
    void update_decoration_preference();
    void notify_native_chrome_changed();

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
    bool maximized_ = false;
    bool decorated_ = true;
    bool resizable_ = true;
    bool configured_ = false;
    int buffer_scale_ = 1;
    std::unordered_set<wl_output*> entered_outputs_;

    // Fractional-scale state. When `fractional_scale_ > 0.0F`, the surface is driven by
    // wp_fractional_scale_v1.preferred_scale instead of wl_output.scale: `buffer_scale_` stays
    // at 1, `wl_surface_set_buffer_scale` is not called, and we use wp_viewport.set_destination
    // to declare the logical surface size to the compositor.
    wp_fractional_scale_v1* fractional_scale_ctrl_ = nullptr;
    wp_viewport* viewport_ = nullptr;
    float fractional_scale_ = 0.0F;

    // Effective decoration mode reported by the compositor. If the optional
    // negotiation protocol is absent, Wayland defaults to client ownership.
    zxdg_toplevel_decoration_v1* toplevel_decoration_ = nullptr;
    bool client_side_decoration_ = false;
    TitlebarStyle titlebar_style_ = TitlebarStyle::Regular;
};

} // namespace nk
