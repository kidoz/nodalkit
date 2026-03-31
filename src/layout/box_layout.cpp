#include <nk/layout/box_layout.h>

#include <nk/ui_core/widget.h>

#include <algorithm>
#include <numeric>

namespace nk {

BoxLayout::BoxLayout(Orientation orientation)
    : orientation_(orientation) {}

BoxLayout::~BoxLayout() = default;

float BoxLayout::spacing() const { return spacing_; }
void BoxLayout::set_spacing(float spacing) { spacing_ = spacing; }

bool BoxLayout::homogeneous() const { return homogeneous_; }
void BoxLayout::set_homogeneous(bool homogeneous) { homogeneous_ = homogeneous; }

SizeRequest BoxLayout::measure(
    Widget const& widget, Constraints const& /*constraints*/) const {
    SizeRequest result{};

    auto const kids = widget.children();
    bool const vertical = (orientation_ == Orientation::Vertical);
    float total_spacing =
        kids.empty() ? 0.0F
                     : spacing_ * static_cast<float>(kids.size() - 1);

    for (auto const& child : kids) {
        if (!child->is_visible()) {
            continue;
        }
        auto const child_req = child->measure(Constraints::unbounded());

        if (vertical) {
            result.minimum_width =
                std::max(result.minimum_width, child_req.minimum_width);
            result.natural_width =
                std::max(result.natural_width, child_req.natural_width);
            result.minimum_height += child_req.minimum_height;
            result.natural_height += child_req.natural_height;
        } else {
            result.minimum_height =
                std::max(result.minimum_height, child_req.minimum_height);
            result.natural_height =
                std::max(result.natural_height, child_req.natural_height);
            result.minimum_width += child_req.minimum_width;
            result.natural_width += child_req.natural_width;
        }
    }

    if (vertical) {
        result.minimum_height += total_spacing;
        result.natural_height += total_spacing;
    } else {
        result.minimum_width += total_spacing;
        result.natural_width += total_spacing;
    }

    return result;
}

void BoxLayout::allocate(Widget& widget, Rect const& allocation) {
    auto const kids = widget.children();
    bool const vertical = (orientation_ == Orientation::Vertical);

    // Count visible children.
    std::size_t visible_count = 0;
    for (auto const& child : kids) {
        if (child->is_visible()) {
            ++visible_count;
        }
    }
    if (visible_count == 0) {
        return;
    }

    float const total_spacing =
        spacing_ * static_cast<float>(visible_count - 1);
    float const available = vertical
                                ? (allocation.height - total_spacing)
                                : (allocation.width - total_spacing);

    float const child_size =
        homogeneous_
            ? (available / static_cast<float>(visible_count))
            : 0.0F;

    float offset = 0;
    for (auto const& child : kids) {
        if (!child->is_visible()) {
            continue;
        }

        float size = child_size;
        if (!homogeneous_) {
            auto const req = child->measure(Constraints::unbounded());
            size = vertical ? req.natural_height : req.natural_width;
        }

        Rect child_rect{};
        if (vertical) {
            child_rect = {allocation.x, allocation.y + offset,
                          allocation.width, size};
        } else {
            child_rect = {allocation.x + offset, allocation.y,
                          size, allocation.height};
        }

        child->allocate(child_rect);
        offset += size + spacing_;
    }
}

} // namespace nk
