/// @file wayland_surface.cpp
/// @brief Wayland native surface implementation.

#include "wayland_surface.h"

#include "wayland_backend.h"
#include "xdg-shell-client-protocol.h"

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
// WaylandSurface
// ---------------------------------------------------------------------------

WaylandSurface::WaylandSurface(WaylandBackend& backend, const WindowConfig& config, Window& owner)
    : backend_(backend), owner_(owner), width_(config.width), height_(config.height) {

    surface_ = wl_compositor_create_surface(backend_.compositor());

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
}

Size WaylandSurface::size() const {
    return {static_cast<float>(width_), static_cast<float>(height_)};
}

float WaylandSurface::scale_factor() const {
    return 1.0F;
}

void WaylandSurface::present(const uint8_t* rgba, int w, int h) {
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

    // Convert RGBA8 (R,G,B,A) → WL_SHM_FORMAT_ARGB8888 (B,G,R,A on LE).
    uint8_t* dst = buffers_[idx].data;
    const int pixel_count = w * h;
    for (int i = 0; i < pixel_count; ++i) {
        const int off = i * 4;
        dst[off + 0] = rgba[off + 2]; // B
        dst[off + 1] = rgba[off + 1]; // G
        dst[off + 2] = rgba[off + 0]; // R
        dst[off + 3] = rgba[off + 3]; // A
    }

    buffers_[idx].busy = true;
    current_buffer_ = 1 - idx;

    wl_surface_attach(surface_, buffers_[idx].buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, w, h);
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

void WaylandSurface::set_cursor_shape(CursorShape /*shape*/) {
    // Window/runtime cursor routing is in place; Wayland-specific cursor
    // surface integration still needs backend protocol work.
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
