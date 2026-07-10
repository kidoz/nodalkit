/// @file wayland_surface.cpp
/// @brief Wayland native surface implementation.

#include "wayland_surface.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wayland_backend.h"
#include "wayland_input.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <nk/foundation/logging.h>
#include <nk/platform/events.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

namespace nk {

// ---------------------------------------------------------------------------
// XDG surface listener
// ---------------------------------------------------------------------------

static void
xdg_surface_handle_configure(void* /*data*/, struct xdg_surface* xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

static constexpr struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

// ---------------------------------------------------------------------------
// XDG toplevel listener
// ---------------------------------------------------------------------------

static void xdg_toplevel_handle_configure(void* data,
                                          struct xdg_toplevel* /*toplevel*/,
                                          int32_t width,
                                          int32_t height,
                                          struct wl_array* states) {
    auto* self = static_cast<WaylandSurface*>(data);
    self->handle_configure(width, height, states);
}

static void xdg_toplevel_handle_close(void* data, struct xdg_toplevel* /*toplevel*/) {
    auto* self = static_cast<WaylandSurface*>(data);
    self->handle_close();
}

static void xdg_toplevel_handle_configure_bounds(void* /*data*/,
                                                 struct xdg_toplevel* /*toplevel*/,
                                                 int32_t /*width*/,
                                                 int32_t /*height*/) {}

static void xdg_toplevel_handle_wm_capabilities(void* /*data*/,
                                                struct xdg_toplevel* /*toplevel*/,
                                                struct wl_array* /*capabilities*/) {}

static constexpr struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
    .configure_bounds = xdg_toplevel_handle_configure_bounds,
    .wm_capabilities = xdg_toplevel_handle_wm_capabilities,
};

static void decoration_handle_configure(void* data,
                                        zxdg_toplevel_decoration_v1* /*decoration*/,
                                        uint32_t mode) {
    static_cast<WaylandSurface*>(data)->handle_decoration_configure(mode);
}

static constexpr zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = decoration_handle_configure,
};

// ---------------------------------------------------------------------------
// Buffer release callback
// ---------------------------------------------------------------------------

static void buffer_handle_release(void* data, struct wl_buffer* /*buffer*/) {
    auto* busy = static_cast<bool*>(data);
    *busy = false;
}

static constexpr struct wl_buffer_listener buffer_listener = {
    .release = buffer_handle_release,
};

// ---------------------------------------------------------------------------
// wl_surface listener (HiDPI enter/leave tracking)
// ---------------------------------------------------------------------------

static void wl_surface_handle_enter(void* data, struct wl_surface* /*surface*/, wl_output* output) {
    static_cast<WaylandSurface*>(data)->handle_output_enter(output);
}

static void wl_surface_handle_leave(void* data, struct wl_surface* /*surface*/, wl_output* output) {
    static_cast<WaylandSurface*>(data)->handle_output_leave(output);
}

static void wl_surface_handle_preferred_buffer_scale(void* /*data*/,
                                                     struct wl_surface* /*surface*/,
                                                     int32_t /*factor*/) {
    // wl_surface v6 optional event — we derive the effective scale from enter/leave across the
    // outputs we know about. Ignore the compositor hint for now.
}

static void wl_surface_handle_preferred_buffer_transform(void* /*data*/,
                                                         struct wl_surface* /*surface*/,
                                                         uint32_t /*transform*/) {}

static constexpr struct wl_surface_listener wl_surface_listener = {
    .enter = wl_surface_handle_enter,
    .leave = wl_surface_handle_leave,
    .preferred_buffer_scale = wl_surface_handle_preferred_buffer_scale,
    .preferred_buffer_transform = wl_surface_handle_preferred_buffer_transform,
};

// ---------------------------------------------------------------------------
// Shared memory helper
// ---------------------------------------------------------------------------

static int create_shm_fd(size_t size) {
    int fd = memfd_create("nk-wl-shm", MFD_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// ---------------------------------------------------------------------------
// wp_fractional_scale_v1 listener
// ---------------------------------------------------------------------------

static void fractional_scale_handle_preferred(void* data,
                                              struct wp_fractional_scale_v1* /*ctrl*/,
                                              uint32_t scale) {
    static_cast<WaylandSurface*>(data)->handle_preferred_fractional_scale(scale);
}

static constexpr struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_handle_preferred,
};

// ---------------------------------------------------------------------------
// WaylandSurface
// ---------------------------------------------------------------------------

WaylandSurface::WaylandSurface(WaylandBackend& backend, const WindowConfig& config, Window& owner)
    : backend_(backend)
    , owner_(owner)
    , width_(config.width)
    , height_(config.height)
    , decorated_(config.decorated)
    , resizable_(config.resizable)
    , titlebar_style_(config.titlebar_style) {

    surface_ = wl_compositor_create_surface(backend_.compositor());
    wl_surface_add_listener(surface_, &wl_surface_listener, this);

    xdg_surface_ = xdg_wm_base_get_xdg_surface(backend_.wm_base(), surface_);
    xdg_surface_add_listener(xdg_surface_, &xdg_surface_listener, this);

    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(xdg_toplevel_, &xdg_toplevel_listener, this);
    xdg_toplevel_set_title(xdg_toplevel_, config.title.c_str());
    if (!config.app_id.empty()) {
        xdg_toplevel_set_app_id(xdg_toplevel_, config.app_id.c_str());
    }

    if (!config.resizable) {
        xdg_toplevel_set_min_size(xdg_toplevel_, config.width, config.height);
        xdg_toplevel_set_max_size(xdg_toplevel_, config.width, config.height);
    }

    // Register for input event routing.
    backend_.register_surface(surface_, this);

    // Fractional scale + viewport (optional — both are required together to drive non-integer
    // scales). If either manager is absent, we fall back to the integer-scale path via
    // wl_output.scale. The listener stays silent until the compositor sends preferred_scale.
    if (backend_.fractional_scale_manager() != nullptr && backend_.viewporter() != nullptr) {
        fractional_scale_ctrl_ = wp_fractional_scale_manager_v1_get_fractional_scale(
            backend_.fractional_scale_manager(), surface_);
        wp_fractional_scale_v1_add_listener(
            fractional_scale_ctrl_, &fractional_scale_listener, this);
        viewport_ = wp_viewporter_get_viewport(backend_.viewporter(), surface_);
        // Seed the destination to the logical window size so the compositor has a sensible
        // default even before the first preferred_scale event arrives.
        wp_viewport_set_destination(viewport_, width_, height_);
    }

    update_decoration_preference();

    // Commit the initial surface state to trigger the first configure event.
    wl_surface_commit(surface_);
    wl_display_roundtrip(backend_.display());
    configured_ = true;

    NK_LOG_DEBUG("Wayland", "Surface created");
}

WaylandSurface::~WaylandSurface() {
    backend_.unregister_surface(surface_);

    destroy_buffer(buffers_[0]);
    destroy_buffer(buffers_[1]);

    if (viewport_) {
        wp_viewport_destroy(viewport_);
        viewport_ = nullptr;
    }
    if (fractional_scale_ctrl_) {
        wp_fractional_scale_v1_destroy(fractional_scale_ctrl_);
        fractional_scale_ctrl_ = nullptr;
    }
    if (toplevel_decoration_) {
        zxdg_toplevel_decoration_v1_destroy(toplevel_decoration_);
        toplevel_decoration_ = nullptr;
    }
    if (xdg_toplevel_) {
        xdg_toplevel_destroy(xdg_toplevel_);
    }
    if (xdg_surface_) {
        xdg_surface_destroy(xdg_surface_);
    }
    if (surface_) {
        wl_surface_destroy(surface_);
    }

    NK_LOG_DEBUG("Wayland", "Surface destroyed");
}

void WaylandSurface::destroy_buffer(ShmBuffer& buf) {
    if (buf.buffer) {
        wl_buffer_destroy(buf.buffer);
        buf.buffer = nullptr;
    }
    if (buf.data) {
        munmap(buf.data, buf.pool_size);
        buf.data = nullptr;
    }
    if (buf.fd >= 0) {
        close(buf.fd);
        buf.fd = -1;
    }
    buf.pool_size = 0;
    buf.width = 0;
    buf.height = 0;
    buf.busy = false;
    buf.initialized = false;
}

void WaylandSurface::ensure_buffer(ShmBuffer& buf, int w, int h) {
    const auto needed = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    // Reuse only when the wl_buffer's actual geometry matches: a resize that preserves total
    // area (e.g. 800x600 -> 600x800) keeps `needed` constant, but the existing wl_buffer was
    // created with the old width/height/stride, so reusing it would corrupt the presented image.
    if (buf.buffer && buf.pool_size == needed && buf.width == w && buf.height == h) {
        return;
    }

    destroy_buffer(buf);

    buf.fd = create_shm_fd(needed);
    if (buf.fd < 0) {
        NK_LOG_ERROR("Wayland", "Failed to create shm fd");
        return;
    }

    buf.data =
        static_cast<uint8_t*>(mmap(nullptr, needed, PROT_READ | PROT_WRITE, MAP_SHARED, buf.fd, 0));
    if (buf.data == MAP_FAILED) {
        buf.data = nullptr;
        close(buf.fd);
        buf.fd = -1;
        NK_LOG_ERROR("Wayland", "Failed to mmap shm buffer");
        return;
    }

    struct wl_shm_pool* pool =
        wl_shm_create_pool(backend_.shm(), buf.fd, static_cast<int32_t>(needed));
    buf.buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    wl_buffer_add_listener(buf.buffer, &buffer_listener, &buf.busy);
    buf.pool_size = needed;
    buf.width = w;
    buf.height = h;
}

// ---------------------------------------------------------------------------
// NativeSurface overrides
// ---------------------------------------------------------------------------

void WaylandSurface::show() {
    // On Wayland the surface becomes visible once a buffer is committed.
    // present() handles the actual commit.
}

void WaylandSurface::hide() {
    wl_surface_attach(surface_, nullptr, 0, 0);
    wl_surface_commit(surface_);
}

void WaylandSurface::set_title(std::string_view title) {
    if (xdg_toplevel_) {
        xdg_toplevel_set_title(xdg_toplevel_, std::string(title).c_str());
    }
}

void WaylandSurface::resize(int width, int height) {
    width_ = width;
    height_ = height;
    // Keep the viewport destination in sync with the logical size while fractional scale is
    // active; otherwise the compositor would keep using the stale destination and clip/scale
    // incorrectly after a toplevel configure.
    if (viewport_ != nullptr && fractional_scale_ > 0.0F) {
        wp_viewport_set_destination(viewport_, width_, height_);
    }
}

Size WaylandSurface::size() const {
    return {static_cast<float>(width_), static_cast<float>(height_)};
}

float WaylandSurface::scale_factor() const {
    if (fractional_scale_ > 0.0F) {
        return fractional_scale_;
    }
    return static_cast<float>(buffer_scale_);
}

void WaylandSurface::handle_output_enter(wl_output* output) {
    entered_outputs_.insert(output);
    recompute_scale_factor();
}

void WaylandSurface::handle_output_leave(wl_output* output) {
    entered_outputs_.erase(output);
    recompute_scale_factor();
}

void WaylandSurface::recompute_scale_factor() {
    // When fractional-scale is active, wp_fractional_scale_v1.preferred_scale is authoritative
    // and we ignore wl_output.scale entirely — mixing them would cause double scaling.
    if (fractional_scale_ > 0.0F) {
        return;
    }

    int best = 1;
    for (wl_output* output : entered_outputs_) {
        best = std::max(best, backend_.output_scale(output));
    }
    if (best == buffer_scale_) {
        return;
    }
    buffer_scale_ = best;
    if (surface_ != nullptr) {
        wl_surface_set_buffer_scale(surface_, buffer_scale_);
        // A commit is needed for the compositor to latch the new scale. The next frame produced
        // by the window will re-attach a correctly-sized buffer; an explicit commit here makes
        // the scale-change observable immediately even if the next render is deferred.
        wl_surface_commit(surface_);
    }
    WindowEvent event{};
    event.type = WindowEvent::Type::ScaleFactorChanged;
    event.scale_factor = static_cast<float>(buffer_scale_);
    owner_.dispatch_window_event(event);
}

void WaylandSurface::handle_preferred_fractional_scale(uint32_t scale_numerator) {
    // wp_fractional_scale_v1 encodes the scale as a uint32_t numerator with an implicit
    // denominator of 120. Anything ≤ 0 is a protocol violation; guard against it anyway so a
    // misbehaving compositor can't drive us into a nonsensical state.
    if (scale_numerator == 0) {
        return;
    }
    const float next = static_cast<float>(scale_numerator) / 120.0F;
    if (std::abs(next - fractional_scale_) < 1e-4F) {
        return;
    }

    fractional_scale_ = next;
    // Drop any integer buffer-scale once fractional is active. Keeping them both would cause
    // the compositor to multiply our oversized buffer a second time.
    if (buffer_scale_ != 1 && surface_ != nullptr) {
        wl_surface_set_buffer_scale(surface_, 1);
    }
    buffer_scale_ = 1;

    // Tell the compositor the logical surface size. The buffer we attach in present() will be
    // `logical * fractional_scale_` physical pixels; the viewport destination declares the
    // logical size so the compositor can composite at the right footprint.
    if (viewport_ != nullptr) {
        wp_viewport_set_destination(viewport_, width_, height_);
    }
    if (surface_ != nullptr) {
        wl_surface_commit(surface_);
    }

    WindowEvent event{};
    event.type = WindowEvent::Type::ScaleFactorChanged;
    event.scale_factor = fractional_scale_;
    owner_.dispatch_window_event(event);
}

RendererBackendSupport WaylandSurface::renderer_backend_support() const {
    return {
        .software = true,
        .d3d11 = false,
        .metal = false,
        .open_gl = false,
        .vulkan = true,
    };
}

void WaylandSurface::present(const uint8_t* rgba,
                             int w,
                             int h,
                             std::span<const Rect> damage_regions) {
    if (!configured_) {
        return;
    }

    // Pick a non-busy buffer for double-buffering.
    int idx = current_buffer_;
    if (buffers_[idx].busy) {
        idx = 1 - idx;
    }
    if (buffers_[idx].busy) {
        return; // Both buffers in use — skip this frame.
    }

    // Capture the geometry change before (re)allocating: a resize that preserves the byte size
    // (e.g. 800x600 -> 600x800) still requires a full re-upload, and comparing geometry avoids
    // the `w * h * 4` int overflow the old byte-size check had.
    const bool size_changed = buffers_[idx].width != w || buffers_[idx].height != h;

    ensure_buffer(buffers_[idx], w, h);
    if (!buffers_[idx].data) {
        return;
    }

    auto convert_region = [&](Rect rect) {
        const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
        const int x1 = std::min(w, static_cast<int>(std::ceil(rect.right())));
        const int y1 = std::min(h, static_cast<int>(std::ceil(rect.bottom())));
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const int off = (y * w + x) * 4;
                buffers_[idx].data[off + 0] = rgba[off + 2]; // B
                buffers_[idx].data[off + 1] = rgba[off + 1]; // G
                buffers_[idx].data[off + 2] = rgba[off + 0]; // R
                buffers_[idx].data[off + 3] = rgba[off + 3]; // A
            }
        }
    };

    const bool full_upload = damage_regions.empty() || !buffers_[idx].initialized || size_changed;
    if (!full_upload && last_presented_buffer_ >= 0 && last_presented_buffer_ != idx &&
        buffers_[last_presented_buffer_].data != nullptr &&
        buffers_[last_presented_buffer_].width == w &&
        buffers_[last_presented_buffer_].height == h &&
        buffers_[last_presented_buffer_].pool_size == buffers_[idx].pool_size) {
        std::memcpy(
            buffers_[idx].data, buffers_[last_presented_buffer_].data, buffers_[idx].pool_size);
    }

    if (full_upload) {
        convert_region({0.0F, 0.0F, static_cast<float>(w), static_cast<float>(h)});
    } else {
        for (const auto& rect : damage_regions) {
            convert_region(rect);
        }
    }
    buffers_[idx].initialized = true;

    buffers_[idx].busy = true;
    current_buffer_ = 1 - idx;
    last_presented_buffer_ = idx;

    wl_surface_attach(surface_, buffers_[idx].buffer, 0, 0);
    if (full_upload) {
        wl_surface_damage_buffer(surface_, 0, 0, w, h);
    } else {
        for (const auto& rect : damage_regions) {
            const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
            const int x1 = std::min(w, static_cast<int>(std::ceil(rect.right())));
            const int y1 = std::min(h, static_cast<int>(std::ceil(rect.bottom())));
            if (x1 > x0 && y1 > y0) {
                wl_surface_damage_buffer(surface_, x0, y0, x1 - x0, y1 - y0);
            }
        }
    }
    wl_surface_commit(surface_);
}

void WaylandSurface::set_fullscreen(bool fullscreen) {
    if (xdg_toplevel_ == nullptr) {
        return;
    }
    if (fullscreen) {
        xdg_toplevel_set_fullscreen(xdg_toplevel_, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(xdg_toplevel_);
    }
}

bool WaylandSurface::is_fullscreen() const {
    return fullscreen_;
}

void WaylandSurface::minimize() {
    if (xdg_toplevel_) {
        xdg_toplevel_set_minimized(xdg_toplevel_);
    }
}

void WaylandSurface::toggle_maximize() {
    if (xdg_toplevel_ == nullptr) {
        return;
    }
    if (maximized_) {
        xdg_toplevel_unset_maximized(xdg_toplevel_);
    } else {
        xdg_toplevel_set_maximized(xdg_toplevel_);
    }
}

bool WaylandSurface::is_maximized() const {
    return maximized_;
}

bool WaylandSurface::uses_client_side_decorations() const {
    return decorated_ && client_side_decoration_ && !fullscreen_;
}

bool WaylandSurface::begin_system_move(std::uint32_t serial) {
    auto* input = backend_.input();
    if (!uses_client_side_decorations() || maximized_ || serial == 0 || input == nullptr ||
        input->seat() == nullptr || xdg_toplevel_ == nullptr) {
        return false;
    }
    xdg_toplevel_move(xdg_toplevel_, input->seat(), serial);
    return true;
}

NativeWindowHandle WaylandSurface::native_handle() const {
    return static_cast<NativeWindowHandle>(surface_);
}

NativeWindowHandle WaylandSurface::native_display_handle() const {
    return static_cast<NativeWindowHandle>(backend_.display());
}

void WaylandSurface::set_cursor_shape(CursorShape shape) {
    if (auto* input = backend_.input(); input != nullptr) {
        input->set_cursor_shape(shape);
    }
}

void WaylandSurface::set_titlebar_style(TitlebarStyle style) {
    if (titlebar_style_ == style) {
        return;
    }
    titlebar_style_ = style;
    update_decoration_preference();
}

// ---------------------------------------------------------------------------
// XDG event handlers
// ---------------------------------------------------------------------------

void WaylandSurface::handle_configure(int width, int height, const wl_array* states) {
    bool next_maximized = false;
    bool next_fullscreen = false;
    if (states != nullptr && states->data != nullptr) {
        const auto count = states->size / sizeof(std::uint32_t);
        const auto* values = static_cast<const std::uint32_t*>(states->data);
        for (std::size_t index = 0; index < count; ++index) {
            next_maximized = next_maximized || values[index] == XDG_TOPLEVEL_STATE_MAXIMIZED;
            next_fullscreen = next_fullscreen || values[index] == XDG_TOPLEVEL_STATE_FULLSCREEN;
        }
    }
    const bool chrome_state_changed =
        maximized_ != next_maximized || fullscreen_ != next_fullscreen;
    maximized_ = next_maximized;
    fullscreen_ = next_fullscreen;

    if (width > 0 && height > 0 && (width != width_ || height != height_)) {
        width_ = width;
        height_ = height;

        WindowEvent we{};
        we.type = WindowEvent::Type::Resize;
        we.width = width;
        we.height = height;
        owner_.dispatch_window_event(we);
    }
    configured_ = true;
    if (chrome_state_changed) {
        notify_native_chrome_changed();
    }
}

void WaylandSurface::handle_decoration_configure(std::uint32_t mode) {
    const bool next_client_side = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    if (client_side_decoration_ == next_client_side) {
        return;
    }
    client_side_decoration_ = next_client_side;
    notify_native_chrome_changed();
}

void WaylandSurface::handle_close() {
    WindowEvent we{};
    we.type = WindowEvent::Type::Close;
    owner_.dispatch_window_event(we);
}

bool WaylandSurface::begin_resize_if_needed(float x, float y, std::uint32_t serial) {
    constexpr float resize_border = 6.0F;
    auto* input = backend_.input();
    if (!uses_client_side_decorations() || !resizable_ || maximized_ || serial == 0 ||
        input == nullptr || input->seat() == nullptr || xdg_toplevel_ == nullptr) {
        return false;
    }

    const bool left = x >= 0.0F && x < resize_border;
    const bool right =
        x <= static_cast<float>(width_) && x > static_cast<float>(width_) - resize_border;
    const bool top = y >= 0.0F && y < resize_border;
    const bool bottom =
        y <= static_cast<float>(height_) && y > static_cast<float>(height_) - resize_border;

    std::uint32_t edge = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    if (top && left) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    } else if (top && right) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    } else if (bottom && left) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    } else if (bottom && right) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    } else if (left) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    } else if (right) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    } else if (top) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    } else if (bottom) {
        edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    }

    if (edge == XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
        return false;
    }
    xdg_toplevel_resize(xdg_toplevel_, input->seat(), serial, edge);
    return true;
}

void WaylandSurface::ensure_decoration_object() {
    if (toplevel_decoration_ != nullptr || backend_.decoration_manager() == nullptr ||
        xdg_toplevel_ == nullptr) {
        return;
    }
    toplevel_decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(
        backend_.decoration_manager(), xdg_toplevel_);
    if (toplevel_decoration_ != nullptr) {
        zxdg_toplevel_decoration_v1_add_listener(toplevel_decoration_, &decoration_listener, this);
    }
}

void WaylandSurface::update_decoration_preference() {
    if (backend_.decoration_manager() == nullptr) {
        const bool changed = !client_side_decoration_;
        client_side_decoration_ = true;
        if (changed) {
            notify_native_chrome_changed();
        }
        return;
    }

    ensure_decoration_object();
    if (toplevel_decoration_ == nullptr) {
        return;
    }
    const bool prefer_client_side = !decorated_ || titlebar_style_ != TitlebarStyle::Regular;
    zxdg_toplevel_decoration_v1_set_mode(toplevel_decoration_,
                                         prefer_client_side
                                             ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                             : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void WaylandSurface::notify_native_chrome_changed() {
    WindowEvent event{};
    event.type = WindowEvent::Type::NativeChromeChanged;
    owner_.dispatch_window_event(event);
}

} // namespace nk
