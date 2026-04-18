#pragma once

/// @file macos_backend.h
/// @brief macOS Cocoa platform backend (private header).

#include <memory>
#include <nk/platform/platform_backend.h>
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

    [[nodiscard]] std::unique_ptr<NativeSurface> create_surface(const WindowConfig& config,
                                                                Window& owner) override;

    int run_event_loop(EventLoop& loop) override;
    void wake_event_loop() override;
    void request_quit(int exit_code) override;

    [[nodiscard]] SpellChecker* spell_checker() override;

    [[nodiscard]] bool supports_open_file_dialog() const override;
    [[nodiscard]] OpenFileDialogResult
    show_open_file_dialog(std::string_view title, const std::vector<std::string>& filters) override;
    [[nodiscard]] bool supports_clipboard_text() const override;
    [[nodiscard]] std::string clipboard_text() const override;
    void set_clipboard_text(std::string_view text) override;
    [[nodiscard]] bool supports_native_app_menu() const override;
    void set_native_app_menu(std::span<const NativeMenu> menus,
                             NativeMenuActionHandler action_handler) override;

    [[nodiscard]] SystemPreferences system_preferences() const override;
    [[nodiscard]] bool supports_system_preferences_observation() const override;
    void start_system_preferences_observation(SystemPreferencesObserver observer) override;
    void stop_system_preferences_observation() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
