#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/separator.h>
#include <nk/widgets/toolbar.h>

namespace nk {

struct Toolbar::Impl {};

std::shared_ptr<Toolbar> Toolbar::create() {
    return std::shared_ptr<Toolbar>(new Toolbar());
}

Toolbar::Toolbar() : impl_(std::make_unique<Impl>()) {
    add_style_class("toolbar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Toolbar);
    accessible.set_name("toolbar");
}

Toolbar::~Toolbar() = default;

void Toolbar::add_item(std::shared_ptr<Widget> item) {
    append_child(std::move(item));
}

void Toolbar::add_separator() {
    append_child(Separator::create(Orientation::Vertical));
}

void Toolbar::clear_items() {
    clear_children();
}

SizeRequest Toolbar::measure(const Constraints& constraints) const {
    const float spacing = theme_number("spacing", 8.0F);
    const float padding_x = theme_number("padding-x", 8.0F);
    const float padding_y = theme_number("padding-y", 4.0F);

    float total_width = padding_x * 2.0F;
    float max_height = 0.0F;
    const auto& child_list = children();

    for (std::size_t i = 0; i < child_list.size(); ++i) {
        if (!child_list[i]->is_visible()) {
            continue;
        }
        const auto child_request = child_list[i]->measure(constraints);
        total_width += child_request.natural_width;
        max_height = std::max(max_height, child_request.natural_height);
        if (i + 1 < child_list.size()) {
            total_width += spacing;
        }
    }

    const float h = max_height + padding_y * 2.0F;
    const float min_h = theme_number("min-height", 40.0F);
    const float final_h = std::max(min_h, h);

    return {total_width, final_h, total_width, final_h};
}

void Toolbar::allocate(const Rect& alloc) {
    Widget::allocate(alloc);

    const float spacing = theme_number("spacing", 8.0F);
    const float padding_x = theme_number("padding-x", 8.0F);
    const float padding_y = theme_number("padding-y", 4.0F);

    const Constraints child_constraints{
        0.0F,
        0.0F,
        std::max(0.0F, alloc.width - padding_x * 2.0F),
        std::max(0.0F, alloc.height - padding_y * 2.0F),
    };

    float x_offset = alloc.x + padding_x;
    const float content_height = std::max(0.0F, alloc.height - padding_y * 2.0F);

    for (const auto& child : children()) {
        if (!child->is_visible()) {
            continue;
        }
        const auto child_request = child->measure(child_constraints);
        const float child_width = child_request.natural_width;
        const float child_height = content_height;
        child->allocate({x_offset, alloc.y + padding_y, child_width, child_height});
        x_offset += child_width + spacing;
    }
}

void Toolbar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();

    // Background
    ctx.add_color_rect(a, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));

    // Bottom border
    ctx.add_color_rect({a.x, a.y + a.height - 1.0F, a.width, 1.0F},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    // Children are rendered by the base class
    Widget::snapshot(ctx);
}

} // namespace nk
