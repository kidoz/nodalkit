#include "positions_view.h"

#include "trading_model.h"
#include "view_list_model.h"

#include <cstddef>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/model/abstract_list_model.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/box.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/label.h>
#include <string>
#include <utility>

namespace trading {
PositionsView::PositionsView(std::shared_ptr<TradingModel> model) : model_(std::move(model)) {
    position_rows_ = std::make_shared<ViewListModel>(
        [model = model_] { return model->positions().size(); },
        [model = model_](std::size_t row) { return model->positions()[row].symbol; });
    auto positions_page = nk::Box::create(nk::Orientation::Vertical, 7.0F);
    auto account_summary = nk::Label::create("Demo account  •  USD  •  Simulated P&L");
    positions_page->append(account_summary);
    positions_table_ = nk::DataTable::create();
    positions_table_->set_model(position_rows_);
    positions_table_->set_columns({
        {.id = "symbol",
         .title = "Symbol",
         .width = 100.0F,
         .text = [model = model_](const nk::AbstractListModel&,
                                  std::size_t row) { return model->positions()[row].symbol; },
         .expand = 1.0F},
        {.id = "quantity",
         .title = "Quantity",
         .width = 110.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return std::to_string(model->positions()[row].quantity);
             }},
        {.id = "average",
         .title = "Average price",
         .width = 130.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return trading::format_price(model->positions()[row].average_price);
             }},
        {.id = "last",
         .title = "Last",
         .width = 120.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 const auto& position = model->positions()[row];
                 const auto* instrument = model->find_instrument(position.symbol);
                 return instrument != nullptr ? trading::format_price(instrument->last)
                                              : std::string("—");
             }},
        {.id = "pnl",
         .title = "Unrealized P&L",
         .width = 150.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 const auto& position = model->positions()[row];
                 const auto* instrument = model->find_instrument(position.symbol);
                 if (instrument == nullptr) {
                     return std::string("—");
                 }
                 const Price pnl = (instrument->last - position.average_price) *
                                   static_cast<Price>(position.quantity);
                 return trading::format_signed_price(pnl);
             }},
    });
    positions_table_->set_vertical_stretch(1);
    positions_table_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    positions_table_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    positions_page->append(positions_table_);

    root_ = positions_page;
}

void PositionsView::refresh() {
    if (seen_row_count_ != model_->positions().size()) {
        position_rows_->rows_changed();
        seen_row_count_ = model_->positions().size();
    } else {
        position_rows_->data_changed();
    }
}
} // namespace trading
