#pragma once

/// @file wayland_backend.h
/// @brief Wayland platform backend (private header).

#include <memory>
#include <nk/platform/platform_backend.h>
#include <string>
#include <string_view>
#include <vector>

struct wl_display;
struct wl_compositor;
struct wl_shm;
struct wl_seat;
struct wl_surface;
struct wl_output;
struct wl_data_device_manager;
struct xdg_wm_base;
struct zwp_text_input_manager_v3;
struct zwp_primary_selection_device_manager_v1;
struct wp_cursor_shape_manager_v1;

namespace nk {

class WaylandInput;
class WaylandSurface;

class WaylandBackend : public PlatformBackend {
public:
    WaylandBackend();
    ~WaylandBackend() override;

    Result<void> initialize() override;
    void shutdown() override;

    [[nodiscard]] std::unique_ptr<NativeSurface> create_surface(const WindowConfig& config,
                                                                Window& owner) override;

    int run_event_loop(EventLoop& loop) override;
    void wake_event_loop() override;
    void request_quit(int exit_code) override;

    [[nodiscard]] bool supports_open_file_dialog() const override;
    [[nodiscard]] OpenFileDialogResult
    show_open_file_dialog(std::string_view title, const std::vector<std::string>& filters) override;
    [[nodiscard]] bool supports_clipboard_text() const override;
    [[nodiscard]] std::string clipboard_text() const override;
    void set_clipboard_text(std::string_view text) override;
    [[nodiscard]] bool supports_primary_selection_text() const override;
    [[nodiscard]] std::string primary_selection_text() const override;
    void set_primary_selection_text(std::string_view text) override;

    [[nodiscard]] SystemPreferences system_preferences() const override;
    [[nodiscard]] bool supports_system_preferences_observation() const override;
    void start_system_preferences_observation(SystemPreferencesObserver observer) override;
    void stop_system_preferences_observation() override;

    // Accessors for Wayland globals (used by WaylandSurface / WaylandInput).
    [[nodiscard]] wl_display* display() const;
    [[nodiscard]] wl_compositor* compositor() const;
    [[nodiscard]] wl_shm* shm() const;
    [[nodiscard]] xdg_wm_base* wm_base() const;
    [[nodiscard]] wl_data_device_manager* data_device_manager() const;
    [[nodiscard]] zwp_text_input_manager_v3* text_input_manager() const;
    [[nodiscard]] zwp_primary_selection_device_manager_v1* primary_selection_manager() const;
    [[nodiscard]] wp_cursor_shape_manager_v1* cursor_shape_manager() const;
    [[nodiscard]] WaylandInput* input() const;

    // Returns the compositor-advertised integer scale for the given wl_output, or 1 when the
    // output is unknown or never advertised a scale. Fractional scales are not yet supported;
    // they require wp_fractional_scale_v1 + wp_viewporter and are tracked separately.
    [[nodiscard]] int output_scale(wl_output* output) const;

    // Surface registration for input event routing.
    void register_surface(wl_surface* wl_surf, WaylandSurface* surface);
    void unregister_surface(wl_surface* wl_surf);
    [[nodiscard]] WaylandSurface* find_surface(wl_surface* wl_surf) const;

    // Public so that C-style Wayland callbacks can cast the data pointer.
    struct Impl;

private:
    void start_accessibility_thread();
    void stop_accessibility_thread();
    void schedule_accessibility_sync();

    std::unique_ptr<Impl> impl_;
};

} // namespace nk
