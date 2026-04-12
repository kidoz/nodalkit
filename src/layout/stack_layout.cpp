#include <algorithm>
#include <nk/layout/stack_layout.h>
#include <nk/ui_core/widget.h>

namespace nk {

StackLayout::StackLayout() = default;

StackLayout::~StackLayout() = default;

SizeRequest StackLayout::measure(const Widget& widget, const Constraints& constraints) const {
    SizeRequest result{};
    for (const auto& child : widget.children()) {
        if (child == nullptr || (!child->is_visible() && !child->retain_size_when_hidden())) {
            continue;
        }

        const auto request = child->measure_for_diagnostics(constraints);
        result.minimum_width = std::max(result.minimum_width, request.minimum_width);
        result.minimum_height = std::max(result.minimum_height, request.minimum_height);
        result.natural_width = std::max(result.natural_width, request.natural_width);
        result.natural_height = std::max(result.natural_height, request.natural_height);
    }
    return result;
}

void StackLayout::allocate(Widget& widget, const Rect& allocation) {
    for (const auto& child : widget.children()) {
        if (child == nullptr || (!child->is_visible() && !child->retain_size_when_hidden())) {
            continue;
        }
        const auto m = child->margin();
        child->allocate({allocation.x + m.left,
                         allocation.y + m.top,
                         std::max(0.0F, allocation.width - m.left - m.right),
                         std::max(0.0F, allocation.height - m.top - m.bottom)});
    }
}

} // namespace nk
