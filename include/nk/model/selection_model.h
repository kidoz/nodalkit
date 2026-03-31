#pragma once

/// @file selection_model.h
/// @brief Selection tracking for item views.

#include <nk/foundation/signal.h>

#include <cstddef>
#include <set>

namespace nk {

/// Selection behavior mode.
enum class SelectionMode {
    None,     ///< No selection allowed.
    Single,   ///< At most one item selected.
    Multiple, ///< Multiple items can be selected.
};

/// Tracks which rows are selected in an item view.
class SelectionModel {
public:
    explicit SelectionModel(SelectionMode mode = SelectionMode::Single);
    ~SelectionModel();

    SelectionModel(SelectionModel const&) = delete;
    SelectionModel& operator=(SelectionModel const&) = delete;

    [[nodiscard]] SelectionMode mode() const;
    void set_mode(SelectionMode mode);

    /// Select a single row (for Single mode, deselects others).
    void select(std::size_t row);

    /// Deselect a row.
    void deselect(std::size_t row);

    /// Toggle selection for a row.
    void toggle(std::size_t row);

    /// Clear all selection.
    void clear();

    /// Whether a row is selected.
    [[nodiscard]] bool is_selected(std::size_t row) const;

    /// All selected rows.
    [[nodiscard]] std::set<std::size_t> const& selected_rows() const;

    /// The "current" row (e.g. for keyboard navigation). -1 if none.
    [[nodiscard]] std::size_t current_row() const;
    void set_current_row(std::size_t row);

    /// Emitted when the selection changes.
    Signal<>& on_selection_changed();

    /// Emitted when the current row changes.
    Signal<std::size_t>& on_current_changed();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
