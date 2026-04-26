#pragma once

/// @file list_model_adapters.h
/// @brief Filter and sort adapters for list models.

#include <cstddef>
#include <functional>
#include <memory>
#include <nk/model/abstract_list_model.h>
#include <optional>
#include <vector>

namespace nk {

/// Predicate used by FilterListModel. The row argument is a row in the source model.
using ListModelFilter = std::function<bool(const AbstractListModel& model, std::size_t row)>;

/// Comparator used by SortListModel. The row arguments are rows in the source model.
using ListModelLess =
    std::function<bool(const AbstractListModel& model, std::size_t lhs, std::size_t rhs)>;

/// Wraps a source list model and exposes only rows accepted by a predicate.
///
/// Source model changes currently rebuild the filtered mapping and emit a model reset. This keeps
/// the contract simple for the first adapter slice while still supporting existing ListView usage.
class FilterListModel : public AbstractListModel {
public:
    FilterListModel();
    explicit FilterListModel(std::shared_ptr<AbstractListModel> source,
                             ListModelFilter filter = {});
    ~FilterListModel() override;

    FilterListModel(const FilterListModel&) = delete;
    FilterListModel& operator=(const FilterListModel&) = delete;

    void set_source_model(std::shared_ptr<AbstractListModel> source);
    [[nodiscard]] AbstractListModel* source_model() const;

    void set_filter(ListModelFilter filter);
    [[nodiscard]] bool has_filter() const;

    [[nodiscard]] std::size_t row_count() const override;
    [[nodiscard]] std::any data(std::size_t row) const override;
    [[nodiscard]] std::string display_text(std::size_t row) const override;

    [[nodiscard]] std::size_t map_to_source(std::size_t row) const;
    [[nodiscard]] std::optional<std::size_t> map_from_source(std::size_t source_row) const;

private:
    void rebuild();
    void reconnect_source();
    void handle_source_changed();

    std::shared_ptr<AbstractListModel> source_;
    ListModelFilter filter_;
    std::vector<std::size_t> rows_;
    std::vector<ScopedConnection> source_connections_;
};

/// Wraps a source list model and exposes rows in a stable sorted order.
///
/// Source model changes currently rebuild the sorted mapping and emit a model reset. Without a
/// comparator, the adapter preserves the source order.
class SortListModel : public AbstractListModel {
public:
    SortListModel();
    explicit SortListModel(std::shared_ptr<AbstractListModel> source, ListModelLess less = {});
    ~SortListModel() override;

    SortListModel(const SortListModel&) = delete;
    SortListModel& operator=(const SortListModel&) = delete;

    void set_source_model(std::shared_ptr<AbstractListModel> source);
    [[nodiscard]] AbstractListModel* source_model() const;

    void set_less(ListModelLess less);
    [[nodiscard]] bool has_less() const;

    [[nodiscard]] std::size_t row_count() const override;
    [[nodiscard]] std::any data(std::size_t row) const override;
    [[nodiscard]] std::string display_text(std::size_t row) const override;

    [[nodiscard]] std::size_t map_to_source(std::size_t row) const;
    [[nodiscard]] std::optional<std::size_t> map_from_source(std::size_t source_row) const;

private:
    void rebuild();
    void reconnect_source();
    void handle_source_changed();

    std::shared_ptr<AbstractListModel> source_;
    ListModelLess less_;
    std::vector<std::size_t> rows_;
    std::vector<ScopedConnection> source_connections_;
};

} // namespace nk
