#pragma once

/// @file breadcrumb.h
/// @brief Path/location navigation bar.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A breadcrumb trail showing a navigable path.
///
/// Earlier segments are links. The last segment is the current location.
/// Left, Right, Home, and End move among the earlier segments. Return or
/// Space reports the highlighted segment. When the path is wider than the
/// bar, the current segment stays visible and the hidden leading segments
/// open from an overflow menu. `on_navigate()` reports an index. The path
/// stays as it was set.
class Breadcrumb : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Breadcrumb> create();
    ~Breadcrumb() override;

    /// Replace the full path.
    void set_path(std::vector<std::string> segments);

    /// Append a segment.
    void push(std::string segment);

    /// Remove segments after the given depth.
    void pop_to(std::size_t depth);

    [[nodiscard]] std::size_t depth() const;
    [[nodiscard]] std::string_view segment(std::size_t index) const;

    /// Emitted with the index of an earlier segment. The path is unchanged.
    Signal<std::size_t>& on_navigate();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    Breadcrumb();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct SegmentSlot {
        int index = -1;
        Rect bounds{};
        float text_width = 0.0F;
    };

    struct Placement {
        std::vector<SegmentSlot> visible;
        Rect overflow{};
        bool has_overflow = false;
    };

    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] float bar_height() const;
    [[nodiscard]] float segment_width(std::string_view text) const;
    [[nodiscard]] float separator_advance() const;
    [[nodiscard]] int clickable_count() const;
    [[nodiscard]] Placement placement() const;
    void activate_segment(std::size_t index);
    void open_overflow();
    void clamp_keyboard_index();
};

} // namespace nk
