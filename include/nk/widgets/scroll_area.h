#pragma once

/// @file scroll_area.h
/// @brief Scrollable viewport container.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Per-axis scroll policy.
enum class ScrollPolicy {
    /// Never scroll on this axis; the content is constrained to the viewport
    /// size and any content overflow is clipped without a scrollbar.
    Never,
    /// Always show the scrollbar and allow scrolling, even when content fits.
    Always,
    /// Show the scrollbar and allow scrolling only when the content's natural
    /// size exceeds the viewport on this axis. This is the default.
    Automatic,
};

/// A scrollable viewport holding a single child widget.
///
/// ScrollArea measures its child at its natural (unbounded) size on the
/// scrollable axes, then allocates the child at that size and offsets it by
/// the current scroll position. Mouse wheel, trackpad precise scrolling,
/// scrollbar thumb dragging, and scrollbar track click-to-jump are all
/// handled by handle_mouse_event().
class ScrollArea : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ScrollArea> create();
    ~ScrollArea() override;

    /// Set the single scrollable child. Replaces any previous content.
    /// Passing nullptr removes the current content.
    void set_content(std::shared_ptr<Widget> content);
    /// Non-owning pointer to the current content, or nullptr if none is set.
    [[nodiscard]] Widget* content() const;

    /// Set the horizontal scroll policy. Defaults to Automatic.
    void set_h_scroll_policy(ScrollPolicy policy);
    /// Set the vertical scroll policy. Defaults to Automatic.
    void set_v_scroll_policy(ScrollPolicy policy);

    /// Current horizontal scroll offset in logical pixels. Always in
    /// [0, max_h_offset].
    [[nodiscard]] float h_offset() const;
    /// Current vertical scroll offset in logical pixels. Always in
    /// [0, max_v_offset].
    [[nodiscard]] float v_offset() const;
    /// Scroll to the given offset. Values are clamped to the valid range,
    /// and on_scroll_changed() fires only if either clamped offset actually
    /// differs from the current value.
    void scroll_to(float h, float v);

    /// Emitted after a successful scroll with the new (h, v) offsets.
    /// Not emitted for no-op scrolls (same clamped value as before).
    Signal<float, float>& on_scroll_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;

protected:
    ScrollArea();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
