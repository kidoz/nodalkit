#pragma once

/// @file info_bar.h
/// @brief In-window notification bar.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// Severity level for the info bar.
enum class InfoBarSeverity : uint8_t {
    Info,
    Warning,
    Error,
    Success,
};

/// An in-window notification bar with a message and optional close button.
class InfoBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<InfoBar> create(
        std::string message = {},
        InfoBarSeverity severity = InfoBarSeverity::Info);
    ~InfoBar() override;

    [[nodiscard]] std::string_view message() const;
    void set_message(std::string message);

    [[nodiscard]] InfoBarSeverity severity() const;
    void set_severity(InfoBarSeverity severity);

    [[nodiscard]] bool is_closable() const;
    void set_closable(bool closable);

    /// Emitted when the user dismisses the bar.
    Signal<>& on_dismissed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    InfoBar(std::string message, InfoBarSeverity severity);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
