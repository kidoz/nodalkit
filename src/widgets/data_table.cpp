#include <algorithm>
#include <cmath>
#include <nk/accessibility/role.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/list_model_adapters.h>
#include <nk/model/selection_model.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/data_table.h>
#include <numeric>
#include <set>

namespace nk {

namespace {

std::size_t invalid_row() {
    return static_cast<std::size_t>(-1);
}

FontDescriptor table_font(float size = 13.0F, FontWeight weight = FontWeight::Regular) {
    return FontDescriptor{
        .family = {},
        .size = size,
        .weight = weight,
    };
}

float total_column_width(std::span<const DataTableColumn> columns) {
    float width = 0.0F;
    for (const auto& column : columns) {
        width += std::max(1.0F, column.width);
    }
    return width;
}

Rect table_inner_rect(const DataTable& table) {
    auto body = table.allocation();
    if (has_flag(table.state_flags(), StateFlags::Focused)) {
        body = {body.x + 2.0F, body.y + 2.0F, body.width - 4.0F, body.height - 4.0F};
    }
    return {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
}

Rect table_header_rect(const DataTable& table, float header_height) {
    const auto inner = table_inner_rect(table);
    return {inner.x, inner.y, inner.width, std::min(header_height, inner.height)};
}

Rect table_body_rect(const DataTable& table, float header_height) {
    const auto inner = table_inner_rect(table);
    const float header = std::min(header_height, inner.height);
    return {inner.x, inner.y + header, inner.width, std::max(0.0F, inner.height - header)};
}

float max_scroll_offset(const AbstractListModel* model, float row_height, float viewport_height) {
    if (!model || viewport_height <= 0.0F) {
        return 0.0F;
    }
    const float content_height = static_cast<float>(model->row_count()) * row_height;
    return std::max(0.0F, content_height - viewport_height);
}

void clamp_scroll_offset(float& scroll_offset,
                         const AbstractListModel* model,
                         float row_height,
                         float viewport_height) {
    scroll_offset =
        std::clamp(scroll_offset, 0.0F, max_scroll_offset(model, row_height, viewport_height));
}

std::size_t current_source_row(const SelectionModel* selection) {
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

int view_row_at_point(const AbstractListModel* display_model,
                      float row_height,
                      float scroll_offset,
                      Rect body,
                      Point point) {
    if (!display_model || row_height <= 0.0F || !body.contains(point)) {
        return -1;
    }

    const float local_y = point.y - body.y + scroll_offset;
    const int row = static_cast<int>(local_y / row_height);
    if (row < 0 || row >= static_cast<int>(display_model->row_count())) {
        return -1;
    }
    return row;
}

int column_at_point(std::span<const DataTableColumn> columns, Rect header, Point point) {
    if (!header.contains(point)) {
        return -1;
    }

    float x = header.x;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const float width = std::max(1.0F, columns[index].width);
        if (point.x >= x && point.x < x + width) {
            return static_cast<int>(index);
        }
        x += width;
    }
    return -1;
}

std::string default_cell_text(const AbstractListModel& model, std::size_t row) {
    return model.display_text(row);
}

std::string
cell_text(const DataTableColumn& column, const AbstractListModel& model, std::size_t source_row) {
    if (column.text) {
        return column.text(model, source_row);
    }
    return default_cell_text(model, source_row);
}

DataTableSortDirection toggled_direction(DataTableSortDirection current) {
    return current == DataTableSortDirection::Ascending ? DataTableSortDirection::Descending
                                                        : DataTableSortDirection::Ascending;
}

} // namespace

struct DataTable::Impl {
    std::shared_ptr<AbstractListModel> model;
    std::shared_ptr<SortListModel> display_model = std::make_shared<SortListModel>();
    std::shared_ptr<SelectionModel> selection;
    std::vector<DataTableColumn> columns;
    float row_height = 28.0F;
    float header_height = 30.0F;
    float scroll_offset = 0.0F;
    std::optional<std::size_t> sort_column;
    DataTableSortDirection sort_direction = DataTableSortDirection::None;
    Signal<std::size_t> row_activated;
    ScopedConnection rows_inserted;
    ScopedConnection rows_removed;
    ScopedConnection data_changed;
    ScopedConnection model_reset;
    ScopedConnection selection_changed;
    ScopedConnection current_changed;
    std::set<std::size_t> last_selected_rows;
    std::size_t last_current_row = invalid_row();
};

std::shared_ptr<DataTable> DataTable::create() {
    return std::shared_ptr<DataTable>(new DataTable());
}

DataTable::DataTable() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("data-table");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Grid);
    accessible.set_name("Data table");
}

DataTable::~DataTable() = default;

void DataTable::set_model(std::shared_ptr<AbstractListModel> model) {
    if (impl_->model == model) {
        return;
    }

    impl_->model = std::move(model);
    impl_->display_model->set_source_model(impl_->model);
    reconnect_model();
    rebuild_sort();
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->display_model.get(),
                        impl_->row_height,
                        table_body_rect(*this, impl_->header_height).height);
    queue_layout();
    queue_redraw();
}

AbstractListModel* DataTable::model() const {
    return impl_->model.get();
}

void DataTable::set_selection_model(std::shared_ptr<SelectionModel> model) {
    impl_->selection = std::move(model);
    impl_->selection_changed.disconnect();
    impl_->current_changed.disconnect();
    if (impl_->selection) {
        impl_->selection_changed = ScopedConnection(
            impl_->selection->on_selection_changed().connect([this] { queue_selection_redraw(); }));
        impl_->current_changed = ScopedConnection(impl_->selection->on_current_changed().connect(
            [this](std::size_t) { queue_selection_redraw(); }));
    }
    impl_->last_selected_rows =
        impl_->selection ? impl_->selection->selected_rows() : std::set<std::size_t>{};
    impl_->last_current_row = current_source_row(impl_->selection.get());
    queue_redraw();
}

SelectionModel* DataTable::selection_model() const {
    return impl_->selection.get();
}

void DataTable::set_columns(std::vector<DataTableColumn> columns) {
    impl_->columns = std::move(columns);
    if (impl_->sort_column && *impl_->sort_column >= impl_->columns.size()) {
        clear_sort();
        return;
    }
    rebuild_sort();
    queue_layout();
    queue_redraw();
}

std::span<const DataTableColumn> DataTable::columns() const {
    return impl_->columns;
}

void DataTable::set_row_height(float height) {
    impl_->row_height = std::max(1.0F, height);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->display_model.get(),
                        impl_->row_height,
                        table_body_rect(*this, impl_->header_height).height);
    queue_layout();
    queue_redraw();
}

float DataTable::row_height() const {
    return impl_->row_height;
}

void DataTable::set_header_height(float height) {
    impl_->header_height = std::max(1.0F, height);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->display_model.get(),
                        impl_->row_height,
                        table_body_rect(*this, impl_->header_height).height);
    queue_layout();
    queue_redraw();
}

float DataTable::header_height() const {
    return impl_->header_height;
}

void DataTable::sort_by_column(std::size_t column, DataTableSortDirection direction) {
    if (column >= impl_->columns.size() || !impl_->columns[column].sortable ||
        direction == DataTableSortDirection::None) {
        clear_sort();
        return;
    }

    impl_->sort_column = column;
    impl_->sort_direction = direction;
    rebuild_sort();
    queue_redraw();
}

void DataTable::clear_sort() {
    impl_->sort_column.reset();
    impl_->sort_direction = DataTableSortDirection::None;
    rebuild_sort();
    queue_redraw();
}

std::optional<std::size_t> DataTable::sort_column() const {
    return impl_->sort_column;
}

DataTableSortDirection DataTable::sort_direction() const {
    return impl_->sort_direction;
}

Signal<std::size_t>& DataTable::on_row_activated() {
    return impl_->row_activated;
}

SizeRequest DataTable::measure(const Constraints& /*constraints*/) const {
    const float min_width = std::min(220.0F, std::max(1.0F, total_column_width(impl_->columns)));
    const float natural_width = std::max(320.0F, total_column_width(impl_->columns) + 2.0F);
    const float body_rows =
        impl_->display_model
            ? std::min<std::size_t>(8, std::max<std::size_t>(4, impl_->display_model->row_count()))
            : 4;
    const float natural_height = impl_->header_height + (body_rows * impl_->row_height) + 2.0F;
    return {
        min_width, impl_->header_height + impl_->row_height + 2.0F, natural_width, natural_height};
}

void DataTable::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->display_model.get(),
                        impl_->row_height,
                        table_body_rect(*this, impl_->header_height).height);
}

bool DataTable::handle_mouse_event(const MouseEvent& event) {
    if (event.type == MouseEvent::Type::Press && event.button == 1) {
        const Point point{event.x, event.y};
        const auto header = table_header_rect(*this, impl_->header_height);
        const int column = column_at_point(impl_->columns, header, point);
        if (column >= 0) {
            const auto column_index = static_cast<std::size_t>(column);
            if (impl_->columns[column_index].sortable) {
                const auto next_direction =
                    impl_->sort_column && *impl_->sort_column == column_index
                        ? toggled_direction(impl_->sort_direction)
                        : DataTableSortDirection::Ascending;
                sort_by_column(column_index, next_direction);
            }
            return true;
        }

        const auto body = table_body_rect(*this, impl_->header_height);
        const int view_row = view_row_at_point(
            impl_->display_model.get(), impl_->row_height, impl_->scroll_offset, body, point);
        if (view_row >= 0) {
            const auto source_row = source_row_for_view_row(static_cast<std::size_t>(view_row));
            if (impl_->selection) {
                impl_->selection->set_current_row(source_row);
                impl_->selection->select(source_row);
            }
            if (event.click_count >= 2) {
                impl_->row_activated.emit(source_row);
            }
            return true;
        }
        return body.contains(point);
    }

    if (event.type == MouseEvent::Type::Scroll) {
        const auto body = table_body_rect(*this, impl_->header_height);
        if (!body.contains({event.x, event.y}) || !impl_->display_model) {
            return false;
        }
        impl_->scroll_offset = std::clamp(
            impl_->scroll_offset - (event.scroll_dy * impl_->row_height),
            0.0F,
            max_scroll_offset(impl_->display_model.get(), impl_->row_height, body.height));
        queue_redraw();
        return true;
    }

    return false;
}

bool DataTable::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || !impl_->display_model ||
        impl_->display_model->row_count() == 0) {
        return false;
    }

    auto select_view_row = [this](std::size_t view_row) {
        const auto source_row = source_row_for_view_row(view_row);
        if (impl_->selection) {
            impl_->selection->set_current_row(source_row);
            impl_->selection->select(source_row);
        }

        const auto body = table_body_rect(*this, impl_->header_height);
        const float row_top = static_cast<float>(view_row) * impl_->row_height;
        const float row_bottom = row_top + impl_->row_height;
        if (row_top < impl_->scroll_offset) {
            impl_->scroll_offset = row_top;
        } else if (row_bottom > impl_->scroll_offset + body.height) {
            impl_->scroll_offset = row_bottom - body.height;
        }
        clamp_scroll_offset(
            impl_->scroll_offset, impl_->display_model.get(), impl_->row_height, body.height);
        queue_redraw();
    };

    const auto row_count = impl_->display_model->row_count();
    std::size_t current_view_row = 0;
    if (const auto current_source = current_source_row(impl_->selection.get());
        current_source != invalid_row()) {
        current_view_row = view_row_for_source_row(current_source).value_or(0);
    }

    switch (event.key) {
    case KeyCode::Up:
        select_view_row(current_view_row > 0 ? current_view_row - 1 : 0);
        return true;
    case KeyCode::Down:
        select_view_row(std::min(current_view_row + 1, row_count - 1));
        return true;
    case KeyCode::Home:
        select_view_row(0);
        return true;
    case KeyCode::End:
        select_view_row(row_count - 1);
        return true;
    case KeyCode::PageUp: {
        const auto body = table_body_rect(*this, impl_->header_height);
        const auto page_rows =
            std::max<std::size_t>(1, static_cast<std::size_t>(body.height / impl_->row_height));
        select_view_row(current_view_row > page_rows ? current_view_row - page_rows : 0);
        return true;
    }
    case KeyCode::PageDown: {
        const auto body = table_body_rect(*this, impl_->header_height);
        const auto page_rows =
            std::max<std::size_t>(1, static_cast<std::size_t>(body.height / impl_->row_height));
        select_view_row(std::min(current_view_row + page_rows, row_count - 1));
        return true;
    }
    case KeyCode::Return:
    case KeyCode::Space:
        impl_->row_activated.emit(source_row_for_view_row(current_view_row));
        return true;
    default:
        return false;
    }
}

void DataTable::snapshot(SnapshotContext& ctx) const {
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
    const auto header_bg = theme_color("header-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto separator = theme_color("row-separator-color", Color{0.88F, 0.89F, 0.92F, 1.0F});
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto muted_text = theme_color("muted-text-color", Color{0.42F, 0.45F, 0.50F, 1.0F});
    const auto selected_bg = theme_color("selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});
    const auto selected_text = theme_color("selected-text-color", text_color);

    ctx.add_rounded_rect(body, background, corner_radius);
    ctx.add_border(body, border, 1.0F, corner_radius);

    const Rect inner = {body.x + 1.0F,
                        body.y + 1.0F,
                        std::max(0.0F, body.width - 2.0F),
                        std::max(0.0F, body.height - 2.0F)};
    const Rect header = {
        inner.x, inner.y, inner.width, std::min(impl_->header_height, inner.height)};
    const Rect rows = {
        inner.x, header.bottom(), inner.width, std::max(0.0F, inner.bottom() - header.bottom())};

    ctx.push_rounded_clip(inner, corner_radius);
    ctx.add_color_rect(header, header_bg);
    ctx.add_color_rect({header.x, header.bottom() - 1.0F, header.width, 1.0F}, border);

    const auto header_font = table_font(12.5F, FontWeight::Medium);
    const auto cell_font = table_font();
    float x = header.x;
    for (std::size_t column_index = 0; column_index < impl_->columns.size(); ++column_index) {
        const auto& column = impl_->columns[column_index];
        const float width = std::max(1.0F, column.width);
        const Rect cell = {x, header.y, width, header.height};
        const std::string suffix =
            impl_->sort_column && *impl_->sort_column == column_index
                ? (impl_->sort_direction == DataTableSortDirection::Ascending ? " ^" : " v")
                : "";
        ctx.add_text(
            {cell.x + 10.0F, cell.y + 8.0F}, column.title + suffix, muted_text, header_font);
        ctx.add_color_rect({cell.right() - 1.0F, cell.y, 1.0F, inner.height}, separator);
        x += width;
    }

    if (impl_->model && impl_->display_model && rows.height > 0.0F) {
        const auto total_rows = impl_->display_model->row_count();
        const auto first_row = static_cast<std::size_t>(impl_->scroll_offset / impl_->row_height);
        float y = rows.y - std::fmod(impl_->scroll_offset, impl_->row_height);
        for (std::size_t view_row = first_row; view_row < total_rows && y < rows.bottom();
             ++view_row) {
            if (y + impl_->row_height <= rows.y) {
                y += impl_->row_height;
                continue;
            }

            const auto source_row = source_row_for_view_row(view_row);
            const bool selected = impl_->selection && impl_->selection->is_selected(source_row);
            if (selected) {
                ctx.add_color_rect({rows.x, y, rows.width, impl_->row_height},
                                   Color{selected_bg.r, selected_bg.g, selected_bg.b, 0.8F});
            }

            float cell_x = rows.x;
            for (const auto& column : impl_->columns) {
                const float width = std::max(1.0F, column.width);
                const auto text = cell_text(column, *impl_->model, source_row);
                ctx.add_text({cell_x + 10.0F, y + 7.0F},
                             text,
                             selected ? selected_text : text_color,
                             cell_font);
                cell_x += width;
            }
            ctx.add_color_rect({rows.x, y + impl_->row_height - 1.0F, rows.width, 1.0F}, separator);
            y += impl_->row_height;
        }
    }

    ctx.pop_container();
}

void DataTable::reconnect_model() {
    impl_->rows_inserted.disconnect();
    impl_->rows_removed.disconnect();
    impl_->data_changed.disconnect();
    impl_->model_reset.disconnect();
    if (!impl_->model) {
        return;
    }

    auto refresh = [this] {
        rebuild_sort();
        clamp_scroll_offset(impl_->scroll_offset,
                            impl_->display_model.get(),
                            impl_->row_height,
                            table_body_rect(*this, impl_->header_height).height);
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

void DataTable::rebuild_sort() {
    if (!impl_->display_model) {
        return;
    }

    if (!impl_->sort_column || impl_->sort_direction == DataTableSortDirection::None ||
        *impl_->sort_column >= impl_->columns.size()) {
        impl_->display_model->set_less({});
        return;
    }

    const auto column_index = *impl_->sort_column;
    const auto direction = impl_->sort_direction;
    impl_->display_model->set_less([this, column_index, direction](const AbstractListModel& model,
                                                                   std::size_t lhs,
                                                                   std::size_t rhs) {
        const auto lhs_text = cell_text(impl_->columns[column_index], model, lhs);
        const auto rhs_text = cell_text(impl_->columns[column_index], model, rhs);
        if (direction == DataTableSortDirection::Ascending) {
            return lhs_text < rhs_text;
        }
        return rhs_text < lhs_text;
    });
}

void DataTable::queue_selection_redraw() {
    impl_->last_selected_rows =
        impl_->selection ? impl_->selection->selected_rows() : std::set<std::size_t>{};
    impl_->last_current_row = current_source_row(impl_->selection.get());
    queue_redraw();
}

std::size_t DataTable::source_row_for_view_row(std::size_t view_row) const {
    if (!impl_->display_model) {
        return view_row;
    }
    return impl_->display_model->map_to_source(view_row);
}

std::optional<std::size_t> DataTable::view_row_for_source_row(std::size_t source_row) const {
    if (!impl_->display_model) {
        return source_row;
    }
    return impl_->display_model->map_from_source(source_row);
}

} // namespace nk
