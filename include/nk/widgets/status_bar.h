#pragma once

/// @file status_bar.h
/// @brief Horizontal status bar widget with text segments.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Horizontal status bar at the bottom of a window.
class StatusBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<StatusBar> create();
    ~StatusBar() override;

    /// Set text segments separated by dividers.
    void set_segments(std::vector<std::string> segments);

    /// Update a single segment by index.
    void set_segment(std::size_t index, std::string text);

    /// Number of segments currently set.
    [[nodiscard]] std::size_t segment_count() const;

    /// Get segment text by index.
    [[nodiscard]] std::string_view segment(std::size_t index) const;

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    StatusBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
