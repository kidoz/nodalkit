#pragma once

/// @file scroll_area.h
/// @brief Scrollable viewport container.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Scroll policy for each axis.
enum class ScrollPolicy { Never, Always, Automatic };

/// A scrollable viewport that can contain a single child larger
/// than the visible area.
class ScrollArea : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ScrollArea> create();
    ~ScrollArea() override;

    /// Set the scrollable content widget.
    void set_content(std::shared_ptr<Widget> content);
    [[nodiscard]] Widget* content() const;

    /// Scroll policies.
    void set_h_scroll_policy(ScrollPolicy policy);
    void set_v_scroll_policy(ScrollPolicy policy);

    /// Current scroll offset.
    [[nodiscard]] float h_offset() const;
    [[nodiscard]] float v_offset() const;
    void scroll_to(float h, float v);

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
