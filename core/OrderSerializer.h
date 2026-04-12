// OrderSerializer.h
#pragma once

#include "Order.h"
#include <cstring>

class OrderSerializer
{
public:
    static constexpr size_t ORDER_SIZE = 128; // Fixed size for Order
    static constexpr size_t TRADE_SIZE = 72;  // Fixed size for Trade

    static void serializeOrder(const Order &order, char *buffer)
    {
        size_t offset = 0;

        // Serialize fixed-size fields
        memcpy(buffer + offset, &order.order_id, sizeof(order.order_id));
        offset += sizeof(order.order_id);

        memcpy(buffer + offset, &order.user_id, sizeof(order.user_id));
        offset += sizeof(order.user_id);

        // Symbol (fixed size 16)
        size_t symbol_len = strlen(order.symbol);
        size_t copy_len = (symbol_len < 15) ? symbol_len : 15;
        memcpy(buffer + offset, order.symbol, copy_len);
        buffer[offset + copy_len] = '\0';
        offset += 16;

        int32_t type = static_cast<int32_t>(order.type);
        memcpy(buffer + offset, &type, sizeof(type));
        offset += sizeof(type);

        int32_t side = static_cast<int32_t>(order.side);
        memcpy(buffer + offset, &side, sizeof(side));
        offset += sizeof(side);

        int32_t status = static_cast<int32_t>(order.status);
        memcpy(buffer + offset, &status, sizeof(status));
        offset += sizeof(status);

        memcpy(buffer + offset, &order.price, sizeof(order.price));
        offset += sizeof(order.price);

        memcpy(buffer + offset, &order.stop_price, sizeof(order.stop_price));
        offset += sizeof(order.stop_price);

        memcpy(buffer + offset, &order.quantity, sizeof(order.quantity));
        offset += sizeof(order.quantity);

        memcpy(buffer + offset, &order.filled_quantity, sizeof(order.filled_quantity));
        offset += sizeof(order.filled_quantity);

        memcpy(buffer + offset, &order.filled_price, sizeof(order.filled_price));
        offset += sizeof(order.filled_price);

        memcpy(buffer + offset, &order.timestamp, sizeof(order.timestamp));
        offset += sizeof(order.timestamp);

        memcpy(buffer + offset, &order.expiry_time, sizeof(order.expiry_time));
        offset += sizeof(order.expiry_time);

        // Fill remaining bytes with zeros for padding
        size_t remaining = ORDER_SIZE - offset;
        if (remaining > 0)
        {
            memset(buffer + offset, 0, remaining);
        }
    }

    static Order deserializeOrder(const char *buffer)
    {
        Order order;
        size_t offset = 0;

        memcpy(&order.order_id, buffer + offset, sizeof(order.order_id));
        offset += sizeof(order.order_id);

        memcpy(&order.user_id, buffer + offset, sizeof(order.user_id));
        offset += sizeof(order.user_id);

        // Symbol
        strncpy(order.symbol, buffer + offset, 16);
        order.symbol[15] = '\0';
        offset += 16;

        int32_t type;
        memcpy(&type, buffer + offset, sizeof(type));
        order.type = static_cast<OrderType>(type);
        offset += sizeof(type);

        int32_t side;
        memcpy(&side, buffer + offset, sizeof(side));
        order.side = static_cast<OrderSide>(side);
        offset += sizeof(side);

        int32_t status;
        memcpy(&status, buffer + offset, sizeof(status));
        order.status = static_cast<OrderStatus>(status);
        offset += sizeof(status);

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

        return order;
    }

    static void serializeTrade(const Trade &trade, char *buffer)
    {
        size_t offset = 0;

        memcpy(buffer + offset, &trade.trade_id, sizeof(trade.trade_id));
        offset += sizeof(trade.trade_id);

        memcpy(buffer + offset, &trade.timestamp, sizeof(trade.timestamp));
        offset += sizeof(trade.timestamp);

        // Symbol (fixed size 16)
        size_t symbol_len = strlen(trade.symbol);
        size_t copy_len = (symbol_len < 15) ? symbol_len : 15;
        memcpy(buffer + offset, trade.symbol, copy_len);
        buffer[offset + copy_len] = '\0';
        offset += 16;

        memcpy(buffer + offset, &trade.price, sizeof(trade.price));
        offset += sizeof(trade.price);

        memcpy(buffer + offset, &trade.quantity, sizeof(trade.quantity));
        offset += sizeof(trade.quantity);

        memcpy(buffer + offset, &trade.buy_order_id, sizeof(trade.buy_order_id));
        offset += sizeof(trade.buy_order_id);

        memcpy(buffer + offset, &trade.sell_order_id, sizeof(trade.sell_order_id));
        offset += sizeof(trade.sell_order_id);

        memcpy(buffer + offset, &trade.buyer_id, sizeof(trade.buyer_id));
        offset += sizeof(trade.buyer_id);

        memcpy(buffer + offset, &trade.seller_id, sizeof(trade.seller_id));
        offset += sizeof(trade.seller_id);

        // Padding
        size_t remaining = TRADE_SIZE - offset;
        if (remaining > 0)
        {
            memset(buffer + offset, 0, remaining);
        }
    }

    static Trade deserializeTrade(const char *buffer)
    {
        Trade trade;
        size_t offset = 0;

        memcpy(&trade.trade_id, buffer + offset, sizeof(trade.trade_id));
        offset += sizeof(trade.trade_id);

        memcpy(&trade.timestamp, buffer + offset, sizeof(trade.timestamp));
        offset += sizeof(trade.timestamp);

        strncpy(trade.symbol, buffer + offset, 16);
        trade.symbol[15] = '\0';
        offset += 16;

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

    static int64_t createBidKey(double price)
    {
        // For bids: higher price should come first
        // Convert to integer with enough precision
        // Use price * 10000 for 4 decimal places
        // For descending order: use 1,000,000,000 - price
        return static_cast<int64_t>((1000000.0 - price) * 10000);
    }

    static int64_t createAskKey(double price)
    {
        // For asks: lower price should come first
        return static_cast<int64_t>(price * 10000);
    }
};