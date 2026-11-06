#pragma once

/// @file revealer.h
/// @brief Shows and hides one child by changing its height.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Shows one child by height.
///
/// `set_revealed(true)` gives the child its full height and makes it visible.
/// `set_revealed(false)` collapses the height to zero and keeps the child out
/// of hit testing. When motion is allowed, the height moves between those
/// sizes. Reduced motion, and a missing event loop, jump to the end size on
/// the same call. Measure follows the current height so widgets below move
/// when the parent lays out again.
class Revealer : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Revealer> create();
    ~Revealer() override;

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;

    [[nodiscard]] bool is_revealed() const;
    void set_revealed(bool revealed);

    /// 0 when collapsed and 1 when fully shown. Values in between are a slide.
    [[nodiscard]] float reveal_fraction() const;

    /// Emitted when the height reaches the requested end size.
    Signal<>& on_settled();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    [[nodiscard]] bool hit_test(Point point) const override;

protected:
    Revealer();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] bool prefers_reduced_motion() const;
    void cancel_timer();
    void start_animation();
    void finish_reveal();
};

} // namespace nk
