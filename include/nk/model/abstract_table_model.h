#pragma once

/// @file abstract_table_model.h
/// @brief Abstract interface for table data models.

#include <any>
#include <cstddef>
#include <nk/foundation/signal.h>
#include <string>

namespace nk {

/// Abstract base for 2D table models — the table counterpart of
/// AbstractListModel.
///
/// This is the canonical contract for row-and-column data going forward, but
/// no in-tree widget consumes it yet: DataTable currently consumes
/// AbstractListModel plus per-column text providers and will migrate to this
/// contract during 0.x.
///
/// The model notifies views of changes through signals so that the
/// view can update incrementally instead of rebuilding entirely.
class AbstractTableModel {
public:
    virtual ~AbstractTableModel();

    AbstractTableModel(const AbstractTableModel&) = delete;
    AbstractTableModel& operator=(const AbstractTableModel&) = delete;

    /// Number of rows.
    [[nodiscard]] virtual std::size_t row_count() const = 0;

    /// Number of columns.
    [[nodiscard]] virtual std::size_t column_count() const = 0;

    /// Data for the given row and column. The std::any may hold any application
    /// type; delegates must know how to unpack it.
    [[nodiscard]] virtual std::any data(std::size_t row, std::size_t column) const = 0;

    /// Convenience: data as string (default returns empty).
    [[nodiscard]] virtual std::string display_text(std::size_t row, std::size_t column) const;

    /// Optional: text to display in the header for a given column.
    [[nodiscard]] virtual std::string header_text(std::size_t column) const;

    // --- Change signals ---

    /// Emitted before rows are inserted. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_about_to_insert();

    /// Emitted after rows are inserted. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_inserted();

    /// Emitted before rows are removed. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_about_to_remove();

    /// Emitted after rows are removed. Args: first_row, count.
    Signal<std::size_t, std::size_t>& on_rows_removed();

    /// Emitted before columns are inserted. Args: first_column, count.
    Signal<std::size_t, std::size_t>& on_columns_about_to_insert();

    /// Emitted after columns are inserted. Args: first_column, count.
    Signal<std::size_t, std::size_t>& on_columns_inserted();

    /// Emitted before columns are removed. Args: first_column, count.
    Signal<std::size_t, std::size_t>& on_columns_about_to_remove();

    /// Emitted after columns are removed. Args: first_column, count.
    Signal<std::size_t, std::size_t>& on_columns_removed();

    /// Emitted when data for an existing block of cells changes.
    /// Args: top_row, left_column, bottom_row, right_column.
    Signal<std::size_t, std::size_t, std::size_t, std::size_t>& on_data_changed();

    /// Emitted when the model is completely reset.
    Signal<>& on_model_reset();

protected:
    AbstractTableModel();

    /// Subclasses call these to fire change signals.
    void begin_insert_rows(std::size_t first, std::size_t count);
    void end_insert_rows();
    void begin_remove_rows(std::size_t first, std::size_t count);
    void end_remove_rows();

    void begin_insert_columns(std::size_t first, std::size_t count);
    void end_insert_columns();
    void begin_remove_columns(std::size_t first, std::size_t count);
    void end_remove_columns();

    void notify_data_changed(std::size_t top_row,
                             std::size_t left_column,
                             std::size_t bottom_row,
                             std::size_t right_column);
    void notify_model_reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
