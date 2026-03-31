#pragma once

/// @file application.h
/// @brief Application lifecycle and global state.

#include <nk/foundation/result.h>
#include <nk/foundation/signal.h>
#include <nk/platform/system_preferences.h>
#include <nk/runtime/event_loop.h>
#include <nk/style/theme_selection.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class PlatformBackend;

/// Configuration for Application construction.
struct ApplicationConfig {
    std::string app_id;    ///< Reverse-DNS application identifier.
    std::string app_name;  ///< Human-readable application name.
};

/// Represents the top-level application. Owns the event loop and manages
/// global platform state. Exactly one Application should exist per process.
///
/// Usage:
/// @code
///   nk::Application app({.app_id = "com.example.hello",
///                        .app_name = "Hello"});
///   // ... create windows, connect signals ...
///   return app.run();
/// @endcode
class Application {
public:
    /// Construct from command-line arguments (for platform integration).
    Application(int argc, char** argv);

    /// Construct with explicit config.
    explicit Application(ApplicationConfig config);

    ~Application();

    Application(Application const&) = delete;
    Application& operator=(Application const&) = delete;

    /// Run the application event loop. Blocks until quit() is called.
    int run();

    /// Request application shutdown.
    void quit(int exit_code = 0);

    /// Access the application event loop.
    [[nodiscard]] EventLoop& event_loop();

    /// Application identifier.
    [[nodiscard]] std::string_view app_id() const;

    /// Human-readable name.
    [[nodiscard]] std::string_view app_name() const;

    /// Current platform-derived visual preferences.
    [[nodiscard]] SystemPreferences const& system_preferences() const;

    /// Refresh platform-derived visual preferences and re-apply theme
    /// selection policy.
    void refresh_system_preferences();

    /// Current application theme selection policy.
    [[nodiscard]] ThemeSelection const& theme_selection() const;

    /// Set the theme selection policy and immediately re-resolve the active
    /// theme.
    void set_theme_selection(ThemeSelection selection);

    /// Enable or disable automatic system-preference observation.
    /// Uses native backend notifications when available and falls back to
    /// polling otherwise.
    void set_system_preferences_observation_enabled(bool enabled);

    /// Whether automatic system-preference observation is active.
    [[nodiscard]] bool is_system_preferences_observation_enabled() const;

    /// Set the fallback polling interval used when native backend
    /// observation is unavailable.
    void set_system_preferences_observation_interval(
        std::chrono::milliseconds interval);

    /// Fallback polling interval used for automatic preference observation.
    [[nodiscard]] std::chrono::milliseconds
    system_preferences_observation_interval() const;

    /// Get the running Application instance (nullptr if none).
    [[nodiscard]] static Application* instance();

    /// Access the platform backend.
    [[nodiscard]] PlatformBackend& platform_backend();

    /// Show a native "open file" dialog. Returns the selected path
    /// or an error if cancelled.
    [[nodiscard]] Result<std::string> open_file_dialog(
        std::string_view title = "Open",
        std::vector<std::string> const& filters = {});

    /// Emitted when system preferences are refreshed.
    [[nodiscard]] Signal<SystemPreferences const&>& on_system_preferences_changed();

    /// Emitted when theme selection policy changes.
    [[nodiscard]] Signal<ThemeSelection const&>& on_theme_selection_changed();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
