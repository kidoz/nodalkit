#include <nk/model/abstract_table_model.h>
#include <memory>

namespace nk {

struct AbstractTableModel::Impl {
    Signal<std::size_t, std::size_t> rows_about_to_insert;
    Signal<std::size_t, std::size_t> rows_inserted;
    Signal<std::size_t, std::size_t> rows_about_to_remove;
    Signal<std::size_t, std::size_t> rows_removed;

    Signal<std::size_t, std::size_t> columns_about_to_insert;
    Signal<std::size_t, std::size_t> columns_inserted;
    Signal<std::size_t, std::size_t> columns_about_to_remove;
    Signal<std::size_t, std::size_t> columns_removed;

    Signal<std::size_t, std::size_t, std::size_t, std::size_t> data_changed;
    Signal<> model_reset;

    std::size_t pending_row_first = 0;
    std::size_t pending_row_count = 0;
    std::size_t pending_col_first = 0;
    std::size_t pending_col_count = 0;
};

AbstractTableModel::AbstractTableModel() : impl_(std::make_unique<Impl>()) {}

AbstractTableModel::~AbstractTableModel() = default;

std::string AbstractTableModel::display_text(std::size_t /*row*/, std::size_t /*column*/) const {
    return "";
}

std::string AbstractTableModel::header_text(std::size_t /*column*/) const {
    return "";
}

Signal<std::size_t, std::size_t>& AbstractTableModel::on_rows_about_to_insert() { return impl_->rows_about_to_insert; }
Signal<std::size_t, std::size_t>& AbstractTableModel::on_rows_inserted() { return impl_->rows_inserted; }
Signal<std::size_t, std::size_t>& AbstractTableModel::on_rows_about_to_remove() { return impl_->rows_about_to_remove; }
Signal<std::size_t, std::size_t>& AbstractTableModel::on_rows_removed() { return impl_->rows_removed; }

Signal<std::size_t, std::size_t>& AbstractTableModel::on_columns_about_to_insert() { return impl_->columns_about_to_insert; }
Signal<std::size_t, std::size_t>& AbstractTableModel::on_columns_inserted() { return impl_->columns_inserted; }
Signal<std::size_t, std::size_t>& AbstractTableModel::on_columns_about_to_remove() { return impl_->columns_about_to_remove; }
Signal<std::size_t, std::size_t>& AbstractTableModel::on_columns_removed() { return impl_->columns_removed; }

Signal<std::size_t, std::size_t, std::size_t, std::size_t>& AbstractTableModel::on_data_changed() { return impl_->data_changed; }
Signal<>& AbstractTableModel::on_model_reset() { return impl_->model_reset; }

void AbstractTableModel::begin_insert_rows(std::size_t first, std::size_t count) {
    impl_->pending_row_first = first;
    impl_->pending_row_count = count;
    impl_->rows_about_to_insert.emit(first, count);
}

void AbstractTableModel::end_insert_rows() {
    impl_->rows_inserted.emit(impl_->pending_row_first, impl_->pending_row_count);
}

void AbstractTableModel::begin_remove_rows(std::size_t first, std::size_t count) {
    impl_->pending_row_first = first;
    impl_->pending_row_count = count;
    impl_->rows_about_to_remove.emit(first, count);
}

void AbstractTableModel::end_remove_rows() {
    impl_->rows_removed.emit(impl_->pending_row_first, impl_->pending_row_count);
}

void AbstractTableModel::begin_insert_columns(std::size_t first, std::size_t count) {
    impl_->pending_col_first = first;
    impl_->pending_col_count = count;
    impl_->columns_about_to_insert.emit(first, count);
}

void AbstractTableModel::end_insert_columns() {
    impl_->columns_inserted.emit(impl_->pending_col_first, impl_->pending_col_count);
}

void AbstractTableModel::begin_remove_columns(std::size_t first, std::size_t count) {
    impl_->pending_col_first = first;
    impl_->pending_col_count = count;
    impl_->columns_about_to_remove.emit(first, count);
}

void AbstractTableModel::end_remove_columns() {
    impl_->columns_removed.emit(impl_->pending_col_first, impl_->pending_col_count);
}

void AbstractTableModel::notify_data_changed(std::size_t top_row, std::size_t left_column, std::size_t bottom_row, std::size_t right_column) {
    impl_->data_changed.emit(top_row, left_column, bottom_row, right_column);
}

void AbstractTableModel::notify_model_reset() {
    impl_->model_reset.emit();
}

} // namespace nk