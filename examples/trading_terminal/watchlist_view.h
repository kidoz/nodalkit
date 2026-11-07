#pragma once

#include "trading_model.h"
#include "view_list_model.h"

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/model/selection_model.h>
#include <nk/widgets/box.h>
#include <nk/widgets/data_table.h>
#include <vector>

namespace trading {

class WatchlistView {
public:
    explicit WatchlistView(std::shared_ptr<TradingModel> model);

    WatchlistView(const WatchlistView&) = delete;
    WatchlistView& operator=(const WatchlistView&) = delete;

    [[nodiscard]] std::shared_ptr<nk::Widget> widget() const { return root_; }

    void refresh();
    void set_selected_instrument(std::size_t index);

    nk::Signal<std::size_t>& on_instrument_selected() { return instrument_selected_; }

private:
    std::shared_ptr<TradingModel> model_;
    std::shared_ptr<ViewListModel> instrument_rows_;
    std::shared_ptr<nk::SelectionModel> watchlist_selection_;
    std::shared_ptr<nk::DataTable> watchlist_table_;
    std::shared_ptr<nk::Box> root_;
    bool syncing_selection_ = false;
    nk::Signal<std::size_t> instrument_selected_;
    std::vector<nk::ScopedConnection> connections_;
};
} // namespace trading
