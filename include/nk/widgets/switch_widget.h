#pragma once

/// @file switch_widget.h
/// @brief On/off toggle switch widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// An on/off toggle switch, styled as a sliding track with a thumb.
class Switch : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Switch> create();
    ~Switch() override;

    [[nodiscard]] bool is_active() const;
    void set_active(bool active);

    /// Emitted when the active state changes.
    Signal<bool>& on_toggled();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    Switch();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
