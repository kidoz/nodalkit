#include "examples/trading_terminal/candlestick_chart.h"
#include "examples/trading_terminal/order_ticket.h"
#include "examples/trading_terminal/orders_view.h"
#include "examples/trading_terminal/trading_model.h"
#include "examples/trading_terminal/watchlist_view.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <nk/accessibility/accessible.h>
#include <nk/foundation/signal.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/label.h>
#include <nk/widgets/spin_button.h>
#include <optional>
#include <string>
#include <vector>

TEST_CASE("Trading model seeds deterministic instruments and advances candles",
          "[trading_terminal]") {
    trading::TradingModel model;
    REQUIRE(model.instruments().size() == 4);
    REQUIRE(model.selected_index() == 0);
    REQUIRE(model.selected_instrument()->symbol == "NODA");
    REQUIRE(model.selected_instrument()->candles.size() == 64);

    const auto initial_last = model.selected_instrument()->last;
    const auto initial_candle_close = model.selected_instrument()->candles.back().close;
    model.advance_market();

    const auto* instrument = model.selected_instrument();
    REQUIRE(model.market_revision() == 1);
    REQUIRE(instrument->last != initial_last);
    REQUIRE(instrument->candles.back().close == instrument->last);
    REQUIRE(instrument->candles.back().close != initial_candle_close);
    REQUIRE(instrument->candles.back().high >= instrument->last);
    REQUIRE(instrument->candles.back().low <= instrument->last);
    REQUIRE(instrument->ask == instrument->last + 2);
    REQUIRE(instrument->bid == instrument->last - 2);

    trading::TradingModel identical;
    identical.advance_market();
    REQUIRE(identical.selected_instrument()->last == instrument->last);

    REQUIRE(model.select_instrument(2));
    REQUIRE(model.selected_instrument()->symbol == "LUMN");
    REQUIRE_FALSE(model.select_instrument(99));
    REQUIRE(model.selected_instrument()->symbol == "LUMN");
}

TEST_CASE("Trading model validates orders without changing the blotter", "[trading_terminal]") {
    trading::TradingModel model;
    REQUIRE_FALSE(model.submit_order(
        {"MISSING", trading::OrderSide::Buy, trading::OrderType::Market, 10, 0}));
    REQUIRE_FALSE(
        model.submit_order({"NODA", trading::OrderSide::Buy, trading::OrderType::Market, 0, 0}));
    REQUIRE_FALSE(model.submit_order(
        {"NODA", trading::OrderSide::Buy, trading::OrderType::Market, 1000001, 0}));
    REQUIRE_FALSE(
        model.submit_order({"NODA", trading::OrderSide::Buy, trading::OrderType::Limit, 10, 0}));
    REQUIRE(model.orders().empty());
    REQUIRE(model.order_revision() == 0);
}

TEST_CASE("Market orders fill and position averages use execution prices", "[trading_terminal]") {
    trading::TradingModel model;
    const auto expected_first_fill = model.selected_instrument()->ask;
    auto first =
        model.submit_order({"NODA", trading::OrderSide::Buy, trading::OrderType::Market, 10, 0});
    REQUIRE(first);
    REQUIRE(first.value() == 1001);
    REQUIRE(model.orders().back().status == trading::OrderStatus::Filled);
    REQUIRE(model.orders().back().average_fill_price == expected_first_fill);
    REQUIRE(model.find_position("NODA")->quantity == 10);
    REQUIRE(model.find_position("NODA")->average_price == expected_first_fill);

    const auto expected_second_fill = model.selected_instrument()->ask;
    auto second =
        model.submit_order({"NODA", trading::OrderSide::Buy, trading::OrderType::Market, 20, 0});
    REQUIRE(second);
    const auto expected_average = ((expected_first_fill * 10) + (expected_second_fill * 20)) / 30;
    REQUIRE(model.find_position("NODA")->quantity == 30);
    REQUIRE(model.find_position("NODA")->average_price == expected_average);

    auto reduce =
        model.submit_order({"NODA", trading::OrderSide::Sell, trading::OrderType::Market, 10, 0});
    REQUIRE(reduce);
    REQUIRE(model.find_position("NODA")->quantity == 20);
    REQUIRE(model.find_position("NODA")->average_price == expected_average);

    auto flip =
        model.submit_order({"NODA", trading::OrderSide::Sell, trading::OrderType::Market, 25, 0});
    REQUIRE(flip);
    REQUIRE(model.find_position("NODA")->quantity == -5);
    REQUIRE(model.find_position("NODA")->average_price == model.orders().back().average_fill_price);
    REQUIRE_FALSE(model.cancel_order(first.value()));
}

TEST_CASE("Working limit orders can fill later or be cancelled", "[trading_terminal]") {
    trading::TradingModel model;
    const auto limit = model.selected_instrument()->ask - 1;
    auto submitted =
        model.submit_order({"NODA", trading::OrderSide::Buy, trading::OrderType::Limit, 15, limit});
    REQUIRE(submitted);
    REQUIRE(model.orders().back().status == trading::OrderStatus::Working);
    REQUIRE(model.find_position("NODA") == nullptr);

    for (int attempt = 0;
         attempt < 1000 && model.orders().back().status == trading::OrderStatus::Working;
         ++attempt) {
        model.advance_market();
    }
    REQUIRE(model.orders().back().status == trading::OrderStatus::Filled);
    REQUIRE(model.orders().back().average_fill_price <= limit);
    REQUIRE(model.find_position("NODA")->quantity == 15);

    const auto unmarketable_sell_limit = model.find_instrument("ORBT")->bid + 100000;
    auto working = model.submit_order(
        {"ORBT", trading::OrderSide::Sell, trading::OrderType::Limit, 5, unmarketable_sell_limit});
    REQUIRE(working);
    REQUIRE(model.orders().back().status == trading::OrderStatus::Working);
    REQUIRE(model.cancel_order(working.value()));
    REQUIRE(model.orders().back().status == trading::OrderStatus::Cancelled);
    REQUIRE_FALSE(model.cancel_order(working.value()));
}

TEST_CASE("Trading price formatting preserves cents and signs", "[trading_terminal]") {
    REQUIRE(trading::format_price(18450) == "$184.50");
    REQUIRE(trading::format_price(5) == "$0.05");
    REQUIRE(trading::format_price(-1205) == "-$12.05");
    REQUIRE(trading::format_signed_price(1205) == "+$12.05");
    REQUIRE(trading::format_signed_price(-1205) == "-$12.05");
}

TEST_CASE("Candlestick chart supports zoom and pointer or keyboard inspection",
          "[trading_terminal]") {
    trading::TradingModel model;
    auto chart = trading::CandlestickChart::create();
    chart->set_data(model.selected_instrument()->symbol, model.selected_instrument()->candles);
    chart->allocate({0.0F, 0.0F, 800.0F, 500.0F});

    REQUIRE(chart->visible_candle_count() == 48);
    chart->zoom_in();
    REQUIRE(chart->visible_candle_count() == 40);
    chart->zoom_out();
    REQUIRE(chart->visible_candle_count() == 48);

    REQUIRE(
        chart->handle_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 100.0F, .y = 100.0F}));
    REQUIRE(chart->cursor_index().has_value());
    REQUIRE(chart->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End}));
    REQUIRE(chart->cursor_index().value() == model.selected_instrument()->candles.size() - 1);
    REQUIRE(chart->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left}));
    REQUIRE(chart->cursor_index().value() == model.selected_instrument()->candles.size() - 2);
    REQUIRE(chart->accessible()->value().find("Open") != std::string::npos);
    REQUIRE(
        chart->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape}));
    REQUIRE_FALSE(chart->cursor_index().has_value());

    chart->reset_view();
    REQUIRE(chart->visible_candle_count() == 48);
}

namespace {

template <typename T>
T* find_widget(
    nk::Widget& root, std::function<bool(const T&)> matches = [](const T&) { return true; }) {
    if (auto* widget = dynamic_cast<T*>(&root); widget != nullptr && matches(*widget)) {
        return widget;
    }
    for (const auto& child : root.children()) {
        if (auto* found = find_widget<T>(*child, matches)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Order ticket preserves edits across quotes and emits requests without submitting",
          "[trading_terminal]") {
    trading::TradingModel model;
    trading::OrderTicket ticket;
    ticket.set_instrument(*model.selected_instrument(), true);
    ticket.set_side(trading::OrderSide::Sell);
    ticket.set_order_type(trading::OrderType::Limit);
    ticket.set_quantity(17);
    ticket.set_limit_price(12345);

    model.advance_market();
    ticket.set_instrument(*model.selected_instrument(), false);
    const auto request = ticket.request();
    REQUIRE(request.symbol == "NODA");
    REQUIRE(request.side == trading::OrderSide::Sell);
    REQUIRE(request.type == trading::OrderType::Limit);
    REQUIRE(request.quantity == 17);
    REQUIRE(request.limit_price == 12345);

    auto* price = find_widget<nk::SpinButton>(
        *ticket.widget(), [](const auto& input) { return input.precision() == 2; });
    REQUIRE(price != nullptr);
    REQUIRE(price->is_sensitive());
    auto* notional = find_widget<nk::Label>(*ticket.widget(), [](const auto& label) {
        return label.text().starts_with("Estimated notional:");
    });
    REQUIRE(notional != nullptr);
    REQUIRE(notional->text() == "Estimated notional: $2098.65");

    std::optional<trading::OrderRequest> emitted;
    nk::ScopedConnection connection(ticket.on_submit_requested().connect(
        [&](const trading::OrderRequest& value) { emitted = value; }));
    auto* submit = find_widget<nk::Button>(
        *ticket.widget(), [](const auto& button) { return button.label().ends_with("Simulated"); });
    REQUIRE(submit != nullptr);
    REQUIRE(
        submit->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(emitted.has_value());
    REQUIRE(emitted->limit_price == request.limit_price);
    REQUIRE(emitted->symbol == request.symbol);
    REQUIRE(model.orders().empty());

    REQUIRE(model.select_instrument(2));
    ticket.set_instrument(*model.selected_instrument(), true);
    REQUIRE(ticket.request().symbol == "LUMN");
    REQUIRE(ticket.request().limit_price == model.selected_instrument()->ask);
    REQUIRE(ticket.request().quantity == 17);
    ticket.set_order_type(trading::OrderType::Market);
    REQUIRE_FALSE(price->is_sensitive());
}

TEST_CASE("Orders view retains source order identity across sorting and refresh",
          "[trading_terminal]") {
    auto model = std::make_shared<trading::TradingModel>();
    const auto first =
        model->submit_order({"NODA", trading::OrderSide::Buy, trading::OrderType::Limit, 10, 1});
    const auto second = model->submit_order(
        {"NODA", trading::OrderSide::Sell, trading::OrderType::Limit, 10, 1000000});
    REQUIRE(first);
    REQUIRE(second);
    trading::OrdersView orders(model);
    REQUIRE_FALSE(orders.selected_order_id().has_value());
    auto* table = find_widget<nk::DataTable>(*orders.widget());
    REQUIRE(table != nullptr);
    table->sort_by_column(0, nk::DataTableSortDirection::Descending);
    REQUIRE(table->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home}));
    REQUIRE(orders.selected_order_id() == second.value());

    std::optional<std::uint64_t> requested_id;
    nk::ScopedConnection connection(
        orders.on_cancel_requested().connect([&] { requested_id = orders.selected_order_id(); }));
    auto* cancel = find_widget<nk::Button>(*orders.widget());
    REQUIRE(cancel != nullptr);
    REQUIRE(
        cancel->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(requested_id == second.value());
    REQUIRE(model->orders()[1].status == trading::OrderStatus::Working);
    REQUIRE(model->cancel_order(*requested_id));
    orders.refresh();
    REQUIRE(orders.selected_order_id() == second.value());
    REQUIRE(table->columns().back().text(*table->model(), 1) == "Cancelled");
}

TEST_CASE("Watchlist selection synchronization does not emit recursive requests",
          "[trading_terminal]") {
    auto model = std::make_shared<trading::TradingModel>();
    trading::WatchlistView watchlist(model);
    std::vector<std::size_t> requested;
    nk::ScopedConnection connection(watchlist.on_instrument_selected().connect(
        [&](std::size_t index) { requested.push_back(index); }));
    watchlist.set_selected_instrument(2);
    REQUIRE(requested.empty());
    auto* table = find_widget<nk::DataTable>(*watchlist.widget());
    REQUIRE(table != nullptr);
    table->selection_model()->set_current_row(1);
    REQUIRE(requested == std::vector<std::size_t>{1});
    REQUIRE(model->selected_index() == 0);
    model->advance_market();
    watchlist.refresh();
    REQUIRE(table->selection_model()->current_row() == 1);
    REQUIRE(requested.size() == 1);
}
