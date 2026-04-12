// Portfolio.h
#pragma once
#include <cstdint>
#include <string>
#include <cstring>

struct PortfolioHolding
{
    int32_t userId;
    char symbol[16];
    int32_t quantity;
    double averagePrice;
    double marketValue; // Could be calculated

    PortfolioHolding() : userId(0), quantity(0), averagePrice(0.0), marketValue(0.0)
    {
        memset(symbol, 0, sizeof(symbol));
    }

    PortfolioHolding(int uid, const std::string &sym, int qty, double price)
        : userId(uid), quantity(qty), averagePrice(price), marketValue(qty * price)
    {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }

    static size_t serializedSize() { return 48; }

    void serialize(char *buffer) const
    {
        size_t offset = 0;
        memcpy(buffer + offset, &userId, sizeof(userId));
        offset += sizeof(userId);
        memcpy(buffer + offset, symbol, sizeof(symbol));
        offset += sizeof(symbol);
        memcpy(buffer + offset, &quantity, sizeof(quantity));
        offset += sizeof(quantity);
        memcpy(buffer + offset, &averagePrice, sizeof(averagePrice));
        offset += sizeof(averagePrice);
        memcpy(buffer + offset, &marketValue, sizeof(marketValue));
    }

    static PortfolioHolding deserialize(const char *buffer)
    {
        PortfolioHolding holding;
        size_t offset = 0;

        memcpy(&holding.userId, buffer + offset, sizeof(holding.userId));
        offset += sizeof(holding.userId);
        memcpy(holding.symbol, buffer + offset, sizeof(holding.symbol));
        offset += sizeof(holding.symbol);
        memcpy(&holding.quantity, buffer + offset, sizeof(holding.quantity));
        offset += sizeof(holding.quantity);
        memcpy(&holding.averagePrice, buffer + offset, sizeof(holding.averagePrice));
        offset += sizeof(holding.averagePrice);
        memcpy(&holding.marketValue, buffer + offset, sizeof(holding.marketValue));

        return holding;
    }
};