#include "examples/trading_terminal/candlestick_chart.h"
#include "examples/trading_terminal/trading_model.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <string>

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
