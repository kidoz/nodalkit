#include "trading_model.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace trading {

namespace {

std::uint32_t next_random(std::uint32_t& state) {
    state = (state * 1664525U) + 1013904223U;
    return state;
}

Instrument make_instrument(std::string symbol,
                           std::string name,
                           Price starting_price,
                           Price previous_close,
                           std::uint32_t seed) {
    Instrument instrument{
        .symbol = std::move(symbol),
        .name = std::move(name),
        .previous_close = previous_close,
        .last = 0,
        .bid = 0,
        .ask = 0,
        .volume = 0,
        .candles = {},
    };

    Price price = starting_price;
    for (int index = 0; index < 64; ++index) {
        const Price open = price;
        const Price movement = static_cast<Price>(next_random(seed) % 61U) - 30;
        const Price close = std::max<Price>(100, open + movement);
        const Price high = std::max(open, close) + static_cast<Price>(next_random(seed) % 17U);
        const Price low =
            std::max<Price>(1, std::min(open, close) - static_cast<Price>(next_random(seed) % 17U));
        const std::uint64_t volume = 1000 + (next_random(seed) % 9000U);
        instrument.candles.push_back({open, high, low, close, volume});
        price = close;
    }

    instrument.last = price;
    instrument.bid = price - 2;
    instrument.ask = price + 2;
    instrument.volume = 250000 + (next_random(seed) % 500000U);
    return instrument;
}

std::uint64_t price_magnitude(Price price) {
    if (price >= 0) {
        return static_cast<std::uint64_t>(price);
    }
    return static_cast<std::uint64_t>(-(price + 1)) + 1;
}

} // namespace

TradingModel::TradingModel() {
    instruments_.push_back(make_instrument("NODA", "Nodal Systems", 18450, 18320, 0x11111111U));
    instruments_.push_back(make_instrument("ORBT", "Orbit Networks", 9275, 9340, 0x22222222U));
    instruments_.push_back(make_instrument("LUMN", "Lumenworks", 14620, 14480, 0x33333333U));
    instruments_.push_back(
        make_instrument("ATLS", "Atlas Semiconductor", 23890, 24010, 0x44444444U));
}

std::span<const Instrument> TradingModel::instruments() const {
    return instruments_;
}

std::span<const Order> TradingModel::orders() const {
    return orders_;
}

std::span<const Position> TradingModel::positions() const {
    return positions_;
}

std::size_t TradingModel::selected_index() const {
    return selected_index_;
}

const Instrument* TradingModel::selected_instrument() const {
    return selected_index_ < instruments_.size() ? &instruments_[selected_index_] : nullptr;
}

const Instrument* TradingModel::find_instrument(std::string_view symbol) const {
    const auto iterator = std::ranges::find(instruments_, symbol, &Instrument::symbol);
    return iterator == instruments_.end() ? nullptr : &*iterator;
}

Instrument* TradingModel::find_instrument_mut(std::string_view symbol) {
    const auto iterator = std::ranges::find(instruments_, symbol, &Instrument::symbol);
    return iterator == instruments_.end() ? nullptr : &*iterator;
}

const Position* TradingModel::find_position(std::string_view symbol) const {
    const auto iterator = std::ranges::find(positions_, symbol, &Position::symbol);
    return iterator == positions_.end() ? nullptr : &*iterator;
}

bool TradingModel::select_instrument(std::size_t index) {
    if (index >= instruments_.size()) {
        return false;
    }
    selected_index_ = index;
    return true;
}

void TradingModel::advance_market() {
    ++market_revision_;
    for (auto& instrument : instruments_) {
        const Price movement = static_cast<Price>(next_random(random_state_) % 15U) - 7;
        instrument.last = std::max<Price>(100, instrument.last + movement);
        instrument.bid = instrument.last - 2;
        instrument.ask = instrument.last + 2;
        const auto trade_volume = 10 + (next_random(random_state_) % 190U);
        instrument.volume += trade_volume;

        auto& candle = instrument.candles.back();
        candle.close = instrument.last;
        candle.high = std::max(candle.high, instrument.last);
        candle.low = std::min(candle.low, instrument.last);
        candle.volume += trade_volume;

        if (market_revision_ % 8 == 0) {
            instrument.candles.push_back(
                {instrument.last, instrument.last, instrument.last, instrument.last, 0});
            if (instrument.candles.size() > 96) {
                instrument.candles.erase(instrument.candles.begin());
            }
        }
    }

    bool order_changed = false;
    for (auto& order : orders_) {
        if (order.status != OrderStatus::Working || order.type != OrderType::Limit) {
            continue;
        }
        const auto* instrument = find_instrument(order.symbol);
        if (instrument == nullptr) {
            continue;
        }
        if (order.side == OrderSide::Buy && order.limit_price >= instrument->ask) {
            fill_order(order, instrument->ask);
            order_changed = true;
        } else if (order.side == OrderSide::Sell && order.limit_price <= instrument->bid) {
            fill_order(order, instrument->bid);
            order_changed = true;
        }
    }
    if (order_changed) {
        ++order_revision_;
    }
}

nk::Result<std::uint64_t> TradingModel::submit_order(const OrderRequest& request) {
    auto* instrument = find_instrument_mut(request.symbol);
    if (instrument == nullptr) {
        return nk::Unexpected<std::string>("Unknown instrument: " + request.symbol);
    }
    if (request.quantity <= 0 || request.quantity > 1000000) {
        return nk::Unexpected<std::string>("Quantity must be between 1 and 1,000,000.");
    }
    if (request.type == OrderType::Limit && request.limit_price <= 0) {
        return nk::Unexpected<std::string>("Limit price must be greater than zero.");
    }

    Order order{
        .id = next_order_id_++,
        .symbol = request.symbol,
        .side = request.side,
        .type = request.type,
        .quantity = request.quantity,
        .limit_price = request.type == OrderType::Limit ? request.limit_price : 0,
    };
    orders_.push_back(order);
    auto& submitted = orders_.back();

    if (submitted.type == OrderType::Market) {
        fill_order(submitted, submitted.side == OrderSide::Buy ? instrument->ask : instrument->bid);
    } else if (submitted.side == OrderSide::Buy && submitted.limit_price >= instrument->ask) {
        fill_order(submitted, instrument->ask);
    } else if (submitted.side == OrderSide::Sell && submitted.limit_price <= instrument->bid) {
        fill_order(submitted, instrument->bid);
    }

    ++order_revision_;
    return submitted.id;
}

bool TradingModel::cancel_order(std::uint64_t id) {
    const auto iterator = std::ranges::find(orders_, id, &Order::id);
    if (iterator == orders_.end() || iterator->status != OrderStatus::Working) {
        return false;
    }
    iterator->status = OrderStatus::Cancelled;
    ++order_revision_;
    return true;
}

std::uint64_t TradingModel::market_revision() const {
    return market_revision_;
}

std::uint64_t TradingModel::order_revision() const {
    return order_revision_;
}

std::uint64_t TradingModel::position_revision() const {
    return position_revision_;
}

void TradingModel::fill_order(Order& order, Price execution_price) {
    order.average_fill_price = execution_price;
    order.status = OrderStatus::Filled;

    const int delta = order.side == OrderSide::Buy ? order.quantity : -order.quantity;
    auto iterator = std::ranges::find(positions_, order.symbol, &Position::symbol);
    if (iterator == positions_.end()) {
        positions_.push_back({order.symbol, delta, execution_price});
        ++position_revision_;
        return;
    }

    auto& position = *iterator;
    const int previous_quantity = position.quantity;
    const int new_quantity = previous_quantity + delta;
    if (previous_quantity == 0 || ((previous_quantity > 0) == (delta > 0))) {
        const auto previous_absolute = static_cast<Price>(std::abs(previous_quantity));
        const auto added_absolute = static_cast<Price>(std::abs(delta));
        position.average_price =
            ((position.average_price * previous_absolute) + (execution_price * added_absolute)) /
            (previous_absolute + added_absolute);
    } else if (new_quantity == 0) {
        position.average_price = 0;
    } else if ((new_quantity > 0) != (previous_quantity > 0)) {
        position.average_price = execution_price;
    }
    position.quantity = new_quantity;
    ++position_revision_;
}

std::string format_price(Price price) {
    const auto magnitude = price_magnitude(price);
    std::ostringstream stream;
    stream << (price < 0 ? "-$" : "$") << (magnitude / 100U) << '.' << std::setw(2)
           << std::setfill('0') << (magnitude % 100U);
    return stream.str();
}

std::string format_signed_price(Price price) {
    return price >= 0 ? "+" + format_price(price) : format_price(price);
}

std::string_view order_side_name(OrderSide side) {
    return side == OrderSide::Buy ? "Buy" : "Sell";
}

std::string_view order_type_name(OrderType type) {
    return type == OrderType::Market ? "Market" : "Limit";
}

std::string_view order_status_name(OrderStatus status) {
    switch (status) {
    case OrderStatus::Working:
        return "Working";
    case OrderStatus::Filled:
        return "Filled";
    case OrderStatus::Cancelled:
        return "Cancelled";
    }
    return "Unknown";
}

} // namespace trading
