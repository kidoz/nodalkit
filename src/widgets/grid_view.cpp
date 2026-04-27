#include <algorithm>
#include <cmath>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/role.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/grid_view.h>
#include <sstream>
#include <string>

namespace nk {

namespace {

std::size_t invalid_row() {
    return static_cast<std::size_t>(-1);
}

FontDescriptor grid_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.0F,
        .weight = FontWeight::Regular,
    };
}

Rect grid_inner_rect(const GridView& view) {
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

std::size_t column_count_for_width(float width, float cell_width, float gap) {
    if (width <= 0.0F || cell_width <= 0.0F) {
        return 1;
    }
    return std::max<std::size_t>(
        1, static_cast<std::size_t>((width + std::max(0.0F, gap)) / (cell_width + gap)));
}

std::size_t row_count_for_items(std::size_t items, std::size_t columns) {
    if (items == 0) {
        return 0;
    }
    return ((items - 1) / std::max<std::size_t>(1, columns)) + 1;
}

float content_height_for_items(std::size_t items,
                               std::size_t columns,
                               float cell_height,
                               float gap) {
    const auto rows = row_count_for_items(items, columns);
    if (rows == 0) {
        return 0.0F;
    }
    return (static_cast<float>(rows) * cell_height) +
           (static_cast<float>(rows - 1) * std::max(0.0F, gap));
}

Rect grid_content_rect(const GridView& view,
                       const AbstractListModel* model,
                       float cell_width,
                       float cell_height,
                       float gap) {
    const auto inner = grid_inner_rect(view);
    if (!model) {
        return inner;
    }

    const auto columns = column_count_for_width(inner.width, cell_width, gap);
    const float total_height =
        content_height_for_items(model->row_count(), columns, cell_height, gap);
    const bool show_scrollbar = total_height > inner.height;
    const float scrollbar_width = show_scrollbar ? 11.0F : 0.0F;
    return {
        inner.x,
        inner.y,
        std::max(0.0F, inner.width - scrollbar_width - (show_scrollbar ? 12.0F : 0.0F)),
        inner.height,
    };
}

float max_scroll_offset(const AbstractListModel* model,
                        float cell_width,
                        float cell_height,
                        float gap,
                        float viewport_width,
                        float viewport_height) {
    if (!model || viewport_width <= 0.0F || viewport_height <= 0.0F) {
        return 0.0F;
    }
    const auto columns = column_count_for_width(viewport_width, cell_width, gap);
    const float content_height =
        content_height_for_items(model->row_count(), columns, cell_height, gap);
    return std::max(0.0F, content_height - viewport_height);
}

void clamp_scroll_offset(float& scroll_offset,
                         const AbstractListModel* model,
                         float cell_width,
                         float cell_height,
                         float gap,
                         float viewport_width,
                         float viewport_height) {
    scroll_offset = std::clamp(
        scroll_offset,
        0.0F,
        max_scroll_offset(model, cell_width, cell_height, gap, viewport_width, viewport_height));
}

std::size_t current_model_row(const SelectionModel* selection) {
    if (!selection) {
        return invalid_row();
    }
    const auto current = selection->current_row();
    if (current != invalid_row()) {
        return current;
    }
    const auto& selected = selection->selected_rows();
    if (!selected.empty()) {
        return *selected.begin();
    }
    return invalid_row();
}

Rect cell_rect_for_row(std::size_t row,
                       std::size_t columns,
                       Rect content,
                       float cell_width,
                       float cell_height,
                       float gap,
                       float scroll_offset) {
    const auto col = row % columns;
    const auto grid_row = row / columns;
    return {
        content.x + static_cast<float>(col) * (cell_width + gap),
        content.y + static_cast<float>(grid_row) * (cell_height + gap) - scroll_offset,
        cell_width,
        cell_height,
    };
}

int item_at_point(const AbstractListModel* model,
                  float cell_width,
                  float cell_height,
                  float gap,
                  float scroll_offset,
                  Rect content,
                  Point point) {
    if (!model || !content.contains(point) || cell_width <= 0.0F || cell_height <= 0.0F) {
        return -1;
    }

    const auto columns = column_count_for_width(content.width, cell_width, gap);
    const float local_x = point.x - content.x;
    const float local_y = point.y - content.y + scroll_offset;
    const float stride_x = cell_width + gap;
    const float stride_y = cell_height + gap;
    const auto col = static_cast<std::size_t>(local_x / stride_x);
    const auto grid_row = static_cast<std::size_t>(local_y / stride_y);
    if (col >= columns) {
        return -1;
    }
    const float cell_local_x = local_x - (static_cast<float>(col) * stride_x);
    const float cell_local_y = local_y - (static_cast<float>(grid_row) * stride_y);
    if (cell_local_x >= cell_width || cell_local_y >= cell_height) {
        return -1;
    }

    const auto row = (grid_row * columns) + col;
    if (row >= model->row_count()) {
        return -1;
    }
    return static_cast<int>(row);
}

std::string grid_accessible_description(const AbstractListModel* model,
                                        const SelectionModel* selection,
                                        std::size_t columns) {
    std::ostringstream stream;
    const auto items = model ? model->row_count() : 0;
    stream << items << " items, " << columns << " columns";
    if (!model) {
        stream << ", no model";
        return stream.str();
    }
    if (items == 0) {
        stream << ", empty";
        return stream.str();
    }

    const auto current = current_model_row(selection);
    if (current != invalid_row() && current < items) {
        stream << ", current item " << (current + 1) << " of " << items;
        stream << ", " << model->display_text(current);
    } else {
        stream << ", no current item";
    }
    if (selection && !selection->selected_rows().empty()) {
        stream << ", " << selection->selected_rows().size() << " selected";
    }
    return stream.str();
}

std::string grid_accessible_value(const AbstractListModel* model, const SelectionModel* selection) {
    if (!model) {
        return {};
    }
    const auto current = current_model_row(selection);
    if (current == invalid_row() || current >= model->row_count()) {
        return {};
    }
    std::ostringstream stream;
    stream << model->display_text(current) << ", item " << (current + 1) << " of "
           << model->row_count();
    return stream.str();
}

} // namespace

struct GridView::Impl {
    std::shared_ptr<AbstractListModel> model;
    std::shared_ptr<SelectionModel> selection;
    float cell_width = 92.0F;
    float cell_height = 72.0F;
    float gap = 8.0F;
    float scroll_offset = 0.0F;
    std::size_t hovered_row = invalid_row();
    Signal<std::size_t> item_activated;
    ScopedConnection rows_inserted;
    ScopedConnection rows_removed;
    ScopedConnection data_changed;
    ScopedConnection model_reset;
    ScopedConnection selection_changed;
    ScopedConnection current_changed;
};

std::shared_ptr<GridView> GridView::create() {
    return std::shared_ptr<GridView>(new GridView());
}

GridView::GridView() : impl_(std::make_unique<Impl>()) {
    add_style_class("grid-view");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Grid);
    accessible.set_name("Grid view");
    accessible.add_action(AccessibleAction::Activate, [this] {
        const auto current = current_item();
        if (current == invalid_row()) {
            return false;
        }
        impl_->item_activated.emit(current);
        return true;
    });
    set_focusable(true);
    sync_accessible_summary();
}

GridView::~GridView() = default;

void GridView::set_model(std::shared_ptr<AbstractListModel> model) {
    if (impl_->model == model) {
        return;
    }
    impl_->model = std::move(model);
    reconnect_model();
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->model.get(),
                        impl_->cell_width,
                        impl_->cell_height,
                        impl_->gap,
                        content.width,
                        content.height);
    if (!impl_->model || impl_->hovered_row >= impl_->model->row_count()) {
        impl_->hovered_row = invalid_row();
    }
    sync_accessible_summary();
    queue_layout();
    queue_redraw();
}

AbstractListModel* GridView::model() const {
    return impl_->model.get();
}

void GridView::set_selection_model(std::shared_ptr<SelectionModel> model) {
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

SelectionModel* GridView::selection_model() const {
    return impl_->selection.get();
}

void GridView::set_cell_width(float width) {
    impl_->cell_width = std::max(1.0F, width);
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->model.get(),
                        impl_->cell_width,
                        impl_->cell_height,
                        impl_->gap,
                        content.width,
                        content.height);
    sync_accessible_summary();
    queue_layout();
    queue_redraw();
}

float GridView::cell_width() const {
    return impl_->cell_width;
}

void GridView::set_cell_height(float height) {
    impl_->cell_height = std::max(1.0F, height);
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->model.get(),
                        impl_->cell_width,
                        impl_->cell_height,
                        impl_->gap,
                        content.width,
                        content.height);
    queue_layout();
    queue_redraw();
}

float GridView::cell_height() const {
    return impl_->cell_height;
}

void GridView::set_gap(float gap) {
    impl_->gap = std::max(0.0F, gap);
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->model.get(),
                        impl_->cell_width,
                        impl_->cell_height,
                        impl_->gap,
                        content.width,
                        content.height);
    sync_accessible_summary();
    queue_layout();
    queue_redraw();
}

float GridView::gap() const {
    return impl_->gap;
}

Signal<std::size_t>& GridView::on_item_activated() {
    return impl_->item_activated;
}

SizeRequest GridView::measure(const Constraints& /*constraints*/) const {
    const float width = (impl_->cell_width * 3.0F) + (impl_->gap * 2.0F) + 2.0F;
    const float height = (impl_->cell_height * 2.0F) + impl_->gap + 2.0F;
    return {
        impl_->cell_width + 2.0F,
        impl_->cell_height + 2.0F,
        std::max(220.0F, width),
        std::max(160.0F, height),
    };
}

void GridView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->model.get(),
                        impl_->cell_width,
                        impl_->cell_height,
                        impl_->gap,
                        content.width,
                        content.height);
    sync_accessible_summary();
}

bool GridView::handle_mouse_event(const MouseEvent& event) {
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);

    if (event.type == MouseEvent::Type::Press && event.button == 1) {
        const int row = item_at_point(impl_->model.get(),
                                      impl_->cell_width,
                                      impl_->cell_height,
                                      impl_->gap,
                                      impl_->scroll_offset,
                                      content,
                                      {event.x, event.y});
        if (row < 0 || !impl_->model) {
            return content.contains({event.x, event.y});
        }
        const auto model_row = static_cast<std::size_t>(row);
        select_item(model_row);
        if (event.click_count >= 2) {
            impl_->item_activated.emit(model_row);
        }
        return true;
    }

    if (event.type == MouseEvent::Type::Scroll) {
        if (!content.contains({event.x, event.y}) || !impl_->model) {
            return false;
        }
        impl_->scroll_offset =
            std::clamp(impl_->scroll_offset - (event.scroll_dy * impl_->cell_height),
                       0.0F,
                       max_scroll_offset(impl_->model.get(),
                                         impl_->cell_width,
                                         impl_->cell_height,
                                         impl_->gap,
                                         content.width,
                                         content.height));
        queue_redraw();
        return true;
    }

    if (event.type == MouseEvent::Type::Move) {
        const int row = item_at_point(impl_->model.get(),
                                      impl_->cell_width,
                                      impl_->cell_height,
                                      impl_->gap,
                                      impl_->scroll_offset,
                                      content,
                                      {event.x, event.y});
        const auto hovered = row >= 0 ? static_cast<std::size_t>(row) : invalid_row();
        if (hovered != impl_->hovered_row) {
            impl_->hovered_row = hovered;
            queue_redraw();
        }
        return content.contains({event.x, event.y});
    }

    if (event.type == MouseEvent::Type::Leave) {
        if (impl_->hovered_row != invalid_row()) {
            impl_->hovered_row = invalid_row();
            queue_redraw();
        }
        return false;
    }

    return false;
}

bool GridView::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || !impl_->model || impl_->model->row_count() == 0) {
        return false;
    }

    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    const auto columns = column_count_for_width(content.width, impl_->cell_width, impl_->gap);
    const auto row_count = impl_->model->row_count();
    std::size_t current = current_item();
    if (current == invalid_row()) {
        current = 0;
    }

    auto select_clamped = [this, row_count](std::size_t row) {
        select_item(std::min(row, row_count - 1));
    };

    switch (event.key) {
    case KeyCode::Left:
        select_clamped(current > 0 ? current - 1 : 0);
        return true;
    case KeyCode::Right:
        select_clamped(current + 1);
        return true;
    case KeyCode::Up:
        select_clamped(current >= columns ? current - columns : 0);
        return true;
    case KeyCode::Down:
        select_clamped(current + columns);
        return true;
    case KeyCode::Home:
        select_item(0);
        return true;
    case KeyCode::End:
        select_item(row_count - 1);
        return true;
    case KeyCode::PageUp: {
        const auto page_rows =
            std::max<std::size_t>(1, static_cast<std::size_t>(content.height / impl_->cell_height));
        const auto delta = page_rows * columns;
        select_item(current > delta ? current - delta : 0);
        return true;
    }
    case KeyCode::PageDown: {
        const auto page_rows =
            std::max<std::size_t>(1, static_cast<std::size_t>(content.height / impl_->cell_height));
        select_clamped(current + (page_rows * columns));
        return true;
    }
    case KeyCode::Return:
    case KeyCode::Space:
        impl_->item_activated.emit(current);
        return true;
    default:
        return false;
    }
}

void GridView::snapshot(SnapshotContext& ctx) const {
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
    const auto cell_border = theme_color("cell-border-color", Color{0.88F, 0.90F, 0.93F, 1.0F});
    const auto scrollbar_track =
        theme_color("scrollbar-track-color", Color{0.88F, 0.90F, 0.93F, 1.0F});
    const auto scrollbar_thumb =
        theme_color("scrollbar-thumb-color", Color{0.67F, 0.71F, 0.76F, 1.0F});

    ctx.add_rounded_rect(body, background, corner_radius);
    ctx.add_border(body, border, 1.0F, corner_radius);

    const auto inner = grid_inner_rect(*this);
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    const auto font = grid_font();
    ctx.push_rounded_clip(inner, corner_radius);

    if (!impl_->model || impl_->model->row_count() == 0) {
        const std::string empty_text = "No items";
        const auto measured = measure_text(empty_text, font);
        ctx.add_text(
            {inner.x + 12.0F, inner.y + std::max(0.0F, (inner.height - measured.height) * 0.5F)},
            empty_text,
            muted_text,
            font);
        ctx.pop_container();
        return;
    }

    const auto columns = column_count_for_width(content.width, impl_->cell_width, impl_->gap);
    const auto items = impl_->model->row_count();
    const float row_stride = impl_->cell_height + impl_->gap;
    const auto first_grid_row = static_cast<std::size_t>(impl_->scroll_offset / row_stride);
    const auto first_item = std::min(items, first_grid_row * columns);

    for (std::size_t row = first_item; row < items; ++row) {
        const auto cell = cell_rect_for_row(row,
                                            columns,
                                            content,
                                            impl_->cell_width,
                                            impl_->cell_height,
                                            impl_->gap,
                                            impl_->scroll_offset);
        if (cell.y >= content.bottom()) {
            break;
        }
        if (cell.bottom() <= content.y) {
            continue;
        }

        const bool selected = impl_->selection && impl_->selection->is_selected(row);
        const bool current = impl_->selection && impl_->selection->current_row() == row;
        const bool hovered = impl_->hovered_row == row;
        if (selected) {
            ctx.add_rounded_rect(
                cell, Color{selected_bg.r, selected_bg.g, selected_bg.b, 0.82F}, 7.0F);
        } else if (hovered) {
            ctx.add_rounded_rect(cell, hover_bg, 7.0F);
        } else {
            ctx.add_rounded_rect(cell, Color{background.r, background.g, background.b, 0.0F}, 7.0F);
        }

        ctx.add_border(cell, cell_border, 1.0F, 7.0F);
        if (current) {
            ctx.add_border(cell, Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.9F}, 1.5F, 7.0F);
        }

        const auto text = impl_->model->display_text(row);
        const Rect text_rect = {cell.x + 9.0F,
                                cell.y + 8.0F,
                                std::max(0.0F, cell.width - 18.0F),
                                std::max(0.0F, cell.height - 16.0F)};
        const auto measured = measure_text_wrapped(text, font, text_rect.width);
        const float text_y =
            text_rect.y +
            std::max(0.0F, (text_rect.height - std::min(measured.height, text_rect.height)) * 0.5F);
        ctx.push_rounded_clip(text_rect, 4.0F);
        ctx.add_wrapped_text({text_rect.x, text_y},
                             std::string(text),
                             selected ? selected_text : text_color,
                             font,
                             text_rect.width);
        ctx.pop_container();
    }

    const float total_height =
        content_height_for_items(items, columns, impl_->cell_height, impl_->gap);
    if (total_height > content.height) {
        constexpr float ScrollbarWidth = 11.0F;
        const float track_x = inner.right() - ScrollbarWidth - 4.0F;
        ctx.add_rounded_rect({track_x, inner.y + 6.0F, ScrollbarWidth, inner.height - 12.0F},
                             scrollbar_track,
                             ScrollbarWidth * 0.5F);

        const float max_offset = std::max(1.0F, total_height - content.height);
        const float thumb_height =
            std::max(28.0F, (content.height / total_height) * (inner.height - 12.0F));
        const float thumb_y =
            inner.y + 6.0F +
            (impl_->scroll_offset / max_offset) * ((inner.height - 12.0F) - thumb_height);
        ctx.add_rounded_rect({track_x + 2.0F, thumb_y, ScrollbarWidth - 4.0F, thumb_height},
                             scrollbar_thumb,
                             (ScrollbarWidth - 4.0F) * 0.5F);
    }

    ctx.pop_container();
}

void GridView::reconnect_model() {
    impl_->rows_inserted.disconnect();
    impl_->rows_removed.disconnect();
    impl_->data_changed.disconnect();
    impl_->model_reset.disconnect();
    if (!impl_->model) {
        return;
    }

    auto refresh = [this] {
        const auto content = grid_content_rect(
            *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
        clamp_scroll_offset(impl_->scroll_offset,
                            impl_->model.get(),
                            impl_->cell_width,
                            impl_->cell_height,
                            impl_->gap,
                            content.width,
                            content.height);
        if (impl_->hovered_row >= impl_->model->row_count()) {
            impl_->hovered_row = invalid_row();
        }
        sync_accessible_summary();
        queue_layout();
        queue_redraw();
    };

    impl_->rows_inserted = ScopedConnection(impl_->model->on_rows_inserted().connect(
        [refresh](std::size_t, std::size_t) { refresh(); }));
    impl_->rows_removed = ScopedConnection(impl_->model->on_rows_removed().connect(
        [refresh](std::size_t, std::size_t) { refresh(); }));
    impl_->data_changed = ScopedConnection(impl_->model->on_data_changed().connect(
        [refresh](std::size_t, std::size_t) { refresh(); }));
    impl_->model_reset =
        ScopedConnection(impl_->model->on_model_reset().connect([refresh] { refresh(); }));
}

void GridView::queue_selection_redraw() {
    sync_accessible_summary();
    queue_redraw();
}

void GridView::sync_accessible_summary() {
    auto& accessible = ensure_accessible();
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    const auto columns = column_count_for_width(content.width, impl_->cell_width, impl_->gap);
    accessible.set_description(
        grid_accessible_description(impl_->model.get(), impl_->selection.get(), columns));
    accessible.set_value(grid_accessible_value(impl_->model.get(), impl_->selection.get()));
}

void GridView::select_item(std::size_t row) {
    if (!impl_->model || row >= impl_->model->row_count()) {
        return;
    }
    if (impl_->selection) {
        impl_->selection->set_current_row(row);
        impl_->selection->select(row);
    }
    ensure_item_visible(row);
    sync_accessible_summary();
    queue_redraw();
}

void GridView::ensure_item_visible(std::size_t row) {
    if (!impl_->model || row >= impl_->model->row_count()) {
        return;
    }
    const auto content = grid_content_rect(
        *this, impl_->model.get(), impl_->cell_width, impl_->cell_height, impl_->gap);
    const auto columns = column_count_for_width(content.width, impl_->cell_width, impl_->gap);
    const auto grid_row = row / columns;
    const float item_top = static_cast<float>(grid_row) * (impl_->cell_height + impl_->gap);
    const float item_bottom = item_top + impl_->cell_height;
    if (item_top < impl_->scroll_offset) {
        impl_->scroll_offset = item_top;
    } else if (item_bottom > impl_->scroll_offset + content.height) {
        impl_->scroll_offset = item_bottom - content.height;
    }
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->model.get(),
                        impl_->cell_width,
                        impl_->cell_height,
                        impl_->gap,
                        content.width,
                        content.height);
}

std::size_t GridView::current_item() const {
    const auto current = current_model_row(impl_->selection.get());
    if (impl_->model && current < impl_->model->row_count()) {
        return current;
    }
    if (impl_->model && impl_->model->row_count() > 0) {
        return 0;
    }
    return invalid_row();
}

} // namespace nk
