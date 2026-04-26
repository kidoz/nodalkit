#include <algorithm>
#include <cassert>
#include <nk/model/list_model_adapters.h>
#include <numeric>

namespace nk {

namespace {

void disconnect_all(std::vector<ScopedConnection>& connections) {
    for (auto& connection : connections) {
        connection.disconnect();
    }
    connections.clear();
}

} // namespace

FilterListModel::FilterListModel() = default;

FilterListModel::FilterListModel(std::shared_ptr<AbstractListModel> source, ListModelFilter filter)
    : source_(std::move(source)), filter_(std::move(filter)) {
    reconnect_source();
    rebuild();
}

FilterListModel::~FilterListModel() = default;

void FilterListModel::set_source_model(std::shared_ptr<AbstractListModel> source) {
    if (source_ == source) {
        return;
    }

    source_ = std::move(source);
    reconnect_source();
    rebuild();
    notify_model_reset();
}

AbstractListModel* FilterListModel::source_model() const {
    return source_.get();
}

void FilterListModel::set_filter(ListModelFilter filter) {
    filter_ = std::move(filter);
    rebuild();
    notify_model_reset();
}

bool FilterListModel::has_filter() const {
    return static_cast<bool>(filter_);
}

std::size_t FilterListModel::row_count() const {
    return rows_.size();
}

std::any FilterListModel::data(std::size_t row) const {
    assert(source_ != nullptr);
    return source_->data(map_to_source(row));
}

std::string FilterListModel::display_text(std::size_t row) const {
    assert(source_ != nullptr);
    return source_->display_text(map_to_source(row));
}

std::size_t FilterListModel::map_to_source(std::size_t row) const {
    assert(row < rows_.size());
    return rows_[row];
}

std::optional<std::size_t> FilterListModel::map_from_source(std::size_t source_row) const {
    const auto iter = std::find(rows_.begin(), rows_.end(), source_row);
    if (iter == rows_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(rows_.begin(), iter));
}

void FilterListModel::rebuild() {
    rows_.clear();
    if (!source_) {
        return;
    }

    const auto count = source_->row_count();
    rows_.reserve(count);
    for (std::size_t row = 0; row < count; ++row) {
        if (!filter_ || filter_(*source_, row)) {
            rows_.push_back(row);
        }
    }
}

void FilterListModel::reconnect_source() {
    disconnect_all(source_connections_);
    if (!source_) {
        return;
    }

    source_connections_.push_back(ScopedConnection(source_->on_rows_inserted().connect(
        [this](std::size_t, std::size_t) { handle_source_changed(); })));
    source_connections_.push_back(ScopedConnection(source_->on_rows_removed().connect(
        [this](std::size_t, std::size_t) { handle_source_changed(); })));
    source_connections_.push_back(ScopedConnection(source_->on_data_changed().connect(
        [this](std::size_t, std::size_t) { handle_source_changed(); })));
    source_connections_.push_back(
        ScopedConnection(source_->on_model_reset().connect([this] { handle_source_changed(); })));
}

void FilterListModel::handle_source_changed() {
    rebuild();
    notify_model_reset();
}

SortListModel::SortListModel() = default;

SortListModel::SortListModel(std::shared_ptr<AbstractListModel> source, ListModelLess less)
    : source_(std::move(source)), less_(std::move(less)) {
    reconnect_source();
    rebuild();
}

SortListModel::~SortListModel() = default;

void SortListModel::set_source_model(std::shared_ptr<AbstractListModel> source) {
    if (source_ == source) {
        return;
    }

    source_ = std::move(source);
    reconnect_source();
    rebuild();
    notify_model_reset();
}

AbstractListModel* SortListModel::source_model() const {
    return source_.get();
}

void SortListModel::set_less(ListModelLess less) {
    less_ = std::move(less);
    rebuild();
    notify_model_reset();
}

bool SortListModel::has_less() const {
    return static_cast<bool>(less_);
}

std::size_t SortListModel::row_count() const {
    return rows_.size();
}

std::any SortListModel::data(std::size_t row) const {
    assert(source_ != nullptr);
    return source_->data(map_to_source(row));
}

std::string SortListModel::display_text(std::size_t row) const {
    assert(source_ != nullptr);
    return source_->display_text(map_to_source(row));
}

std::size_t SortListModel::map_to_source(std::size_t row) const {
    assert(row < rows_.size());
    return rows_[row];
}

std::optional<std::size_t> SortListModel::map_from_source(std::size_t source_row) const {
    const auto iter = std::find(rows_.begin(), rows_.end(), source_row);
    if (iter == rows_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(rows_.begin(), iter));
}

void SortListModel::rebuild() {
    rows_.clear();
    if (!source_) {
        return;
    }

    rows_.resize(source_->row_count());
    std::iota(rows_.begin(), rows_.end(), std::size_t{0});
    if (less_) {
        std::stable_sort(rows_.begin(), rows_.end(), [this](std::size_t lhs, std::size_t rhs) {
            return less_(*source_, lhs, rhs);
        });
    }
}

void SortListModel::reconnect_source() {
    disconnect_all(source_connections_);
    if (!source_) {
        return;
    }

    source_connections_.push_back(ScopedConnection(source_->on_rows_inserted().connect(
        [this](std::size_t, std::size_t) { handle_source_changed(); })));
    source_connections_.push_back(ScopedConnection(source_->on_rows_removed().connect(
        [this](std::size_t, std::size_t) { handle_source_changed(); })));
    source_connections_.push_back(ScopedConnection(source_->on_data_changed().connect(
        [this](std::size_t, std::size_t) { handle_source_changed(); })));
    source_connections_.push_back(
        ScopedConnection(source_->on_model_reset().connect([this] { handle_source_changed(); })));
}

void SortListModel::handle_source_changed() {
    rebuild();
    notify_model_reset();
}

} // namespace nk
