#pragma once

#include <cstdint>

namespace order_matching {

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };
enum class TimeInForce : std::uint8_t { GoodTillCancel, ImmediateOrCancel };

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;

struct alignas(64) Order {
    OrderId id{0};
    Price price{0};
    Quantity quantity{0};
    Quantity remaining{0};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce time_in_force{TimeInForce::GoodTillCancel};
    Order* previous{nullptr};
    Order* next{nullptr};
};

struct alignas(64) OrderRequest {
    OrderId id{0};
    Price price{0};
    Quantity quantity{0};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce time_in_force{TimeInForce::GoodTillCancel};
};

struct Trade {
    OrderId buyOrderId{0};
    OrderId sellOrderId{0};
    Quantity matchedQuantity{0};
    Price executedPrice{0};
};

} // namespace order_matching