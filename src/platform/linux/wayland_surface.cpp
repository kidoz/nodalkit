/// @file wayland_surface.cpp
/// @brief Wayland native surface implementation.

#include "wayland_surface.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wayland_backend.h"
#include "wayland_input.h"
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
                                          struct wl_array* /*states*/) {
    auto* self = static_cast<WaylandSurface*>(data);
    if (width > 0 && height > 0) {
        self->handle_configure(width, height);
    }
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
    : backend_(backend), owner_(owner), width_(config.width), height_(config.height) {

    surface_ = wl_compositor_create_surface(backend_.compositor());
    wl_surface_add_listener(surface_, &wl_surface_listener, this);

    xdg_surface_ = xdg_wm_base_get_xdg_surface(backend_.wm_base(), surface_);
    xdg_surface_add_listener(xdg_surface_, &xdg_surface_listener, this);

    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(xdg_toplevel_, &xdg_toplevel_listener, this);
    xdg_toplevel_set_title(xdg_toplevel_, config.title.c_str());

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
    buf.busy = false;
    buf.initialized = false;
}

void WaylandSurface::ensure_buffer(ShmBuffer& buf, int w, int h) {
    const auto needed = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    if (buf.buffer && buf.pool_size == needed) {
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

    const bool size_changed = buffers_[idx].pool_size != static_cast<size_t>(w * h * 4);
    const bool full_upload = damage_regions.empty() || !buffers_[idx].initialized || size_changed;
    if (!full_upload && last_presented_buffer_ >= 0 && last_presented_buffer_ != idx &&
        buffers_[last_presented_buffer_].data != nullptr &&
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
    if (fullscreen_ == fullscreen) {
        return;
    }
    fullscreen_ = fullscreen;
    if (xdg_toplevel_) {
        if (fullscreen) {
            xdg_toplevel_set_fullscreen(xdg_toplevel_, nullptr);
        } else {
            xdg_toplevel_unset_fullscreen(xdg_toplevel_);
        }
    }
}

bool WaylandSurface::is_fullscreen() const {
    return fullscreen_;
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

// ---------------------------------------------------------------------------
// XDG event handlers
// ---------------------------------------------------------------------------

void WaylandSurface::handle_configure(int width, int height) {
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
}

void WaylandSurface::handle_close() {
    WindowEvent we{};
    we.type = WindowEvent::Type::Close;
    owner_.dispatch_window_event(we);
}

} // namespace nk
