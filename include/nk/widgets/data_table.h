#pragma once

/// @file data_table.h
/// @brief Model-backed table widget with sortable columns.

#include <cstddef>
#include <functional>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nk {

class AbstractListModel;
class SelectionModel;

/// Sort state for a DataTable column.
enum class DataTableSortDirection {
    None,
    Ascending,
    Descending,
};

/// Text provider for a table cell. The row argument is a row in the source model.
using DataTableCellText =
    std::function<std::string(const AbstractListModel& model, std::size_t row)>;

/// Column description for DataTable.
struct DataTableColumn {
    std::string id;
    std::string title;
    float width = 140.0F;
    bool sortable = true;
    DataTableCellText text;
};

/// A model-backed data table with column headers, single-row navigation, and
/// header-click sorting.
class DataTable : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<DataTable> create();
    ~DataTable() override;

    void set_model(std::shared_ptr<AbstractListModel> model);
    [[nodiscard]] AbstractListModel* model() const;

    void set_selection_model(std::shared_ptr<SelectionModel> model);
    [[nodiscard]] SelectionModel* selection_model() const;

    void set_columns(std::vector<DataTableColumn> columns);
    [[nodiscard]] std::span<const DataTableColumn> columns() const;

    void set_min_column_width(float width);
    [[nodiscard]] float min_column_width() const;

    void set_row_height(float height);
    [[nodiscard]] float row_height() const;

    void set_header_height(float height);
    [[nodiscard]] float header_height() const;

    void sort_by_column(std::size_t column, DataTableSortDirection direction);
    void clear_sort();
    [[nodiscard]] std::optional<std::size_t> sort_column() const;
    [[nodiscard]] DataTableSortDirection sort_direction() const;

    /// Emitted with a source-model row when a row is activated.
    Signal<std::size_t>& on_row_activated();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    DataTable();
    void snapshot(SnapshotContext& ctx) const override;

private:
    void reconnect_model();
    void rebuild_sort();
    void queue_selection_redraw();
    void sync_accessible_summary();
    [[nodiscard]] std::size_t source_row_for_view_row(std::size_t view_row) const;
    [[nodiscard]] std::optional<std::size_t> view_row_for_source_row(std::size_t source_row) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
