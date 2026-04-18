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
    float margin_before = 0.0F; // top for vertical, left for horizontal
    float margin_after = 0.0F;  // bottom for vertical, right for horizontal
    float margin_cross_start = 0.0F; // left for vertical, top for horizontal
    float margin_cross_end = 0.0F;   // right for vertical, bottom for horizontal
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

        const auto request = child->measure_for_diagnostics(measure_constraints);
        const auto m = child->margin();
        const float margin_main = vertical ? (m.top + m.bottom) : (m.left + m.right);
        const float margin_cross = vertical ? (m.left + m.right) : (m.top + m.bottom);
        children.push_back({
            .widget = child,
            .request = request,
            .minimum_main = (vertical ? request.minimum_height : request.minimum_width) + margin_main,
            .natural_main = (vertical ? request.natural_height : request.natural_width) + margin_main,
            .natural_cross = (vertical ? request.natural_width : request.natural_height) + margin_cross,
            .main_policy =
                vertical ? child->vertical_size_policy() : child->horizontal_size_policy(),
            .cross_policy =
                vertical ? child->horizontal_size_policy() : child->vertical_size_policy(),
            .stretch = vertical ? child->vertical_stretch() : child->horizontal_stretch(),
            .margin_before = vertical ? m.top : m.left,
            .margin_after = vertical ? m.bottom : m.right,
            .margin_cross_start = vertical ? m.left : m.top,
            .margin_cross_end = vertical ? m.right : m.bottom,
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

bool BoxLayout::has_height_for_width(Widget const& widget) const {
    for (const auto& child : widget.children()) {
        if (child && participates_in_layout(*child) && child->has_height_for_width()) {
            return true;
        }
    }
    return false;
}

float BoxLayout::height_for_width(Widget const& widget, float width) const {
    if (!has_height_for_width(widget)) {
        return measure(widget, Constraints::unbounded()).natural_height;
    }

    if (orientation_ == Orientation::Vertical) {
        float total_height = 0.0F;
        int visible_count = 0;
        for (const auto& child : widget.children()) {
            if (child && participates_in_layout(*child)) {
                const auto m = child->margin();
                const float available_width = std::max(0.0F, width - m.left - m.right);
                float h = 0.0F;
                if (child->has_height_for_width()) {
                    h = child->height_for_width(available_width);
                } else {
                    h = child->measure_for_diagnostics(Constraints::unbounded()).natural_height;
                }
                total_height += h + m.top + m.bottom;
                visible_count++;
            }
        }
        if (visible_count > 0) {
            total_height += spacing_ * static_cast<float>(visible_count - 1);
        }
        return total_height;
    }

    // Horizontal height-for-width
    float max_height = 0.0F;
    const auto children = collect_box_children(widget, Constraints{0, 0, width, std::numeric_limits<float>::infinity()}, false);
    float total_spacing = children.empty() ? 0.0F : spacing_ * static_cast<float>(children.size() - 1);
    float available_main = std::max(0.0F, width - total_spacing);

    std::vector<float> main_sizes(children.size(), 0.0F);
    if (homogeneous_) {
        float h_size = children.empty() ? 0.0F : available_main / static_cast<float>(children.size());
        std::fill(main_sizes.begin(), main_sizes.end(), h_size);
    } else {
        main_sizes = distribute_main_sizes(children, available_main);
    }

    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto& child = children[index];
        float child_width = std::max(0.0F, main_sizes[index] - child.margin_before - child.margin_after);
        float h = 0.0F;
        if (child.widget->has_height_for_width()) {
            h = child.widget->height_for_width(child_width);
        } else {
            h = child.request.natural_height;
        }
        max_height = std::max(max_height, h + child.margin_cross_start + child.margin_cross_end);
    }
    return max_height;
}

SizeRequest BoxLayout::measure(const Widget& widget, const Constraints& constraints) const {
    SizeRequest result{};
    const bool vertical = (orientation_ == Orientation::Vertical);
    const auto children = collect_box_children(widget, constraints, vertical);
    const float total_spacing =
        children.empty() ? 0.0F : spacing_ * static_cast<float>(children.size() - 1);

    for (const auto& child : children) {
        if (vertical) {
            result.minimum_width = std::max(result.minimum_width, child.natural_cross);
            result.natural_width = std::max(result.natural_width, child.natural_cross);
            result.minimum_height += child.minimum_main;
            result.natural_height += child.natural_main;
        } else {
            result.minimum_height = std::max(result.minimum_height, child.natural_cross);
            result.natural_height = std::max(result.natural_height, child.natural_cross);
            result.minimum_width += child.minimum_main;
            result.natural_width += child.natural_main;
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
        const float available_cross = vertical ? allocation.width : allocation.height;
        const float cross_margin = child.margin_cross_start + child.margin_cross_end;
        const float cross_size = clamp_cross_size(
            std::max(0.0F, available_cross - cross_margin), child.natural_cross - cross_margin,
            child.cross_policy);

        // Inset the child rect by margins so the widget receives the
        // content area, not the full margin-inclusive slot.
        Rect child_rect{};
        if (vertical) {
            child_rect = {allocation.x + child.margin_cross_start,
                          allocation.y + offset + child.margin_before,
                          std::max(0.0F, cross_size),
                          std::max(0.0F, main_size - child.margin_before - child.margin_after)};
        } else {
            child_rect = {allocation.x + offset + child.margin_before,
                          allocation.y + child.margin_cross_start,
                          std::max(0.0F, main_size - child.margin_before - child.margin_after),
                          std::max(0.0F, cross_size)};
        }

        child.widget->allocate(child_rect);
        offset += main_size + spacing_;
    }
}

} // namespace nk
