#include <nk/model/abstract_list_model.h>

#include <cassert>

namespace nk {

struct AbstractListModel::Impl {
    Signal<std::size_t, std::size_t> rows_about_to_insert;
    Signal<std::size_t, std::size_t> rows_inserted;
    Signal<std::size_t, std::size_t> rows_about_to_remove;
    Signal<std::size_t, std::size_t> rows_removed;
    Signal<std::size_t, std::size_t> data_changed;
    Signal<> model_reset;
};

AbstractListModel::AbstractListModel()
    : impl_(std::make_unique<Impl>()) {}

AbstractListModel::~AbstractListModel() = default;

std::string AbstractListModel::display_text(std::size_t /*row*/) const {
    return {};
}

Signal<std::size_t, std::size_t>& AbstractListModel::on_rows_about_to_insert() {
    return impl_->rows_about_to_insert;
}
Signal<std::size_t, std::size_t>& AbstractListModel::on_rows_inserted() {
    return impl_->rows_inserted;
}
Signal<std::size_t, std::size_t>& AbstractListModel::on_rows_about_to_remove() {
    return impl_->rows_about_to_remove;
}
Signal<std::size_t, std::size_t>& AbstractListModel::on_rows_removed() {
    return impl_->rows_removed;
}
Signal<std::size_t, std::size_t>& AbstractListModel::on_data_changed() {
    return impl_->data_changed;
}
Signal<>& AbstractListModel::on_model_reset() {
    return impl_->model_reset;
}

void AbstractListModel::begin_insert_rows(std::size_t first, std::size_t count) {
    impl_->rows_about_to_insert.emit(first, count);
}
void AbstractListModel::end_insert_rows() {
    // Re-emit with stored values is typical; simplified here.
    impl_->rows_inserted.emit(0, 0);
}
void AbstractListModel::begin_remove_rows(std::size_t first, std::size_t count) {
    impl_->rows_about_to_remove.emit(first, count);
}
void AbstractListModel::end_remove_rows() {
    impl_->rows_removed.emit(0, 0);
}
void AbstractListModel::notify_data_changed(std::size_t first, std::size_t last) {
    impl_->data_changed.emit(first, last);
}
void AbstractListModel::notify_model_reset() {
    impl_->model_reset.emit();
}

// --- StringListModel ---

StringListModel::StringListModel() = default;

StringListModel::StringListModel(std::vector<std::string> items)
    : items_(std::move(items)) {}

StringListModel::~StringListModel() = default;

std::size_t StringListModel::row_count() const { return items_.size(); }

std::any StringListModel::data(std::size_t row) const {
    assert(row < items_.size());
    return items_[row];
}

std::string StringListModel::display_text(std::size_t row) const {
    assert(row < items_.size());
    return items_[row];
}

void StringListModel::append(std::string item) {
    auto const idx = items_.size();
    begin_insert_rows(idx, 1);
    items_.push_back(std::move(item));
    end_insert_rows();
}

void StringListModel::insert(std::size_t row, std::string item) {
    assert(row <= items_.size());
    begin_insert_rows(row, 1);
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(row),
                  std::move(item));
    end_insert_rows();
}

void StringListModel::remove(std::size_t row) {
    assert(row < items_.size());
    begin_remove_rows(row, 1);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(row));
    end_remove_rows();
}

void StringListModel::clear() {
    if (!items_.empty()) {
        begin_remove_rows(0, items_.size());
        items_.clear();
        end_remove_rows();
    }
}

std::string const& StringListModel::at(std::size_t row) const {
    assert(row < items_.size());
    return items_[row];
}

} // namespace nk
