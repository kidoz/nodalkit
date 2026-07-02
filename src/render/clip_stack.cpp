#include <algorithm>
#include <nk/render/clip_stack.h>

namespace nk {

namespace {

[[nodiscard]] bool rect_contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
           inner.bottom() <= outer.bottom();
}

/// The area a rounded clip covers at full opacity, shrunk by the corner radius
/// (which over-covers the straight edges — conservative) plus a half-pixel
/// anti-aliasing margin.
[[nodiscard]] Rect safe_inner_rect(const ClipRegion& clip) {
    const float inset = std::max(0.0F, clip.radius) + 0.5F;
    return {clip.rect.x + inset,
            clip.rect.y + inset,
            std::max(0.0F, clip.rect.width - (inset * 2.0F)),
            std::max(0.0F, clip.rect.height - (inset * 2.0F))};
}

[[nodiscard]] Rect intersect_rects(const Rect& a, const Rect& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.right(), b.right());
    const float y1 = std::min(a.bottom(), b.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

} // namespace

ClipStack::ClipStack(std::size_t max_depth) : max_depth_(max_depth) {
    regions_.reserve(max_depth);
}

bool ClipStack::push(ClipRegion clip) {
    // The clip adds no constraint when everything that passes the active
    // clips already lies in its fully-covered interior: the pass region is a
    // subset of every active clip's rect, so containing one of them suffices.
    const Rect safe_inner = safe_inner_rect(clip);
    const bool redundant =
        !regions_.empty() &&
        std::any_of(regions_.begin(), regions_.end(), [&](const ClipRegion& region) {
            return rect_contains(safe_inner, region.rect);
        });
    if (redundant) {
        frames_.push_back({.kind = FrameKind::Skipped, .slot = 0, .previous = {}});
        return true;
    }

    // Coverage is the product of all active clips, so two rectangular clips
    // collapse into their intersection without consuming a second slot.
    if (clip.radius <= 0.0F) {
        for (std::size_t slot = 0; slot < regions_.size(); ++slot) {
            if (regions_[slot].radius > 0.0F) {
                continue;
            }
            frames_.push_back(
                {.kind = FrameKind::Merged, .slot = slot, .previous = regions_[slot]});
            regions_[slot].rect = intersect_rects(regions_[slot].rect, clip.rect);
            return true;
        }
    }

    if (regions_.size() >= max_depth_) {
        return false;
    }
    frames_.push_back({.kind = FrameKind::Pushed, .slot = regions_.size(), .previous = {}});
    regions_.push_back(clip);
    return true;
}

void ClipStack::pop() {
    if (frames_.empty()) {
        return;
    }
    const Frame frame = frames_.back();
    frames_.pop_back();
    switch (frame.kind) {
    case FrameKind::Pushed:
        regions_.pop_back();
        break;
    case FrameKind::Merged:
        regions_[frame.slot] = frame.previous;
        break;
    case FrameKind::Skipped:
        break;
    }
}

std::span<const ClipRegion> ClipStack::active() const {
    return regions_;
}

void ClipStack::clear() {
    regions_.clear();
    frames_.clear();
}

} // namespace nk
