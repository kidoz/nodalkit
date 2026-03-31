#pragma once

/// @file button.h
/// @brief Clickable button widget.

#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

#include <memory>
#include <string>
#include <string_view>

namespace nk {

/// A push button that emits on_clicked() when activated.
class Button : public Widget {
public:
    /// Create a button with the given label.
    [[nodiscard]] static std::shared_ptr<Button> create(
        std::string label = {});

    ~Button() override;

    /// Get or set the button label text.
    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    /// Signal emitted when the button is clicked.
    Signal<>& on_clicked();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(
        Constraints const& constraints) const override;
    bool handle_mouse_event(MouseEvent const& event) override;
    bool handle_key_event(KeyEvent const& event) override;

protected:
    explicit Button(std::string label);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
