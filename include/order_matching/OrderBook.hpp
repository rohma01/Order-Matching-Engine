#pragma once

#include "MemoryPool.hpp"
#include "Order.hpp"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace order_matching {

template <std::size_t MaxOrders = 65'536, std::size_t MaxPriceLevels = 4'096>
class alignas(64) OrderBook {
    struct alignas(64) Limit {
        Price price{0};
        Quantity total_quantity{0};
        Order* head{nullptr};
        Order* tail{nullptr};
        Side side{Side::Buy};
    };

public:
    using TradeHandler = std::function<void(const Trade&)>;

    OrderBook() {
        orders_.reserve(MaxOrders);
        bids_.reserve(MaxPriceLevels);
        asks_.reserve(MaxPriceLevels);
    }

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    [[nodiscard]] bool addOrder(const OrderRequest& request, TradeHandler on_trade = {}) {
        if (request.id == 0 || request.quantity == 0 || orders_.contains(request.id)) {
            return false;
        }

        Quantity remaining = matchOrders(request, on_trade);
        if (remaining == 0 || request.type == OrderType::Market ||
            request.time_in_force == TimeInForce::ImmediateOrCancel) {
            return true;
        }

        Order* order = order_pool_.acquire();
        if (order == nullptr) {
            return false;
        }
        *order = Order{request.id, request.price, request.quantity, remaining,
                       request.side, request.type, request.time_in_force, nullptr, nullptr};

        Limit* limit = find_or_create_limit(request.side, request.price);
        if (limit == nullptr) {
            order_pool_.release(order);
            return false;
        }
        append(*limit, *order);
        orders_.emplace(order->id, order);
        return true;
    }

    [[nodiscard]] bool cancelOrder(OrderId id) noexcept {
        const auto iterator = orders_.find(id);
        if (iterator == orders_.end()) {
            return false;
        }

        Order* order = iterator->second;
        Limit* limit = find_limit(order->side, order->price);
        unlink(*limit, *order);
        orders_.erase(iterator);
        order_pool_.release(order);
        if (limit->head == nullptr) {
            remove_limit(*limit);
        }
        return true;
    }

    [[nodiscard]] bool add(const OrderRequest& request, TradeHandler on_trade = {}) {
        return addOrder(request, std::move(on_trade));
    }

    [[nodiscard]] bool cancel(OrderId id) noexcept { return cancelOrder(id); }

    [[nodiscard]] Quantity matchOrders(const OrderRequest& request,
                                        const TradeHandler& on_trade = {}) {
        Quantity remaining = request.quantity;
        match(request, remaining, on_trade);
        return remaining;
    }

    [[nodiscard]] const Order* find(OrderId id) const noexcept {
        const auto iterator = orders_.find(id);
        return iterator == orders_.end() ? nullptr : iterator->second;
    }

    [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }
    [[nodiscard]] std::size_t bid_level_count() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_level_count() const noexcept { return asks_.size(); }

private:
    using LevelMap = std::unordered_map<Price, Limit*>;

    [[nodiscard]] Limit* find_limit(Side side, Price price) const noexcept {
        const LevelMap& levels = side == Side::Buy ? bids_ : asks_;
        const auto iterator = levels.find(price);
        return iterator == levels.end() ? nullptr : iterator->second;
    }

    [[nodiscard]] Limit* find_or_create_limit(Side side, Price price) {
        LevelMap& levels = side == Side::Buy ? bids_ : asks_;
        if (const auto iterator = levels.find(price); iterator != levels.end()) {
            return iterator->second;
        }

        Limit* limit = limit_pool_.acquire();
        if (limit == nullptr) {
            return nullptr;
        }
        *limit = Limit{price, 0, nullptr, nullptr, side};
        try {
            levels.emplace(price, limit);
        } catch (...) {
            limit_pool_.release(limit);
            return nullptr;
        }
        return limit;
    }

    void append(Limit& limit, Order& order) noexcept {
        order.previous = limit.tail;
        order.next = nullptr;
        if (limit.tail != nullptr) {
            limit.tail->next = &order;
        } else {
            limit.head = &order;
        }
        limit.tail = &order;
        limit.total_quantity += order.remaining;
    }

    void unlink(Limit& limit, Order& order) noexcept {
        if (order.previous != nullptr) {
            order.previous->next = order.next;
        } else {
            limit.head = order.next;
        }
        if (order.next != nullptr) {
            order.next->previous = order.previous;
        } else {
            limit.tail = order.previous;
        }
        limit.total_quantity -= order.remaining;
        order.previous = nullptr;
        order.next = nullptr;
    }

    void match(const OrderRequest& request, Quantity& remaining,
               const TradeHandler& on_trade) {
        LevelMap& opposite = request.side == Side::Buy ? asks_ : bids_;
        while (remaining != 0) {
            Limit* best = best_crossing_limit(request, opposite);
            if (best == nullptr) {
                break;
            }

            Order* maker = best->head;
            while (maker != nullptr && remaining != 0) {
                const Quantity traded = remaining < maker->remaining ? remaining : maker->remaining;
                remaining -= traded;
                maker->remaining -= traded;
                best->total_quantity -= traded;
                if (on_trade) {
                    const bool maker_is_buy = maker->side == Side::Buy;
                    on_trade(Trade{maker_is_buy ? maker->id : request.id,
                                   maker_is_buy ? request.id : maker->id,
                                   traded, maker->price});
                }
                if (maker->remaining == 0) {
                    Order* completed = maker;
                    maker = maker->next;
                    unlink_completed(*best, *completed);
                    orders_.erase(completed->id);
                    order_pool_.release(completed);
                } else {
                    maker = maker->next;
                }
            }
            if (best->head == nullptr) {
                remove_limit(*best);
            }
        }
    }

    [[nodiscard]] Limit* best_crossing_limit(const OrderRequest& request,
                                              const LevelMap& opposite) const noexcept {
        Limit* best = nullptr;
        for (const auto& [price, limit] : opposite) {
            const bool crosses = request.side == Side::Buy ? price <= request.price
                                                            : price >= request.price;
            if (request.type == OrderType::Market || crosses) {
                if (best == nullptr ||
                    (request.side == Side::Buy ? price < best->price : price > best->price)) {
                    best = limit;
                }
            }
        }
        return best;
    }

    void unlink_completed(Limit& limit, Order& order) noexcept {
        if (order.next != nullptr) {
            order.next->previous = nullptr;
        }
        limit.head = order.next;
        if (limit.tail == &order) {
            limit.tail = nullptr;
        }
        order.previous = nullptr;
        order.next = nullptr;
    }

    void remove_limit(Limit& limit) noexcept {
        LevelMap& levels = limit.side == Side::Buy ? bids_ : asks_;
        levels.erase(limit.price);
        limit_pool_.release(&limit);
    }

    ObjectPool<Order, MaxOrders> order_pool_;
    ObjectPool<Limit, MaxPriceLevels> limit_pool_;
    std::unordered_map<OrderId, Order*> orders_;
    LevelMap bids_;
    LevelMap asks_;
};

} // namespace order_matching