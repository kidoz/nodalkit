#pragma once

/// @file progress_bar.h
/// @brief Determinate and indeterminate progress indicator widget.

#include <memory>
#include <nk/ui_core/widget.h>

namespace nk {

/// A horizontal progress bar showing task completion.
class ProgressBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ProgressBar> create();
    ~ProgressBar() override;

    /// Completion fraction in [0, 1]. A negative value requests the
    /// indeterminate indicator.
    ///
    /// While the fraction is negative, a visible bar in a visible window
    /// advances on its own timer. Reduced motion, hiding the bar, hiding the
    /// window, and removing it from the window hold the indicator still.
    /// snapshot() does not advance it.
    [[nodiscard]] float fraction() const;
    void set_fraction(float fraction);

    /// Advance the indeterminate indicator by one tenth of its travel, wrapping
    /// past the end. The step is applied even when the timer is stopped,
    /// including while reduced motion is in effect.
    void pulse();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    ProgressBar();
    void snapshot(SnapshotContext& ctx) const override;
    void on_host_window_changed(Window* window) override;

private:
    void sync_motion();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
