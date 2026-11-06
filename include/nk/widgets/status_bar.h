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
///
/// Each segment is text, or one child widget such as a progress bar. Dividers
/// stay between segments, and `set_trailing()` stays at the trailing edge.
/// A resize grip is drawn at the bottom-trailing corner when the bar is in a
/// resizable window that uses client-side decorations and is not maximized.
/// Dragging the grip resizes that corner. The grip stays hidden when the
/// window is not resizable, is maximized, or the platform draws the corner.
class StatusBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<StatusBar> create();
    ~StatusBar() override;

    /// Set text segments separated by dividers.
    void set_segments(std::vector<std::string> segments);

    /// Update a single segment by index.
    void set_segment(std::size_t index, std::string text);

    /// Place one widget in a segment. Null clears it and the segment text
    /// shows again.
    void set_segment_widget(std::size_t index, std::shared_ptr<Widget> widget);
    [[nodiscard]] Widget* segment_widget(std::size_t index) const;

    /// Number of segments currently set.
    [[nodiscard]] std::size_t segment_count() const;

    /// Get segment text by index.
    [[nodiscard]] std::string_view segment(std::size_t index) const;

    /// Set trailing text drawn right-aligned in the secondary text color.
    void set_trailing(std::string text);

    /// Get the trailing text.
    [[nodiscard]] std::string_view trailing() const;

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    StatusBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct SegmentSlot {
        Rect bounds{};
        float width = 0.0F;
    };

    struct Placement {
        std::vector<SegmentSlot> segments;
        Rect trailing{};
        Rect grip{};
        bool has_grip = false;
    };

    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] bool shows_resize_grip() const;
    [[nodiscard]] Placement placement() const;
    void resize_from_grip(Point point);
    void sync_widget_slots();
};

} // namespace nk
