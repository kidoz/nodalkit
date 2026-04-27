#include <algorithm>
#include <cmath>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/role.h>
#include <nk/model/selection_model.h>
#include <nk/model/tree_model.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/tree_view.h>
#include <sstream>
#include <string>

namespace nk {

namespace {

FontDescriptor tree_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.0F,
        .weight = FontWeight::Regular,
    };
}

Rect tree_inner_rect(const TreeView& view) {
    auto body = view.allocation();
    if (has_flag(view.state_flags(), StateFlags::Focused)) {
        body = {body.x + 2.0F, body.y + 2.0F, body.width - 4.0F, body.height - 4.0F};
    }
    return {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
}

float max_scroll_offset(const TreeModel* model, float row_height, float viewport_height) {
    if (!model || viewport_height <= 0.0F) {
        return 0.0F;
    }
    const float content_height = static_cast<float>(model->visible_row_count()) * row_height;
    return std::max(0.0F, content_height - viewport_height);
}

void clamp_scroll_offset(float& scroll_offset,
                         const TreeModel* model,
                         float row_height,
                         float viewport_height) {
    scroll_offset =
        std::clamp(scroll_offset, 0.0F, max_scroll_offset(model, row_height, viewport_height));
}

int visible_row_at_point(
    const TreeModel* model, float row_height, float scroll_offset, Rect viewport, Point point) {
    if (!model || row_height <= 0.0F || !viewport.contains(point)) {
        return -1;
    }

    const float local_y = point.y - viewport.y + scroll_offset;
    const int row = static_cast<int>(local_y / row_height);
    if (row < 0 || row >= static_cast<int>(model->visible_row_count())) {
        return -1;
    }
    return row;
}

TreeNodeId selection_current_node(const SelectionModel* selection) {
    if (!selection) {
        return InvalidTreeNodeId;
    }
    const auto current = selection->current_row();
    if (current != InvalidTreeNodeId) {
        return current;
    }
    const auto& selected = selection->selected_rows();
    if (!selected.empty()) {
        return *selected.begin();
    }
    return InvalidTreeNodeId;
}

std::string tree_accessible_description(const TreeModel* model, const SelectionModel* selection) {
    std::ostringstream stream;
    const auto visible_rows = model ? model->visible_row_count() : 0;
    stream << visible_rows << " visible rows";
    if (!model) {
        stream << ", no model";
        return stream.str();
    }
    if (visible_rows == 0) {
        stream << ", empty";
        return stream.str();
    }

    const auto current = selection_current_node(selection);
    if (model && model->contains(current)) {
        stream << ", current item " << model->display_text(current);
        const auto visible_row = model->visible_row_for_node(current);
        if (visible_row) {
            stream << ", current row " << (*visible_row + 1) << " of " << visible_rows;
        } else {
            stream << ", hidden";
        }
        stream << ", depth " << model->depth(current);
        if (model->has_children(current)) {
            stream << (model->is_expanded(current) ? ", expanded" : ", collapsed");
            stream << ", " << model->children(current).size() << " children";
        } else {
            stream << ", leaf";
        }
        if (selection && selection->is_selected(current)) {
            stream << ", selected";
        }
    } else {
        stream << ", no current item";
    }
    return stream.str();
}

std::string tree_accessible_value(const TreeModel* model, const SelectionModel* selection) {
    if (!model) {
        return {};
    }

    const auto current = selection_current_node(selection);
    if (!model->contains(current)) {
        return {};
    }

    std::ostringstream stream;
    stream << model->display_text(current);
    const auto visible_row = model->visible_row_for_node(current);
    if (visible_row) {
        stream << ", row " << (*visible_row + 1) << " of " << model->visible_row_count();
    } else {
        stream << ", hidden";
    }
    if (model->has_children(current)) {
        stream << (model->is_expanded(current) ? ", expanded" : ", collapsed");
    } else {
        stream << ", leaf";
    }
    return stream.str();
}

void draw_right_chevron(SnapshotContext& ctx, float x, float y, Color color) {
    ctx.add_color_rect({x + 2.0F, y + 2.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 4.0F, y + 4.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 6.0F, y + 6.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 4.0F, y + 8.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 2.0F, y + 10.0F, 2.0F, 2.0F}, color);
}

void draw_down_chevron(SnapshotContext& ctx, float x, float y, Color color) {
    ctx.add_color_rect({x + 1.0F, y + 3.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 3.0F, y + 5.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 5.0F, y + 7.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 7.0F, y + 5.0F, 2.0F, 2.0F}, color);
    ctx.add_color_rect({x + 9.0F, y + 3.0F, 2.0F, 2.0F}, color);
}

} // namespace

struct TreeView::Impl {
    std::shared_ptr<TreeModel> model;
    std::shared_ptr<SelectionModel> selection;
    float row_height = 26.0F;
    float indent_width = 18.0F;
    float scroll_offset = 0.0F;
    TreeNodeId hovered_node = InvalidTreeNodeId;
    Signal<TreeNodeId> node_activated;
    ScopedConnection model_reset;
    ScopedConnection selection_changed;
    ScopedConnection current_changed;
};

std::shared_ptr<TreeView> TreeView::create() {
    return std::shared_ptr<TreeView>(new TreeView());
}

TreeView::TreeView() : impl_(std::make_unique<Impl>()) {
    add_style_class("tree-view");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Tree);
    accessible.set_name("Tree view");
    accessible.add_action(AccessibleAction::Activate, [this] {
        const auto current = current_node();
        if (current == InvalidTreeNodeId) {
            return false;
        }
        impl_->node_activated.emit(current);
        return true;
    });
    accessible.add_action(AccessibleAction::Toggle, [this] {
        const auto current = current_node();
        if (!impl_->model || current == InvalidTreeNodeId || !impl_->model->has_children(current)) {
            return false;
        }
        impl_->model->toggle_expanded(current);
        return true;
    });
    set_focusable(true);
    sync_accessible_summary();
}

TreeView::~TreeView() = default;

void TreeView::set_model(std::shared_ptr<TreeModel> model) {
    if (impl_->model == model) {
        return;
    }
    impl_->model = std::move(model);
    reconnect_model();
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->model.get(), impl_->row_height, tree_inner_rect(*this).height);
    if (!impl_->model || !impl_->model->contains(impl_->hovered_node)) {
        impl_->hovered_node = InvalidTreeNodeId;
    }
    sync_accessible_summary();
    queue_layout();
    queue_redraw();
}

TreeModel* TreeView::model() const {
    return impl_->model.get();
}

void TreeView::set_selection_model(std::shared_ptr<SelectionModel> model) {
    impl_->selection = std::move(model);
    impl_->selection_changed.disconnect();
    impl_->current_changed.disconnect();
    if (impl_->selection) {
        impl_->selection_changed = ScopedConnection(
            impl_->selection->on_selection_changed().connect([this] { queue_selection_redraw(); }));
        impl_->current_changed = ScopedConnection(impl_->selection->on_current_changed().connect(
            [this](std::size_t) { queue_selection_redraw(); }));
    }
    sync_accessible_summary();
    queue_redraw();
}

SelectionModel* TreeView::selection_model() const {
    return impl_->selection.get();
}

void TreeView::set_row_height(float height) {
    impl_->row_height = std::max(1.0F, height);
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->model.get(), impl_->row_height, tree_inner_rect(*this).height);
    queue_layout();
    queue_redraw();
}

float TreeView::row_height() const {
    return impl_->row_height;
}

void TreeView::set_indent_width(float width) {
    impl_->indent_width = std::max(0.0F, width);
    queue_layout();
    queue_redraw();
}

float TreeView::indent_width() const {
    return impl_->indent_width;
}

Signal<TreeNodeId>& TreeView::on_node_activated() {
    return impl_->node_activated;
}

SizeRequest TreeView::measure(const Constraints& /*constraints*/) const {
    const float rows = impl_->model
                           ? static_cast<float>(std::min<std::size_t>(
                                 8, std::max<std::size_t>(4, impl_->model->visible_row_count())))
                           : 4.0F;
    return {180.0F, impl_->row_height * 3.0F, 280.0F, rows * impl_->row_height + 2.0F};
}

void TreeView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->model.get(), impl_->row_height, tree_inner_rect(*this).height);
}

bool TreeView::handle_mouse_event(const MouseEvent& event) {
    const auto inner = tree_inner_rect(*this);
    if (event.type == MouseEvent::Type::Press && event.button == 1) {
        const int row = visible_row_at_point(
            impl_->model.get(), impl_->row_height, impl_->scroll_offset, inner, {event.x, event.y});
        if (row < 0 || !impl_->model) {
            return inner.contains({event.x, event.y});
        }

        const auto node = impl_->model->visible_node_at(static_cast<std::size_t>(row));
        const float disclosure_x =
            inner.x + 8.0F + static_cast<float>(impl_->model->depth(node)) * impl_->indent_width;
        const Rect disclosure_rect = {disclosure_x,
                                      inner.y + static_cast<float>(row) * impl_->row_height -
                                          impl_->scroll_offset + 5.0F,
                                      16.0F,
                                      impl_->row_height - 10.0F};
        if (impl_->model->has_children(node) && disclosure_rect.contains({event.x, event.y})) {
            impl_->model->toggle_expanded(node);
            return true;
        }

        select_node(node);
        if (event.click_count >= 2) {
            if (impl_->model->has_children(node)) {
                impl_->model->toggle_expanded(node);
            }
            impl_->node_activated.emit(node);
        }
        return true;
    }

    if (event.type == MouseEvent::Type::Scroll) {
        if (!inner.contains({event.x, event.y}) || !impl_->model) {
            return false;
        }
        impl_->scroll_offset =
            std::clamp(impl_->scroll_offset - (event.scroll_dy * impl_->row_height),
                       0.0F,
                       max_scroll_offset(impl_->model.get(), impl_->row_height, inner.height));
        queue_redraw();
        return true;
    }

    if (event.type == MouseEvent::Type::Move) {
        const int row = visible_row_at_point(
            impl_->model.get(), impl_->row_height, impl_->scroll_offset, inner, {event.x, event.y});
        const auto hovered = row >= 0 && impl_->model
                                 ? impl_->model->visible_node_at(static_cast<std::size_t>(row))
                                 : InvalidTreeNodeId;
        if (hovered != impl_->hovered_node) {
            impl_->hovered_node = hovered;
            queue_redraw();
        }
        return inner.contains({event.x, event.y});
    }

    if (event.type == MouseEvent::Type::Leave) {
        if (impl_->hovered_node != InvalidTreeNodeId) {
            impl_->hovered_node = InvalidTreeNodeId;
            queue_redraw();
        }
        return false;
    }

    return false;
}

bool TreeView::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || !impl_->model ||
        impl_->model->visible_row_count() == 0) {
        return false;
    }

    const auto current = current_node();
    std::size_t current_row = 0;
    if (current != InvalidTreeNodeId) {
        current_row = impl_->model->visible_row_for_node(current).value_or(0);
    }

    auto select_visible_row = [this](std::size_t row) {
        select_node(impl_->model->visible_node_at(row));
    };

    const auto row_count = impl_->model->visible_row_count();
    switch (event.key) {
    case KeyCode::Up:
        select_visible_row(current_row > 0 ? current_row - 1 : 0);
        return true;
    case KeyCode::Down:
        select_visible_row(std::min(current_row + 1, row_count - 1));
        return true;
    case KeyCode::Home:
        select_visible_row(0);
        return true;
    case KeyCode::End:
        select_visible_row(row_count - 1);
        return true;
    case KeyCode::Left:
        if (current != InvalidTreeNodeId && impl_->model->has_children(current) &&
            impl_->model->is_expanded(current)) {
            impl_->model->set_expanded(current, false);
            return true;
        }
        if (current != InvalidTreeNodeId && impl_->model->parent(current) != InvalidTreeNodeId) {
            select_node(impl_->model->parent(current));
            return true;
        }
        return false;
    case KeyCode::Right:
        if (current != InvalidTreeNodeId && impl_->model->has_children(current)) {
            if (!impl_->model->is_expanded(current)) {
                impl_->model->set_expanded(current, true);
            } else {
                select_node(impl_->model->children(current).front());
            }
            return true;
        }
        return false;
    case KeyCode::Return:
    case KeyCode::Space:
        if (current != InvalidTreeNodeId) {
            impl_->node_activated.emit(current);
            return true;
        }
        return false;
    default:
        return false;
    }
}

void TreeView::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 8.0F);
    auto body = a;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        const auto focus_ring = theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
        ctx.add_rounded_rect(
            a, Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.08F}, corner_radius + 1.0F);
        body = {a.x + 1.5F, a.y + 1.5F, a.width - 3.0F, a.height - 3.0F};
    }

    const auto background = theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto border = theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F});
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto muted_text = theme_color("muted-text-color", Color{0.42F, 0.45F, 0.50F, 1.0F});
    const auto selected_bg = theme_color("selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});
    const auto selected_text = theme_color("selected-text-color", text_color);
    const auto focus_ring = theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
    const auto hover_bg = theme_color("hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto separator = theme_color("row-separator-color", Color{0.9F, 0.91F, 0.94F, 1.0F});

    ctx.add_rounded_rect(body, background, corner_radius);
    ctx.add_border(body, border, 1.0F, corner_radius);

    const auto inner = tree_inner_rect(*this);
    const auto font = tree_font();
    ctx.push_rounded_clip(inner, corner_radius);
    if (!impl_->model || impl_->model->visible_row_count() == 0) {
        const std::string empty_text = "No items";
        const auto measured = measure_text(empty_text, font);
        ctx.add_text(
            {inner.x + 12.0F, inner.y + std::max(0.0F, (inner.height - measured.height) * 0.5F)},
            empty_text,
            muted_text,
            font);
    } else {
        const auto total_rows = impl_->model->visible_row_count();
        const auto first_row = static_cast<std::size_t>(impl_->scroll_offset / impl_->row_height);
        float y = inner.y - std::fmod(impl_->scroll_offset, impl_->row_height);
        for (std::size_t row = first_row; row < total_rows && y < inner.bottom(); ++row) {
            if (y + impl_->row_height <= inner.y) {
                y += impl_->row_height;
                continue;
            }
            const auto node = impl_->model->visible_node_at(row);
            const bool selected = impl_->selection && impl_->selection->is_selected(node);
            const bool current = impl_->selection && impl_->selection->current_row() == node;
            const bool hovered = impl_->hovered_node == node;
            if (hovered && !selected) {
                ctx.add_rounded_rect(
                    {inner.x + 3.0F, y + 2.0F, inner.width - 6.0F, impl_->row_height - 4.0F},
                    hover_bg,
                    5.0F);
            }
            if (selected) {
                ctx.add_rounded_rect(
                    {inner.x + 3.0F, y + 2.0F, inner.width - 6.0F, impl_->row_height - 4.0F},
                    Color{selected_bg.r, selected_bg.g, selected_bg.b, 0.82F},
                    5.0F);
            }
            if (current) {
                ctx.add_border(
                    {inner.x + 3.0F, y + 2.0F, inner.width - 6.0F, impl_->row_height - 4.0F},
                    Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.9F},
                    1.5F,
                    5.0F);
            }

            const float indent =
                static_cast<float>(impl_->model->depth(node)) * impl_->indent_width;
            const float disclosure_x = inner.x + 8.0F + indent;
            const float text_x = disclosure_x + 18.0F;
            if (impl_->model->has_children(node)) {
                const Rect disclosure_bg = {disclosure_x - 2.0F, y + 5.0F, 16.0F, 16.0F};
                if (hovered) {
                    ctx.add_rounded_rect(disclosure_bg,
                                         Color{muted_text.r, muted_text.g, muted_text.b, 0.10F},
                                         4.0F);
                }
                if (impl_->model->is_expanded(node)) {
                    draw_down_chevron(ctx, disclosure_x, y + 7.0F, muted_text);
                } else {
                    draw_right_chevron(ctx, disclosure_x + 1.0F, y + 6.0F, muted_text);
                }
            }
            ctx.add_text({text_x, y + 6.0F},
                         std::string(impl_->model->display_text(node)),
                         selected ? selected_text : text_color,
                         font);
            ctx.add_color_rect({inner.x, y + impl_->row_height - 1.0F, inner.width, 1.0F},
                               separator);
            y += impl_->row_height;
        }
    }
    ctx.pop_container();
}

void TreeView::reconnect_model() {
    impl_->model_reset.disconnect();
    if (!impl_->model) {
        return;
    }
    impl_->model_reset = ScopedConnection(impl_->model->on_model_reset().connect([this] {
        clamp_scroll_offset(impl_->scroll_offset,
                            impl_->model.get(),
                            impl_->row_height,
                            tree_inner_rect(*this).height);
        if (!impl_->model->contains(impl_->hovered_node) ||
            !impl_->model->visible_row_for_node(impl_->hovered_node).has_value()) {
            impl_->hovered_node = InvalidTreeNodeId;
        }
        sync_accessible_summary();
        queue_layout();
        queue_redraw();
    }));
}

void TreeView::queue_selection_redraw() {
    sync_accessible_summary();
    queue_redraw();
}

void TreeView::sync_accessible_summary() {
    auto& accessible = ensure_accessible();
    accessible.set_description(
        tree_accessible_description(impl_->model.get(), impl_->selection.get()));
    accessible.set_value(tree_accessible_value(impl_->model.get(), impl_->selection.get()));
}

void TreeView::select_node(TreeNodeId node) {
    if (!impl_->model || !impl_->model->contains(node)) {
        return;
    }
    if (impl_->selection) {
        impl_->selection->set_current_row(node);
        impl_->selection->select(node);
    }
    ensure_node_visible(node);
    sync_accessible_summary();
    queue_redraw();
}

void TreeView::ensure_node_visible(TreeNodeId node) {
    if (!impl_->model) {
        return;
    }
    const auto row = impl_->model->visible_row_for_node(node);
    if (!row) {
        return;
    }
    const auto inner = tree_inner_rect(*this);
    const float row_top = static_cast<float>(*row) * impl_->row_height;
    const float row_bottom = row_top + impl_->row_height;
    if (row_top < impl_->scroll_offset) {
        impl_->scroll_offset = row_top;
    } else if (row_bottom > impl_->scroll_offset + inner.height) {
        impl_->scroll_offset = row_bottom - inner.height;
    }
    clamp_scroll_offset(impl_->scroll_offset, impl_->model.get(), impl_->row_height, inner.height);
}

TreeNodeId TreeView::current_node() const {
    const auto current = selection_current_node(impl_->selection.get());
    if (impl_->model && impl_->model->contains(current) &&
        impl_->model->visible_row_for_node(current).has_value()) {
        return current;
    }
    if (impl_->model && impl_->model->visible_row_count() > 0) {
        return impl_->model->visible_node_at(0);
    }
    return InvalidTreeNodeId;
}

} // namespace nk
