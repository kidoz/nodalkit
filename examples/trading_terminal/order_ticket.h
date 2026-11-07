#pragma once

#include "trading_model.h"

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/label.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/spin_button.h>
#include <vector>

namespace trading {

// Application composite. The caller owns model commands and submission results.
class OrderTicket {
public:
    OrderTicket();

    OrderTicket(const OrderTicket&) = delete;
    OrderTicket& operator=(const OrderTicket&) = delete;

    [[nodiscard]] std::shared_ptr<nk::Widget> widget() const { return root_; }

    void set_instrument(const Instrument& instrument, bool reset_limit_price);
    void set_side(OrderSide side);
    void set_order_type(OrderType type);
    void set_quantity(int quantity);
    void set_limit_price(Price price);
    void set_feedback(const std::string& message);
    [[nodiscard]] OrderRequest request() const;

    nk::Signal<OrderRequest>& on_submit_requested() { return submit_requested_; }

private:
    void update_ticket_controls();
    void update_submit_label();
    void update_notional();
    std::string symbol_;
    Price bid_ = 0;
    Price ask_ = 0;
    std::shared_ptr<nk::Box> root_;
    std::shared_ptr<nk::Label> ticket_instrument_;
    std::shared_ptr<nk::SegmentedControl> side_selector_;
    std::shared_ptr<nk::ComboBox> type_selector_;
    std::shared_ptr<nk::SpinButton> quantity_input_;
    std::shared_ptr<nk::SpinButton> price_input_;
    std::shared_ptr<nk::Label> price_label_;
    std::shared_ptr<nk::Label> notional_label_;
    std::shared_ptr<nk::Button> submit_button_;
    std::shared_ptr<nk::Label> feedback_;
    nk::Signal<OrderRequest> submit_requested_;
    std::vector<nk::ScopedConnection> connections_;
};
} // namespace trading
