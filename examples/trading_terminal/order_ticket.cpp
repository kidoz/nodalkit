#include "order_ticket.h"

#include "trading_model.h"

#include <cmath>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/role.h>
#include <nk/foundation/types.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/label.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/spin_button.h>

namespace trading {

OrderTicket::OrderTicket() {
    auto panel = nk::Box::create(nk::Orientation::Vertical, 7.0F);
    panel->append(nk::Label::create("Order ticket"));
    ticket_instrument_ = nk::Label::create();
    panel->append(ticket_instrument_);

    panel->append(nk::Label::create("Side"));
    side_selector_ = nk::SegmentedControl::create();
    side_selector_->set_segments({"Buy", "Sell"});
    side_selector_->set_selected_index(0);
    panel->append(side_selector_);

    panel->append(nk::Label::create("Order type"));
    type_selector_ = nk::ComboBox::create();
    type_selector_->set_items({"Market", "Limit"});
    type_selector_->set_selected_index(0);
    panel->append(type_selector_);

    panel->append(nk::Label::create("Quantity"));
    quantity_input_ = nk::SpinButton::create();
    quantity_input_->set_range(1.0, 1000000.0);
    quantity_input_->set_step(1.0);
    quantity_input_->set_precision(0);
    quantity_input_->set_value(10.0);
    panel->append(quantity_input_);

    price_label_ = nk::Label::create("Limit price  •  USD");
    panel->append(price_label_);
    price_input_ = nk::SpinButton::create();
    price_input_->set_range(0.01, 1000000.0);
    price_input_->set_step(0.01);
    price_input_->set_precision(2);
    price_input_->set_sensitive(false);
    panel->append(price_input_);

    notional_label_ = nk::Label::create();
    notional_label_->set_wrapping(true);
    panel->append(notional_label_);

    submit_button_ = nk::Button::create();
    panel->append(submit_button_);

    feedback_ = nk::Label::create("Ready. Orders remain inside this simulation.");
    feedback_->set_wrapping(true);
    feedback_->ensure_accessible().set_role(nk::AccessibleRole::Status);
    feedback_->ensure_accessible().set_name("Order entry status");
    panel->append(feedback_);

    connections_.emplace_back(
        side_selector_->on_selection_changed().connect([this](int) { update_ticket_controls(); }));
    connections_.emplace_back(
        type_selector_->on_selection_changed().connect([this](int) { update_ticket_controls(); }));
    connections_.emplace_back(
        quantity_input_->on_value_changed().connect([this](double) { update_notional(); }));
    connections_.emplace_back(
        price_input_->on_value_changed().connect([this](double) { update_notional(); }));
    update_ticket_controls();
    root_ = panel;
    connections_.emplace_back(
        submit_button_->on_clicked().connect([this] { submit_requested_.emit(request()); }));
}

void OrderTicket::update_ticket_controls() {
    const bool limit = type_selector_->selected_index() == 1;
    price_input_->set_sensitive(limit);
    price_label_->set_text(limit ? "Limit price  •  USD"
                                 : "Limit price  •  not used for market orders");
    update_submit_label();
    update_notional();
}

void OrderTicket::update_submit_label() {
    const std::string side = side_selector_->selected_index() == 1 ? "Sell" : "Buy";
    submit_button_->set_label(side + " " + symbol_ + "  •  Simulated");
}

void OrderTicket::update_notional() {
    const Price unit_price = type_selector_->selected_index() == 1
                                 ? static_cast<Price>(std::llround(price_input_->value() * 100.0))
                                 : (side_selector_->selected_index() == 1 ? bid_ : ask_);
    const auto quantity = static_cast<Price>(quantity_input_->value());
    notional_label_->set_text("Estimated notional: " +
                              trading::format_price(unit_price * quantity));
    update_submit_label();
}

void OrderTicket::set_instrument(const Instrument& instrument, bool reset_limit_price) {
    symbol_ = instrument.symbol;
    bid_ = instrument.bid;
    ask_ = instrument.ask;
    ticket_instrument_->set_text(instrument.symbol + "  •  " + format_price(instrument.last));
    if (reset_limit_price) {
        set_limit_price(instrument.ask);
    }
    update_notional();
}

void OrderTicket::set_side(OrderSide side) {
    side_selector_->set_selected_index(side == OrderSide::Sell ? 1 : 0);
}

void OrderTicket::set_order_type(OrderType type) {
    type_selector_->set_selected_index(type == OrderType::Limit ? 1 : 0);
}

void OrderTicket::set_quantity(int quantity) {
    quantity_input_->set_value(quantity);
}

void OrderTicket::set_limit_price(Price price) {
    price_input_->set_value(static_cast<double>(price) / 100.0);
}

void OrderTicket::set_feedback(const std::string& message) {
    feedback_->set_text(message);
}

OrderRequest OrderTicket::request() const {
    return {
        .symbol = symbol_,
        .side = side_selector_->selected_index() == 1 ? OrderSide::Sell : OrderSide::Buy,
        .type = type_selector_->selected_index() == 1 ? OrderType::Limit : OrderType::Market,
        .quantity = static_cast<int>(quantity_input_->value()),
        .limit_price = static_cast<Price>(std::llround(price_input_->value() * 100.0)),
    };
}
} // namespace trading
