#pragma once

#include "trading_model.h"
#include "view_list_model.h"

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/model/selection_model.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/data_table.h>
#include <optional>
#include <vector>

namespace trading {

class OrdersView {
public:
    explicit OrdersView(std::shared_ptr<TradingModel> model);

    OrdersView(const OrdersView&) = delete;
    OrdersView& operator=(const OrdersView&) = delete;

    [[nodiscard]] std::shared_ptr<nk::Widget> widget() const { return root_; }

    void refresh();
    [[nodiscard]] std::optional<std::uint64_t> selected_order_id() const;

    nk::Signal<>& on_cancel_requested() { return cancel_requested_; }

private:
    std::shared_ptr<TradingModel> model_;
    std::shared_ptr<ViewListModel> order_rows_;
    std::shared_ptr<nk::SelectionModel> order_selection_;
    std::shared_ptr<nk::DataTable> orders_table_;
    std::shared_ptr<nk::Button> cancel_button_;
    std::shared_ptr<nk::Box> root_;
    std::uint64_t seen_revision_ = 0;
    std::size_t seen_row_count_ = 0;
    nk::Signal<> cancel_requested_;
    std::vector<nk::ScopedConnection> connections_;
};
} // namespace trading
