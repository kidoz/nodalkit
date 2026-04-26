#include <nk/model/selection_model.h>

namespace nk {

struct SelectionModel::Impl {
    SelectionMode mode = SelectionMode::Single;
    std::set<std::size_t> selected;
    std::size_t current = static_cast<std::size_t>(-1);
    Signal<> selection_changed;
    Signal<std::size_t> current_changed;
};

SelectionModel::SelectionModel(SelectionMode mode) : impl_(std::make_unique<Impl>()) {
    impl_->mode = mode;
}

SelectionModel::~SelectionModel() = default;

SelectionMode SelectionModel::mode() const {
    return impl_->mode;
}

void SelectionModel::set_mode(SelectionMode mode) {
    impl_->mode = mode;
    clear();
}

void SelectionModel::select(std::size_t row) {
    if (impl_->mode == SelectionMode::None) {
        return;
    }
    if (impl_->mode == SelectionMode::Single) {
        impl_->selected.clear();
    }
    impl_->selected.insert(row);
    impl_->selection_changed.emit();
}

void SelectionModel::deselect(std::size_t row) {
    if (impl_->selected.erase(row) > 0) {
        impl_->selection_changed.emit();
    }
}

void SelectionModel::toggle(std::size_t row) {
    if (is_selected(row)) {
        deselect(row);
    } else {
        select(row);
    }
}

void SelectionModel::clear() {
    if (!impl_->selected.empty()) {
        impl_->selected.clear();
        impl_->selection_changed.emit();
    }
}

bool SelectionModel::is_selected(std::size_t row) const {
    return impl_->selected.contains(row);
}

const std::set<std::size_t>& SelectionModel::selected_rows() const {
    return impl_->selected;
}

std::size_t SelectionModel::current_row() const {
    return impl_->current;
}

void SelectionModel::set_current_row(std::size_t row) {
    if (impl_->current != row) {
        impl_->current = row;
        impl_->current_changed.emit(row);
    }
}

Signal<>& SelectionModel::on_selection_changed() {
    return impl_->selection_changed;
}

Signal<std::size_t>& SelectionModel::on_current_changed() {
    return impl_->current_changed;
}

} // namespace nk
