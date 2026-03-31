#include <algorithm>
#include <limits>
#include <nk/layout/box_layout.h>
#include <nk/ui_core/widget.h>
#include <vector>

namespace nk {

namespace {

struct BoxChild {
    std::shared_ptr<Widget> widget;
    SizeRequest request;
    float minimum_main = 0.0F;
    float natural_main = 0.0F;
    float natural_cross = 0.0F;
    SizePolicy main_policy = SizePolicy::Preferred;
    SizePolicy cross_policy = SizePolicy::Preferred;
    uint8_t stretch = 0;
};

bool participates_in_layout(const Widget& widget) {
    return widget.is_visible() || widget.retain_size_when_hidden();
}

Constraints child_constraints(const Constraints& constraints, bool vertical) {
    Constraints result = Constraints::unbounded();
    if (vertical) {
        result.max_width = constraints.max_width;
    } else {
        result.max_height = constraints.max_height;
    }
    return result;
}

float clamp_cross_size(float available, float natural, SizePolicy policy) {
    if (policy == SizePolicy::Expanding) {
        return available;
    }
    return std::min(available, natural);
}

std::vector<BoxChild>
collect_box_children(const Widget& widget, const Constraints& constraints, bool vertical) {
    std::vector<BoxChild> children;
    const auto kids = widget.children();
    children.reserve(kids.size());

    const auto measure_constraints = child_constraints(constraints, vertical);
    for (const auto& child : kids) {
        if (child == nullptr || !participates_in_layout(*child)) {
            continue;
        }

        const auto request = child->measure(measure_constraints);
        children.push_back({
            .widget = child,
            .request = request,
            .minimum_main = vertical ? request.minimum_height : request.minimum_width,
            .natural_main = vertical ? request.natural_height : request.natural_width,
            .natural_cross = vertical ? request.natural_width : request.natural_height,
            .main_policy =
                vertical ? child->vertical_size_policy() : child->horizontal_size_policy(),
            .cross_policy =
                vertical ? child->horizontal_size_policy() : child->vertical_size_policy(),
            .stretch = vertical ? child->vertical_stretch() : child->horizontal_stretch(),
        });
    }

    return children;
}

std::vector<float> distribute_main_sizes(const std::vector<BoxChild>& children,
                                         float available_main) {
    std::vector<float> sizes(children.size(), 0.0F);
    if (children.empty()) {
        return sizes;
    }

    float total_minimum = 0.0F;
    float total_natural = 0.0F;
    float total_shrink_capacity = 0.0F;
    std::size_t expanding_count = 0;
    uint32_t stretch_sum = 0;

    for (std::size_t index = 0; index < children.size(); ++index) {
        sizes[index] = children[index].natural_main;
        total_minimum += children[index].minimum_main;
        total_natural += children[index].natural_main;
        total_shrink_capacity +=
            std::max(0.0F, children[index].natural_main - children[index].minimum_main);
        if (children[index].main_policy == SizePolicy::Expanding) {
            ++expanding_count;
            stretch_sum += std::max<uint32_t>(children[index].stretch, 1);
        }
    }

    if (available_main < total_natural) {
        const float deficit = total_natural - available_main;
        if (deficit >= total_shrink_capacity && total_minimum > 0.0F) {
            for (std::size_t index = 0; index < children.size(); ++index) {
                sizes[index] = children[index].minimum_main;
            }
            return sizes;
        }

        if (total_shrink_capacity > 0.0F) {
            for (std::size_t index = 0; index < children.size(); ++index) {
                const float shrink_capacity =
                    std::max(0.0F, children[index].natural_main - children[index].minimum_main);
                const float share = deficit * (shrink_capacity / total_shrink_capacity);
                sizes[index] =
                    std::max(children[index].minimum_main, children[index].natural_main - share);
            }
        }
        return sizes;
    }

    if (available_main <= total_natural || expanding_count == 0) {
        return sizes;
    }

    const float extra = available_main - total_natural;
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (children[index].main_policy != SizePolicy::Expanding) {
            continue;
        }
        const float weight = static_cast<float>(std::max<uint32_t>(children[index].stretch, 1));
        sizes[index] += extra * (weight / static_cast<float>(stretch_sum));
    }

    return sizes;
}

} // namespace

BoxLayout::BoxLayout(Orientation orientation) : orientation_(orientation) {}

BoxLayout::~BoxLayout() = default;

float BoxLayout::spacing() const {
    return spacing_;
}

void BoxLayout::set_spacing(float spacing) {
    spacing_ = spacing;
}

bool BoxLayout::homogeneous() const {
    return homogeneous_;
}

void BoxLayout::set_homogeneous(bool homogeneous) {
    homogeneous_ = homogeneous;
}

SizeRequest BoxLayout::measure(const Widget& widget, const Constraints& constraints) const {
    SizeRequest result{};
    const bool vertical = (orientation_ == Orientation::Vertical);
    const auto children = collect_box_children(widget, constraints, vertical);
    const float total_spacing =
        children.empty() ? 0.0F : spacing_ * static_cast<float>(children.size() - 1);

    for (const auto& child : children) {
        const auto& child_req = child.request;
        if (vertical) {
            result.minimum_width = std::max(result.minimum_width, child_req.minimum_width);
            result.natural_width = std::max(result.natural_width, child_req.natural_width);
            result.minimum_height += child_req.minimum_height;
            result.natural_height += child_req.natural_height;
        } else {
            result.minimum_height = std::max(result.minimum_height, child_req.minimum_height);
            result.natural_height = std::max(result.natural_height, child_req.natural_height);
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

void BoxLayout::allocate(Widget& widget, const Rect& allocation) {
    const bool vertical = (orientation_ == Orientation::Vertical);
    const auto children = collect_box_children(
        widget,
        vertical
            ? Constraints{0.0F, 0.0F, allocation.width, std::numeric_limits<float>::infinity()}
            : Constraints{0.0F, 0.0F, std::numeric_limits<float>::infinity(), allocation.height},
        vertical);
    if (children.empty()) {
        return;
    }

    const float total_spacing = spacing_ * static_cast<float>(children.size() - 1);
    const float available_main =
        std::max(0.0F, (vertical ? allocation.height : allocation.width) - total_spacing);

    std::vector<float> main_sizes(children.size(), 0.0F);
    if (homogeneous_) {
        const float homogeneous_size =
            children.empty() ? 0.0F : available_main / static_cast<float>(children.size());
        std::fill(main_sizes.begin(), main_sizes.end(), homogeneous_size);
    } else {
        main_sizes = distribute_main_sizes(children, available_main);
    }

    float offset = 0.0F;
    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto& child = children[index];
        const float main_size = std::max(0.0F, main_sizes[index]);
        const float cross_size = clamp_cross_size(vertical ? allocation.width : allocation.height,
                                                  child.natural_cross,
                                                  child.cross_policy);

        Rect child_rect{};
        if (vertical) {
            child_rect = {
                allocation.x, allocation.y + offset, std::max(0.0F, cross_size), main_size};
        } else {
            child_rect = {
                allocation.x + offset, allocation.y, main_size, std::max(0.0F, cross_size)};
        }

        child.widget->allocate(child_rect);
        offset += main_size + spacing_;
    }
}

} // namespace nk
