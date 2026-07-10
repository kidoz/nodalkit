#pragma once

/// @file breakpoint.h
/// @brief Size-dependent state and a single-child adaptive container.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <span>
#include <vector>

namespace nk {

/// Inclusive size bounds that determine whether a breakpoint is active.
struct BreakpointCondition {
    std::optional<float> min_width;
    std::optional<float> max_width;
    std::optional<float> min_height;
    std::optional<float> max_height;

    [[nodiscard]] bool matches(Size size) const;
};

/// Observable state that becomes active when its condition matches the
/// allocation of the owning BreakpointBin.
class Breakpoint {
public:
    [[nodiscard]] static std::shared_ptr<Breakpoint> create(BreakpointCondition condition = {});

    [[nodiscard]] const BreakpointCondition& condition() const;
    void set_condition(BreakpointCondition condition);

    [[nodiscard]] bool is_active() const;
    Signal<bool>& on_active_changed();

private:
    friend class BreakpointBin;

    explicit Breakpoint(BreakpointCondition condition);
    void update(Size size);
    void set_active(bool active);

    BreakpointCondition condition_;
    std::optional<Size> last_size_;
    bool active_ = false;
    Signal<bool> active_changed_;
};

/// A single-child container that evaluates reusable breakpoints against its
/// allocated logical size.
class BreakpointBin : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<BreakpointBin> create();
    ~BreakpointBin() override;

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;

    void add_breakpoint(std::shared_ptr<Breakpoint> breakpoint);
    void remove_breakpoint(Breakpoint& breakpoint);
    void clear_breakpoints();
    [[nodiscard]] std::span<const std::shared_ptr<Breakpoint>> breakpoints() const;

    [[nodiscard]] bool has_height_for_width() const override;
    [[nodiscard]] float height_for_width(float width) const override;
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    BreakpointBin();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
