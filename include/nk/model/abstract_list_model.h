#pragma once

/// @file abstract_list_model.h
/// @brief Abstract interface for list data models.

#include <any>
#include <cstddef>
#include <nk/foundation/signal.h>
#include <string>

namespace nk {

/// Abstract base for flat list models. Provides data for ListView
/// and similar item-view widgets.
///
/// The model notifies views of changes through signals so that the
/// view can update incrementally instead of rebuilding entirely.
class AbstractListModel {
public:
    virtual ~AbstractListModel();

    AbstractListModel(const AbstractListModel&) = delete;
    AbstractListModel& operator=(const AbstractListModel&) = delete;

    /// Number of items.
    [[nodiscard]] virtual std::size_t row_count() const = 0;

    /// Data for the given row. The std::any may hold any application
    /// type; delegates must know how to unpack it.
    [[nodiscard]] virtual std::any data(std::size_t row) const = 0;

    /// Convenience: data as string (default returns empty).
    [[nodiscard]] virtual std::string display_text(std::size_t row) const;

    // --- Change signals ---

    /// Emitted before rows are inserted. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_about_to_insert();

    /// Emitted after rows are inserted. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_inserted();

    /// Emitted before rows are removed. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_about_to_remove();

    /// Emitted after rows are removed. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_removed();

    /// Emitted when data for existing rows changes. Args: first, last.
    Signal<std::size_t, std::size_t>& on_data_changed();

    /// Emitted when the model is completely reset.
    Signal<>& on_model_reset();

protected:
    AbstractListModel();

    /// Subclasses call these to fire change signals.
    void begin_insert_rows(std::size_t first, std::size_t count);
    void end_insert_rows();
    void begin_remove_rows(std::size_t first, std::size_t count);
    void end_remove_rows();
    void notify_data_changed(std::size_t first, std::size_t last);
    void notify_model_reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A simple in-memory string list model.
class StringListModel : public AbstractListModel {
public:
    StringListModel();
    explicit StringListModel(std::vector<std::string> items);
    ~StringListModel() override;

    [[nodiscard]] std::size_t row_count() const override;
    [[nodiscard]] std::any data(std::size_t row) const override;
    [[nodiscard]] std::string display_text(std::size_t row) const override;

    void append(std::string item);
    void insert(std::size_t row, std::string item);
    void remove(std::size_t row);
    void clear();

    [[nodiscard]] const std::string& at(std::size_t row) const;

private:
    std::vector<std::string> items_;
};

} // namespace nk
