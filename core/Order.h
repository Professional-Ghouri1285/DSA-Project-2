// Add to Order.h
#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <chrono>
#include <vector>
#include <cstring> // Add for memcpy

using namespace std;

enum OrderType
{
    ORDER_MARKET = 1,
    ORDER_LIMIT = 2,
    ORDER_STOP = 3
};

enum OrderSide
{
    SIDE_BUY = 1,
    SIDE_SELL = 2
};

enum OrderStatus
{
    STATUS_PENDING = 0,
    STATUS_PARTIAL_FILL = 1,
    STATUS_FILLED = 2,
    STATUS_CANCELLED = 3,
    STATUS_REJECTED = 4
};

struct Order
{
    int64_t order_id;
    int32_t user_id;
    char symbol[16]; // Fixed size for serialization
    OrderType type;
    OrderSide side;
    OrderStatus status;

    double price;
    double stop_price;
    int quantity;
    int filled_quantity;
    double filled_price;

    int64_t timestamp;
    int64_t expiry_time;

    // B+Tree keys - for different indexing strategies
    double btree_price_key; // For price-time priority

    Order() : order_id(0), user_id(0), type(ORDER_LIMIT), side(SIDE_BUY),
              status(STATUS_PENDING), price(0.0), stop_price(0.0),
              quantity(0), filled_quantity(0), filled_price(0.0),
              timestamp(0), expiry_time(0), btree_price_key(0.0)
    {
        memset(symbol, 0, sizeof(symbol));
    }

    Order(const std::string &sym, int64_t id, int32_t uid,
          OrderSide s, OrderType t, double p, int qty)
        : order_id(id), user_id(uid), type(t), side(s),
          status(STATUS_PENDING), price(p), stop_price(0.0),
          quantity(qty), filled_quantity(0), filled_price(0.0),
          timestamp(std::chrono::system_clock::now().time_since_epoch().count()),
          expiry_time(0), btree_price_key(p)
    {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }

    // Helper methods
    bool isBuy() const { return side == SIDE_BUY; }
    bool isSell() const { return side == SIDE_SELL; }
    bool isLimit() const { return type == ORDER_LIMIT; }
    bool isMarket() const { return type == ORDER_MARKET; }
    bool isStop() const { return type == ORDER_STOP; }

    int getRemainingQuantity() const { return quantity - filled_quantity; }
    bool isFilled() const { return filled_quantity >= quantity; }
    bool isActive() const { return status == STATUS_PENDING || status == STATUS_PARTIAL_FILL; }
    bool isCancelled() const { return status == STATUS_CANCELLED; }
    bool isRejected() const { return status == STATUS_REJECTED; }

    void fill(int fill_qty, double fill_price)
    {
        if (fill_qty <= 0 || fill_price <= 0)
            return;

        double total_value = (filled_quantity * filled_price) + (fill_qty * fill_price);
        filled_quantity += fill_qty;
        filled_price = total_value / filled_quantity;

        if (filled_quantity >= quantity)
        {
            status = STATUS_FILLED;
        }
        else if (filled_quantity > 0)
        {
            status = STATUS_PARTIAL_FILL;
        }
    }

    void cancel() { status = STATUS_CANCELLED; }
    void reject(const string &reason = "")
    {
        status = STATUS_REJECTED;
        if (!reason.empty())
        {
            cerr << "Order " << order_id << " rejected: " << reason << "\n";
        }
    }

    // Serialization for B+Tree storage
    static size_t serializedSize() { return 128; } // Fixed size

    void serialize(char *buffer) const
    {
        size_t offset = 0;
        memcpy(buffer + offset, &order_id, sizeof(order_id));
        offset += sizeof(order_id);

        memcpy(buffer + offset, &user_id, sizeof(user_id));
        offset += sizeof(user_id);

        memcpy(buffer + offset, symbol, sizeof(symbol));
        offset += sizeof(symbol);

        memcpy(buffer + offset, &type, sizeof(type));
        offset += sizeof(type);

        memcpy(buffer + offset, &side, sizeof(side));
        offset += sizeof(side);

        memcpy(buffer + offset, &status, sizeof(status));
        offset += sizeof(status);

        memcpy(buffer + offset, &price, sizeof(price));
        offset += sizeof(price);

        memcpy(buffer + offset, &stop_price, sizeof(stop_price));
        offset += sizeof(stop_price);

        memcpy(buffer + offset, &quantity, sizeof(quantity));
        offset += sizeof(quantity);

        memcpy(buffer + offset, &filled_quantity, sizeof(filled_quantity));
        offset += sizeof(filled_quantity);

        memcpy(buffer + offset, &filled_price, sizeof(filled_price));
        offset += sizeof(filled_price);

        memcpy(buffer + offset, &timestamp, sizeof(timestamp));
        offset += sizeof(timestamp);

        memcpy(buffer + offset, &expiry_time, sizeof(expiry_time));
        offset += sizeof(expiry_time);

        memcpy(buffer + offset, &btree_price_key, sizeof(btree_price_key));
    }

    static Order deserialize(const char *buffer)
    {
        Order order;
        size_t offset = 0;

        memcpy(&order.order_id, buffer + offset, sizeof(order.order_id));
        offset += sizeof(order.order_id);

        memcpy(&order.user_id, buffer + offset, sizeof(order.user_id));
        offset += sizeof(order.user_id);

        memcpy(order.symbol, buffer + offset, sizeof(order.symbol));
        offset += sizeof(order.symbol);

        memcpy(&order.type, buffer + offset, sizeof(order.type));
        offset += sizeof(order.type);

        memcpy(&order.side, buffer + offset, sizeof(order.side));
        offset += sizeof(order.side);

        memcpy(&order.status, buffer + offset, sizeof(order.status));
        offset += sizeof(order.status);

        memcpy(&order.price, buffer + offset, sizeof(order.price));
        offset += sizeof(order.price);

        memcpy(&order.stop_price, buffer + offset, sizeof(order.stop_price));
        offset += sizeof(order.stop_price);

        memcpy(&order.quantity, buffer + offset, sizeof(order.quantity));
        offset += sizeof(order.quantity);

        memcpy(&order.filled_quantity, buffer + offset, sizeof(order.filled_quantity));
        offset += sizeof(order.filled_quantity);

        memcpy(&order.filled_price, buffer + offset, sizeof(order.filled_price));
        offset += sizeof(order.filled_price);

        memcpy(&order.timestamp, buffer + offset, sizeof(order.timestamp));
        offset += sizeof(order.timestamp);

        memcpy(&order.expiry_time, buffer + offset, sizeof(order.expiry_time));
        offset += sizeof(order.expiry_time);

        memcpy(&order.btree_price_key, buffer + offset, sizeof(order.btree_price_key));

        return order;
    }

    string toString() const
    {
        string side_str = isBuy() ? "BUY" : "SELL";
        string type_str;
        switch (type)
        {
        case ORDER_MARKET:
            type_str = "MARKET";
            break;
        case ORDER_LIMIT:
            type_str = "LIMIT";
            break;
        case ORDER_STOP:
            type_str = "STOP";
            break;
        default:
            type_str = "UNKNOWN";
        }

        string status_str;
        switch (status)
        {
        case STATUS_PENDING:
            status_str = "PENDING";
            break;
        case STATUS_PARTIAL_FILL:
            status_str = "PARTIAL_FILL";
            break;
        case STATUS_FILLED:
            status_str = "FILLED";
            break;
        case STATUS_CANCELLED:
            status_str = "CANCELLED";
            break;
        case STATUS_REJECTED:
            status_str = "REJECTED";
            break;
        default:
            status_str = "UNKNOWN";
        }

        return "Order[" + to_string(order_id) + "] " + side_str + " " +
               to_string(quantity) + " " + symbol + " @" +
               to_string(price) + " (" + type_str + ", " + status_str + ")";
    }
};

struct Trade
{
    int64_t trade_id;
    int64_t timestamp;
    char symbol[16];
    double price;
    int quantity;
    int64_t buy_order_id;
    int64_t sell_order_id;
    int buyer_id;
    int seller_id;

    Trade() : trade_id(0), timestamp(0), price(0.0), quantity(0),
              buy_order_id(0), sell_order_id(0), buyer_id(0), seller_id(0)
    {
        memset(symbol, 0, sizeof(symbol));
    }

    string toString() const
    {
        return "Trade[" + to_string(trade_id) + "] " + symbol + " " +
               to_string(quantity) + " @ $" + to_string(price) +
               " (Buyer: " + to_string(buyer_id) +
               ", Seller: " + to_string(seller_id) + ")";
    }

    // Serialization for B+Tree storage
    static size_t serializedSize() { return 72; }

    void serialize(char *buffer) const
    {
        size_t offset = 0;
        memcpy(buffer + offset, &trade_id, sizeof(trade_id));
        offset += sizeof(trade_id);
        memcpy(buffer + offset, &timestamp, sizeof(timestamp));
        offset += sizeof(timestamp);
        memcpy(buffer + offset, symbol, sizeof(symbol));
        offset += sizeof(symbol);
        memcpy(buffer + offset, &price, sizeof(price));
        offset += sizeof(price);
        memcpy(buffer + offset, &quantity, sizeof(quantity));
        offset += sizeof(quantity);
        memcpy(buffer + offset, &buy_order_id, sizeof(buy_order_id));
        offset += sizeof(buy_order_id);
        memcpy(buffer + offset, &sell_order_id, sizeof(sell_order_id));
        offset += sizeof(sell_order_id);
        memcpy(buffer + offset, &buyer_id, sizeof(buyer_id));
        offset += sizeof(buyer_id);
        memcpy(buffer + offset, &seller_id, sizeof(seller_id));
    }

    static Trade deserialize(const char *buffer)
    {
        Trade trade;
        size_t offset = 0;

        memcpy(&trade.trade_id, buffer + offset, sizeof(trade.trade_id));
        offset += sizeof(trade.trade_id);
        memcpy(&trade.timestamp, buffer + offset, sizeof(trade.timestamp));
        offset += sizeof(trade.timestamp);
        memcpy(trade.symbol, buffer + offset, sizeof(trade.symbol));
        offset += sizeof(trade.symbol);
        memcpy(&trade.price, buffer + offset, sizeof(trade.price));
        offset += sizeof(trade.price);
        memcpy(&trade.quantity, buffer + offset, sizeof(trade.quantity));
        offset += sizeof(trade.quantity);
        memcpy(&trade.buy_order_id, buffer + offset, sizeof(trade.buy_order_id));
        offset += sizeof(trade.buy_order_id);
        memcpy(&trade.sell_order_id, buffer + offset, sizeof(trade.sell_order_id));
        offset += sizeof(trade.sell_order_id);
        memcpy(&trade.buyer_id, buffer + offset, sizeof(trade.buyer_id));
        offset += sizeof(trade.buyer_id);
        memcpy(&trade.seller_id, buffer + offset, sizeof(trade.seller_id));

        return trade;
    }
};