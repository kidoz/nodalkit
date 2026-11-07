#pragma once

#include <cstddef>
#include <cstdint>
#include <nk/foundation/result.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

using Price = std::int64_t;

struct Candle {
    Price open = 0;
    Price high = 0;
    Price low = 0;
    Price close = 0;
    std::uint64_t volume = 0;
};

struct Instrument {
    std::string symbol;
    std::string name;
    Price previous_close = 0;
    Price last = 0;
    Price bid = 0;
    Price ask = 0;
    std::uint64_t volume = 0;
    std::vector<Candle> candles;
};

enum class OrderSide {
    Buy,
    Sell,
};

enum class OrderType {
    Market,
    Limit,
};

enum class OrderStatus {
    Working,
    Filled,
    Cancelled,
};

struct Order {
    std::uint64_t id = 0;
    std::string symbol;
    OrderSide side = OrderSide::Buy;
    OrderType type = OrderType::Market;
    int quantity = 0;
    Price limit_price = 0;
    Price average_fill_price = 0;
    OrderStatus status = OrderStatus::Working;
};

struct Position {
    std::string symbol;
    int quantity = 0;
    Price average_price = 0;
};

struct OrderRequest {
    std::string symbol;
    OrderSide side = OrderSide::Buy;
    OrderType type = OrderType::Market;
    int quantity = 0;
    Price limit_price = 0;
};

class TradingModel {
public:
    TradingModel();

    [[nodiscard]] std::span<const Instrument> instruments() const;
    [[nodiscard]] std::span<const Order> orders() const;
    [[nodiscard]] std::span<const Position> positions() const;
    [[nodiscard]] std::size_t selected_index() const;
    [[nodiscard]] const Instrument* selected_instrument() const;
    [[nodiscard]] const Instrument* find_instrument(std::string_view symbol) const;
    [[nodiscard]] const Position* find_position(std::string_view symbol) const;

    bool select_instrument(std::size_t index);
    void advance_market();
    [[nodiscard]] nk::Result<std::uint64_t> submit_order(const OrderRequest& request);
    bool cancel_order(std::uint64_t id);

    [[nodiscard]] std::uint64_t market_revision() const;
    [[nodiscard]] std::uint64_t order_revision() const;
    [[nodiscard]] std::uint64_t position_revision() const;

private:
    Instrument* find_instrument_mut(std::string_view symbol);
    void fill_order(Order& order, Price execution_price);

    std::vector<Instrument> instruments_;
    std::vector<Order> orders_;
    std::vector<Position> positions_;
    std::size_t selected_index_ = 0;
    std::uint64_t next_order_id_ = 1001;
    std::uint32_t random_state_ = 0x4E4B3031U;
    std::uint64_t market_revision_ = 0;
    std::uint64_t order_revision_ = 0;
    std::uint64_t position_revision_ = 0;
};

[[nodiscard]] std::string format_price(Price price);
[[nodiscard]] std::string format_signed_price(Price price);
[[nodiscard]] std::string_view order_side_name(OrderSide side);
[[nodiscard]] std::string_view order_type_name(OrderType type);
[[nodiscard]] std::string_view order_status_name(OrderStatus status);

} // namespace trading
