#pragma once

#include "trading_model.h"
#include "view_list_model.h"

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/box.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/label.h>
#include <vector>

namespace trading {

class PositionsView {
public:
    explicit PositionsView(std::shared_ptr<TradingModel> model);

    PositionsView(const PositionsView&) = delete;
    PositionsView& operator=(const PositionsView&) = delete;

    [[nodiscard]] std::shared_ptr<nk::Widget> widget() const { return root_; }

    void refresh();

private:
    void update_empty_state();

    std::shared_ptr<TradingModel> model_;
    std::shared_ptr<ViewListModel> position_rows_;
    std::shared_ptr<nk::DataTable> positions_table_;
    std::shared_ptr<nk::Label> empty_hint_;
    std::shared_ptr<nk::Box> root_;
    std::size_t seen_row_count_ = 0;
};
} // namespace trading
