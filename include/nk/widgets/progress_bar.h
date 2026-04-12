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

    /// Completion fraction in [0, 1]. Negative means indeterminate.
    [[nodiscard]] float fraction() const;
    void set_fraction(float fraction);

    /// Nudge the indeterminate animation forward. Call this periodically
    /// when fraction() is negative to animate the pulsing indicator.
    void pulse();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    ProgressBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
