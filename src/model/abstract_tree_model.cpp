#include <nk/model/abstract_tree_model.h>
#include <memory>

namespace nk {

struct AbstractTreeModel::Impl {
    Signal<TreeIndex, std::size_t, std::size_t> nodes_about_to_insert;
    Signal<TreeIndex, std::size_t, std::size_t> nodes_inserted;
    Signal<TreeIndex, std::size_t, std::size_t> nodes_about_to_remove;
    Signal<TreeIndex, std::size_t, std::size_t> nodes_removed;
    Signal<TreeIndex, std::size_t, std::size_t> data_changed;
    Signal<> model_reset;

    TreeIndex pending_parent{};
    std::size_t pending_first = 0;
    std::size_t pending_count = 0;
};

AbstractTreeModel::AbstractTreeModel() : impl_(std::make_unique<Impl>()) {}

AbstractTreeModel::~AbstractTreeModel() = default;

std::string AbstractTreeModel::display_text(const TreeIndex& /*index*/) const {
    return "";
}

Signal<TreeIndex, std::size_t, std::size_t>& AbstractTreeModel::on_nodes_about_to_insert() { return impl_->nodes_about_to_insert; }
Signal<TreeIndex, std::size_t, std::size_t>& AbstractTreeModel::on_nodes_inserted() { return impl_->nodes_inserted; }
Signal<TreeIndex, std::size_t, std::size_t>& AbstractTreeModel::on_nodes_about_to_remove() { return impl_->nodes_about_to_remove; }
Signal<TreeIndex, std::size_t, std::size_t>& AbstractTreeModel::on_nodes_removed() { return impl_->nodes_removed; }
Signal<TreeIndex, std::size_t, std::size_t>& AbstractTreeModel::on_data_changed() { return impl_->data_changed; }
Signal<>& AbstractTreeModel::on_model_reset() { return impl_->model_reset; }

void AbstractTreeModel::begin_insert_nodes(const TreeIndex& parent, std::size_t first, std::size_t count) {
    impl_->pending_parent = parent;
    impl_->pending_first = first;
    impl_->pending_count = count;
    impl_->nodes_about_to_insert.emit(parent, first, count);
}

void AbstractTreeModel::end_insert_nodes() {
    impl_->nodes_inserted.emit(impl_->pending_parent, impl_->pending_first, impl_->pending_count);
}

void AbstractTreeModel::begin_remove_nodes(const TreeIndex& parent, std::size_t first, std::size_t count) {
    impl_->pending_parent = parent;
    impl_->pending_first = first;
    impl_->pending_count = count;
    impl_->nodes_about_to_remove.emit(parent, first, count);
}

void AbstractTreeModel::end_remove_nodes() {
    impl_->nodes_removed.emit(impl_->pending_parent, impl_->pending_first, impl_->pending_count);
}

void AbstractTreeModel::notify_data_changed(const TreeIndex& parent, std::size_t first, std::size_t last) {
    impl_->data_changed.emit(parent, first, last);
}

void AbstractTreeModel::notify_model_reset() {
    impl_->model_reset.emit();
}

} // namespace nk