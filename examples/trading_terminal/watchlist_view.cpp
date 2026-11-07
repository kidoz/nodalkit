#include "watchlist_view.h"

#include "trading_model.h"
#include "view_list_model.h"

#include <cstddef>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/box.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/label.h>
#include <string>
#include <string_view>
#include <utility>

namespace trading {
namespace {
std::shared_ptr<nk::Box> make_panel(std::string_view title, std::shared_ptr<nk::Widget> child) {
    auto panel = nk::Box::create(nk::Orientation::Vertical, 7.0F);
    panel->append(nk::Label::create(std::string(title)));
    child->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    child->set_vertical_size_policy(nk::SizePolicy::Expanding);
    child->set_vertical_stretch(1);
    panel->append(std::move(child));
    return panel;
}

} // namespace

WatchlistView::WatchlistView(std::shared_ptr<TradingModel> model) : model_(std::move(model)) {
    instrument_rows_ = std::make_shared<ViewListModel>(
        [model = model_] { return model->instruments().size(); },
        [model = model_](std::size_t row) { return model->instruments()[row].symbol; });
    watchlist_table_ = nk::DataTable::create();
    watchlist_table_->set_model(instrument_rows_);
    watchlist_selection_ = std::make_shared<nk::SelectionModel>();
    watchlist_table_->set_selection_model(watchlist_selection_);
    watchlist_table_->set_row_height(29.0F);
    watchlist_table_->set_columns({
        {.id = "symbol",
         .title = "Symbol",
         .width = 72.0F,
         .text = [model = model_](const nk::AbstractListModel&,
                                  std::size_t row) { return model->instruments()[row].symbol; },
         .expand = 1.0F},
        {.id = "last",
         .title = "Last",
         .width = 82.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return trading::format_price(model->instruments()[row].last);
             }},
        {.id = "change",
         .title = "Change",
         .width = 76.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 const auto& item = model->instruments()[row];
                 return format_percent(item.last, item.previous_close);
             }},
        {.id = "bid",
         .title = "Bid",
         .width = 80.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return trading::format_price(model->instruments()[row].bid);
             }},
        {.id = "ask",
         .title = "Ask",
         .width = 80.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return trading::format_price(model->instruments()[row].ask);
             }},
    });
    watchlist_selection_->set_current_row(0);
    watchlist_selection_->select(0);
    connections_.emplace_back(
        watchlist_selection_->on_current_changed().connect([this](std::size_t row) {
            if (!syncing_selection_ && row < model_->instruments().size()) {
                instrument_selected_.emit(row);
            }
        }));
    root_ = make_panel("Watchlist  •  SIM", watchlist_table_);
}

void WatchlistView::refresh() {
    instrument_rows_->data_changed();
}

void WatchlistView::set_selected_instrument(std::size_t index) {
    syncing_selection_ = true;
    watchlist_selection_->set_current_row(index);
    watchlist_selection_->select(index);
    syncing_selection_ = false;
}
} // namespace trading
