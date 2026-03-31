#pragma once

/// @file wayland_backend.h
/// @brief Wayland platform backend (private header).

#include <nk/platform/platform_backend.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct wl_display;
struct wl_compositor;
struct wl_shm;
struct wl_seat;
struct wl_surface;
struct xdg_wm_base;

namespace nk {

class WaylandSurface;

class WaylandBackend : public PlatformBackend {
public:
    WaylandBackend();
    ~WaylandBackend() override;

    Result<void> initialize() override;
    void shutdown() override;

    [[nodiscard]] std::unique_ptr<NativeSurface> create_surface(
        WindowConfig const& config, Window& owner) override;

    int run_event_loop(EventLoop& loop) override;
    void wake_event_loop() override;
    void request_quit(int exit_code) override;

    [[nodiscard]] Result<std::string> show_open_file_dialog(
        std::string_view title,
        std::vector<std::string> const& filters) override;

    [[nodiscard]] SystemPreferences system_preferences() const override;
    [[nodiscard]] bool supports_system_preferences_observation() const override;
    void start_system_preferences_observation(
        SystemPreferencesObserver observer) override;
    void stop_system_preferences_observation() override;

    // Accessors for Wayland globals (used by WaylandSurface / WaylandInput).
    [[nodiscard]] wl_display* display() const;
    [[nodiscard]] wl_compositor* compositor() const;
    [[nodiscard]] wl_shm* shm() const;
    [[nodiscard]] xdg_wm_base* wm_base() const;

    // Surface registration for input event routing.
    void register_surface(wl_surface* wl_surf, WaylandSurface* surface);
    void unregister_surface(wl_surface* wl_surf);
    [[nodiscard]] WaylandSurface* find_surface(wl_surface* wl_surf) const;

    // Public so that C-style Wayland callbacks can cast the data pointer.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
