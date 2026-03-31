#include <algorithm>
#include <limits>
#include <nk/layout/grid_layout.h>
#include <nk/ui_core/widget.h>
#include <vector>

namespace nk {

namespace {

struct GridChild {
    std::shared_ptr<Widget> widget;
    GridLayout::Placement placement;
    SizeRequest request;
};

bool participates_in_layout(const Widget& widget) {
    return widget.is_visible() || widget.retain_size_when_hidden();
}

std::vector<float> distribute_track_sizes(const std::vector<float>& minimums,
                                          const std::vector<float>& naturals,
                                          float available) {
    std::vector<float> sizes = naturals;
    if (sizes.empty()) {
        return sizes;
    }

    float total_minimum = 0.0F;
    float total_natural = 0.0F;
    float total_shrink_capacity = 0.0F;
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        total_minimum += minimums[index];
        total_natural += naturals[index];
        total_shrink_capacity += std::max(0.0F, naturals[index] - minimums[index]);
    }

    if (available < total_natural) {
        const float deficit = total_natural - available;
        if (deficit >= total_shrink_capacity && total_minimum > 0.0F) {
            const float scale = std::max(0.0F, available) / total_minimum;
            for (std::size_t index = 0; index < sizes.size(); ++index) {
                sizes[index] = minimums[index] * scale;
            }
            return sizes;
        }

        if (total_shrink_capacity > 0.0F) {
            for (std::size_t index = 0; index < sizes.size(); ++index) {
                const float shrink_capacity = std::max(0.0F, naturals[index] - minimums[index]);
                const float share = deficit * (shrink_capacity / total_shrink_capacity);
                sizes[index] = std::max(minimums[index], naturals[index] - share);
            }
        }
        return sizes;
    }

    if (available > total_natural) {
        const float extra = (available - total_natural) / static_cast<float>(sizes.size());
        for (auto& size : sizes) {
            size += extra;
        }
    }

    return sizes;
}

template <typename GetMinimum, typename GetNatural>
void apply_span_requirement(std::vector<float>& minimums,
                            std::vector<float>& naturals,
                            int start,
                            int span,
                            const GridChild& child,
                            GetMinimum get_minimum,
                            GetNatural get_natural) {
    if (span <= 0) {
        return;
    }

    const float minimum_per_track = get_minimum(child.request) / static_cast<float>(span);
    const float natural_per_track = get_natural(child.request) / static_cast<float>(span);
    for (int offset = 0; offset < span; ++offset) {
        const auto index = static_cast<std::size_t>(start + offset);
        minimums[index] = std::max(minimums[index], minimum_per_track);
        naturals[index] = std::max(naturals[index], natural_per_track);
    }
}

} // namespace

GridLayout::GridLayout() = default;

GridLayout::~GridLayout() = default;

float GridLayout::row_spacing() const {
    return row_spacing_;
}

void GridLayout::set_row_spacing(float spacing) {
    row_spacing_ = std::max(0.0F, spacing);
}

float GridLayout::column_spacing() const {
    return column_spacing_;
}

void GridLayout::set_column_spacing(float spacing) {
    column_spacing_ = std::max(0.0F, spacing);
}

void GridLayout::attach(Widget& child, int row, int column, int row_span, int column_span) {
    placements_[&child] = {
        .row = std::max(0, row),
        .column = std::max(0, column),
        .row_span = std::max(1, row_span),
        .column_span = std::max(1, column_span),
    };
}

void GridLayout::remove(Widget& child) {
    placements_.erase(&child);
}

void GridLayout::clear() {
    placements_.clear();
}

GridLayout::Placement GridLayout::placement_for(const Widget& child, int fallback_index) const {
    if (auto it = placements_.find(&child); it != placements_.end()) {
        return it->second;
    }

    return {
        .row = fallback_index,
        .column = 0,
        .row_span = 1,
        .column_span = 1,
    };
}

SizeRequest GridLayout::measure(const Widget& widget, const Constraints& constraints) const {
    std::vector<GridChild> children;
    int row_count = 0;
    int column_count = 0;
    int fallback_index = 0;
    for (const auto& child : widget.children()) {
        if (child == nullptr || !participates_in_layout(*child)) {
            continue;
        }

        const auto placement = placement_for(*child, fallback_index++);
        const auto request = child->measure(constraints);
        row_count = std::max(row_count, placement.row + placement.row_span);
        column_count = std::max(column_count, placement.column + placement.column_span);
        children.push_back({child, placement, request});
    }

    if (children.empty()) {
        return {};
    }

    std::vector<float> column_minimums(static_cast<std::size_t>(column_count), 0.0F);
    std::vector<float> column_naturals(static_cast<std::size_t>(column_count), 0.0F);
    std::vector<float> row_minimums(static_cast<std::size_t>(row_count), 0.0F);
    std::vector<float> row_naturals(static_cast<std::size_t>(row_count), 0.0F);

    for (const auto& child : children) {
        apply_span_requirement(
            column_minimums,
            column_naturals,
            child.placement.column,
            child.placement.column_span,
            child,
            [](const SizeRequest& request) { return request.minimum_width; },
            [](const SizeRequest& request) { return request.natural_width; });
        apply_span_requirement(
            row_minimums,
            row_naturals,
            child.placement.row,
            child.placement.row_span,
            child,
            [](const SizeRequest& request) { return request.minimum_height; },
            [](const SizeRequest& request) { return request.natural_height; });
    }

    const auto sum = [](const std::vector<float>& values) {
        float total = 0.0F;
        for (float value : values) {
            total += value;
        }
        return total;
    };

    return {
        sum(column_minimums) + column_spacing_ * static_cast<float>(std::max(0, column_count - 1)),
        sum(row_minimums) + row_spacing_ * static_cast<float>(std::max(0, row_count - 1)),
        sum(column_naturals) + column_spacing_ * static_cast<float>(std::max(0, column_count - 1)),
        sum(row_naturals) + row_spacing_ * static_cast<float>(std::max(0, row_count - 1)),
    };
}

void GridLayout::allocate(Widget& widget, const Rect& allocation) {
    std::vector<GridChild> children;
    int row_count = 0;
    int column_count = 0;
    int fallback_index = 0;
    for (const auto& child : widget.children()) {
        if (child == nullptr || !participates_in_layout(*child)) {
            continue;
        }

        const auto placement = placement_for(*child, fallback_index++);
        const auto request = child->measure(Constraints{
            0.0F, 0.0F, std::max(0.0F, allocation.width), std::max(0.0F, allocation.height)});
        row_count = std::max(row_count, placement.row + placement.row_span);
        column_count = std::max(column_count, placement.column + placement.column_span);
        children.push_back({child, placement, request});
    }

    if (children.empty()) {
        return;
    }

    std::vector<float> column_minimums(static_cast<std::size_t>(column_count), 0.0F);
    std::vector<float> column_naturals(static_cast<std::size_t>(column_count), 0.0F);
    std::vector<float> row_minimums(static_cast<std::size_t>(row_count), 0.0F);
    std::vector<float> row_naturals(static_cast<std::size_t>(row_count), 0.0F);

    for (const auto& child : children) {
        apply_span_requirement(
            column_minimums,
            column_naturals,
            child.placement.column,
            child.placement.column_span,
            child,
            [](const SizeRequest& request) { return request.minimum_width; },
            [](const SizeRequest& request) { return request.natural_width; });
        apply_span_requirement(
            row_minimums,
            row_naturals,
            child.placement.row,
            child.placement.row_span,
            child,
            [](const SizeRequest& request) { return request.minimum_height; },
            [](const SizeRequest& request) { return request.natural_height; });
    }

    const float available_width = std::max(
        0.0F,
        allocation.width - column_spacing_ * static_cast<float>(std::max(0, column_count - 1)));
    const float available_height = std::max(
        0.0F, allocation.height - row_spacing_ * static_cast<float>(std::max(0, row_count - 1)));
    const auto column_sizes =
        distribute_track_sizes(column_minimums, column_naturals, available_width);
    const auto row_sizes = distribute_track_sizes(row_minimums, row_naturals, available_height);

    std::vector<float> column_offsets(static_cast<std::size_t>(column_count), allocation.x);
    std::vector<float> row_offsets(static_cast<std::size_t>(row_count), allocation.y);
    for (int index = 1; index < column_count; ++index) {
        column_offsets[static_cast<std::size_t>(index)] =
            column_offsets[static_cast<std::size_t>(index - 1)] +
            column_sizes[static_cast<std::size_t>(index - 1)] + column_spacing_;
    }
    for (int index = 1; index < row_count; ++index) {
        row_offsets[static_cast<std::size_t>(index)] =
            row_offsets[static_cast<std::size_t>(index - 1)] +
            row_sizes[static_cast<std::size_t>(index - 1)] + row_spacing_;
    }

    for (const auto& child : children) {
        float width = 0.0F;
        for (int offset = 0; offset < child.placement.column_span; ++offset) {
            width += column_sizes[static_cast<std::size_t>(child.placement.column + offset)];
        }
        width += column_spacing_ * static_cast<float>(std::max(0, child.placement.column_span - 1));

        float height = 0.0F;
        for (int offset = 0; offset < child.placement.row_span; ++offset) {
            height += row_sizes[static_cast<std::size_t>(child.placement.row + offset)];
        }
        height += row_spacing_ * static_cast<float>(std::max(0, child.placement.row_span - 1));

        child.widget->allocate({column_offsets[static_cast<std::size_t>(child.placement.column)],
                                row_offsets[static_cast<std::size_t>(child.placement.row)],
                                width,
                                height});
    }
}

} // namespace nk
