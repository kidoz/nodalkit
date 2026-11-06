#pragma once

/// @file level_bar.h
/// @brief Discrete meter for a known level.

#include <memory>
#include <nk/ui_core/widget.h>

namespace nk {

/// A row of blocks for a known level, such as volume, strength, or quota.
///
/// The value runs from 0 through `maximum()`. The default maximum is 4, so
/// the meter can be empty or show four filled blocks. Values outside that
/// range are clamped. The bar does not animate. `ProgressBar` remains the
/// control for task completion.
class LevelBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<LevelBar> create();
    ~LevelBar() override;

    [[nodiscard]] int value() const;
    void set_value(int value);

    [[nodiscard]] int maximum() const;
    /// The maximum is at least 1. A smaller request is raised to 1, and the
    /// current value is clamped to the new maximum.
    void set_maximum(int maximum);

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    LevelBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void publish_value();
};

} // namespace nk
