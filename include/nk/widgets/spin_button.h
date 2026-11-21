#pragma once

/// @file spin_button.h
/// @brief Numeric input with increment/decrement buttons.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/text_field.h>

namespace nk {

/// A numeric entry field with increment and decrement buttons.
/// Type a value directly or use the arrows to step. Return or loss of focus
/// restores formatted text. Escape discards an unfinished edit.
class SpinButton : public TextField {
public:
    [[nodiscard]] static std::shared_ptr<SpinButton> create();
    ~SpinButton() override;

    [[nodiscard]] double value() const;
    void set_value(double value);

    [[nodiscard]] double minimum() const;
    [[nodiscard]] double maximum() const;
    void set_range(double min, double max);

    [[nodiscard]] double step() const;
    void set_step(double step);

    /// Number of decimal places to display.
    [[nodiscard]] int precision() const;
    void set_precision(int digits);

    Signal<double>& on_value_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    SpinButton();
    void snapshot(SnapshotContext& ctx) const override;
    [[nodiscard]] Rect text_rect() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
