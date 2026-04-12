#pragma once

/// @file slider.h
/// @brief Continuous or stepped value selection widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// A horizontal or vertical slider for selecting a numeric value.
class Slider : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Slider> create(
        Orientation orientation = Orientation::Horizontal);
    ~Slider() override;

    [[nodiscard]] Orientation orientation() const;

    /// Current value in [min, max].
    [[nodiscard]] double value() const;
    void set_value(double value);

    /// Value range. Default [0, 1].
    [[nodiscard]] double minimum() const;
    [[nodiscard]] double maximum() const;
    void set_range(double min, double max);

    /// Step size for keyboard increments. 0 means continuous.
    [[nodiscard]] double step() const;
    void set_step(double step);

    /// Emitted when value changes.
    Signal<double>& on_value_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit Slider(Orientation orientation);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
