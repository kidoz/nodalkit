#pragma once

/// @file split_view.h
/// @brief Resizable split between two panes.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// A container that splits its area between two children with a
/// draggable divider.
class SplitView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<SplitView> create(
        Orientation orientation = Orientation::Horizontal);
    ~SplitView() override;

    [[nodiscard]] Orientation orientation() const;

    /// Set the two panes. Either may be nullptr to leave a pane empty.
    void set_start_child(std::shared_ptr<Widget> child);
    void set_end_child(std::shared_ptr<Widget> child);

    /// Split position as a fraction of total size in [0, 1].
    [[nodiscard]] float position() const;
    void set_position(float fraction);

    /// Minimum size for each pane in logical pixels.
    void set_min_start_size(float size);
    void set_min_end_size(float size);

    Signal<float>& on_position_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit SplitView(Orientation orientation);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
