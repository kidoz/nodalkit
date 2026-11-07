#include "orders_view.h"

#include "trading_model.h"
#include "view_list_model.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/label.h>
#include <optional>
#include <string>
#include <utility>

namespace trading {
OrdersView::OrdersView(std::shared_ptr<TradingModel> model) : model_(std::move(model)) {
    order_rows_ = std::make_shared<ViewListModel>(
        [model = model_] { return model->orders().size(); },
        [model = model_](std::size_t row) { return std::to_string(model->orders()[row].id); });
    auto orders_page = nk::Box::create(nk::Orientation::Vertical, 7.0F);
    auto orders_toolbar = nk::Box::create(nk::Orientation::Horizontal, 10.0F);
    orders_toolbar->append(nk::Label::create("Working and completed orders"));
    cancel_button_ = nk::Button::create("Cancel selected working order");
    orders_toolbar->append(cancel_button_);
    orders_page->append(orders_toolbar);

    orders_table_ = nk::DataTable::create();
    orders_table_->set_model(order_rows_);
    order_selection_ = std::make_shared<nk::SelectionModel>();
    orders_table_->set_selection_model(order_selection_);
    orders_table_->set_columns({
        {.id = "id",
         .title = "ID",
         .width = 58.0F,
         .text = [model =
                      model_](const nk::AbstractListModel&,
                              std::size_t row) { return std::to_string(model->orders()[row].id); }},
        {.id = "symbol",
         .title = "Symbol",
         .width = 72.0F,
         .text = [model = model_](const nk::AbstractListModel&,
                                  std::size_t row) { return model->orders()[row].symbol; }},
        {.id = "side",
         .title = "Side",
         .width = 58.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return std::string(trading::order_side_name(model->orders()[row].side));
             }},
        {.id = "type",
         .title = "Type",
         .width = 66.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return std::string(trading::order_type_name(model->orders()[row].type));
             }},
        {.id = "quantity",
         .title = "Qty",
         .width = 64.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return std::to_string(model->orders()[row].quantity);
             }},
        {.id = "price",
         .title = "Limit",
         .width = 86.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 const auto& order = model->orders()[row];
                 return order.type == OrderType::Limit ? trading::format_price(order.limit_price)
                                                       : std::string("—");
             }},
        {.id = "fill",
         .title = "Avg fill",
         .width = 88.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 const auto& order = model->orders()[row];
                 return order.status == OrderStatus::Filled
                            ? trading::format_price(order.average_fill_price)
                            : std::string("—");
             }},
        {.id = "status",
         .title = "Status",
         .width = 92.0F,
         .text =
             [model = model_](const nk::AbstractListModel&, std::size_t row) {
                 return std::string(trading::order_status_name(model->orders()[row].status));
             },
         .expand = 1.0F},
    });
    orders_table_->set_vertical_stretch(1);
    orders_table_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    orders_table_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    orders_page->append(orders_table_);

    root_ = orders_page;
    connections_.emplace_back(
        cancel_button_->on_clicked().connect([this] { cancel_requested_.emit(); }));
}

void OrdersView::refresh() {
    if (seen_row_count_ != model_->orders().size()) {
        order_rows_->rows_changed();
        seen_row_count_ = model_->orders().size();
    } else if (seen_revision_ != model_->order_revision()) {
        order_rows_->data_changed();
    }
    seen_revision_ = model_->order_revision();
}

std::optional<std::uint64_t> OrdersView::selected_order_id() const {
    const auto row = order_selection_->current_row();
    if (row >= model_->orders().size()) {
        return std::nullopt;
    }
    return model_->orders()[row].id;
}
} // namespace trading
