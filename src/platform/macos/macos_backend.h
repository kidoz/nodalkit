#pragma once

/// @file macos_backend.h
/// @brief macOS Cocoa platform backend (private header).

#include <nk/platform/platform_backend.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class MacosBackend : public PlatformBackend {
public:
    MacosBackend();
    ~MacosBackend() override;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
