#pragma once

/// @file win32_backend.h
/// @brief Win32 platform backend (private header).

#include <memory>
#include <nk/platform/platform_backend.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class Win32Backend : public PlatformBackend {
public:
    Win32Backend();
    ~Win32Backend() override;

    [[nodiscard]] Result<void> initialize() override;
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

    [[nodiscard]] SystemPreferences system_preferences() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
