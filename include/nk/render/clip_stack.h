#pragma once

/// @file clip_stack.h
/// @brief Bounded rounded-clip stack with lossless flattening for GPU backends.

#include <cstddef>
#include <nk/foundation/types.h>
#include <span>
#include <vector>

namespace nk {

/// One active clip: a rectangle with a uniform corner radius (0 = plain rect).
struct ClipRegion {
    Rect rect{};
    float radius = 0.0F;
};

/// Tracks the active rounded clips while walking a render tree, bounded by the
/// number of clip slots a GPU draw command can carry. Instead of failing as
/// soon as the tree nests more clips than there are slots, pushes are
/// flattened where that is exact:
///
/// - a clip whose safe inner area already contains an active clip's rect adds
///   no constraint and consumes no slot;
/// - a rectangular clip (radius 0) intersects into an existing rectangular
///   slot, since clip coverage is the product of all active clips and
///   rect ∩ rect is again a rect.
///
/// pop() undoes whatever the matching push() did (slot push, merge, or skip),
/// so the stack can be used from a recursive visitor.
class ClipStack {
public:
    explicit ClipStack(std::size_t max_depth);

    /// Apply a clip. Returns false when the clip cannot be flattened and no
    /// slot is free; the stack is unchanged in that case (do not pop).
    [[nodiscard]] bool push(ClipRegion clip);

    /// Undo the most recent successful push.
    void pop();

    /// The active clip slots, at most max_depth entries.
    [[nodiscard]] std::span<const ClipRegion> active() const;

    void clear();

private:
    enum class FrameKind : std::uint8_t {
        Pushed,
        Merged,
        Skipped,
    };

    struct Frame {
        FrameKind kind = FrameKind::Pushed;
        std::size_t slot = 0;
        ClipRegion previous{};
    };

    std::vector<ClipRegion> regions_;
    std::vector<Frame> frames_;
    std::size_t max_depth_ = 0;
};

} // namespace nk
