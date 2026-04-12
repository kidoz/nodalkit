#pragma once

/// @file color_well.h
/// @brief Color selection button.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// A small button that displays a color and emits a signal when clicked.
class ColorWell : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ColorWell> create(Color color = {0.5F, 0.5F, 0.5F, 1.0F});
    ~ColorWell() override;

    [[nodiscard]] Color color() const;
    void set_color(Color color);

    /// Emitted when the well is clicked (to open a color picker).
    Signal<>& on_activated();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit ColorWell(Color color);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
