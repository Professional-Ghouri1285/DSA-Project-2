#pragma once

#include "Order.h"
#include "../btree/BPlusTree.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h"
#include "../core/MatchingEngine.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <iostream>
#include <iomanip>
#include <functional>

class MatchingEngine;

class PersistentOrderBook
{
private:
    std::string symbol;
    BufferManager *bufferManager;
    WriteAheadLog *wal;
    MatchingEngine *matchingEngine;

    // B+Tree for bids (buy orders) - stores order IDs sorted by price-time
    BPlusTree bidsTree; // FIXED: Changed from bidTree to bidsTree

    // B+Tree for asks (sell orders) - stores order IDs sorted by price-time
    BPlusTree asksTree; // FIXED: Changed from askTree to asksTree

    // B+Tree for order lookup by ID
    BPlusTree orderIdIndex;

    // In-memory order cache (optional, for performance)
    std::unordered_map<int64_t, Order> orderCache;

    // Reverse lookup: orderId -> price-time key
    std::unordered_map<int64_t, int64_t> orderIdToKey; // ADDED: For updateOrderInTree

    mutable std::mutex orderbookMutex;

    // Price-time priority key generation
    static const int64_t PRICE_SCALE = 1000000;   // 6 decimal places
    static const int64_t TIME_SCALE = 1000000000; // nanoseconds

    // Key generation functions
    int64_t createBidKey(double price, int64_t timestamp, int64_t orderId) const;
    int64_t createAskKey(double price, int64_t timestamp, int64_t orderId) const;

    // NEW: Helper function for updateOrderInTree
    int64_t generatePriceTimeKey(double price, int64_t timestamp, bool isBuy) const;

    // Order storage
    int storeOrderData(const Order &order);
    Order loadOrderData(int64_t dataPage);
    bool deleteOrderData(int64_t orderId);

    // Tree operations
    bool insertOrderInTree(const Order &order);
    bool removeOrderFromTree(int64_t orderId);
    Order loadOrderFromTree(int64_t orderId);

    // Matching helpers
    std::vector<int64_t> getBestBidKeys(int count = 10);
    std::vector<int64_t> getBestAskKeys(int count = 10);
    Order loadOrderByKey(int64_t key, bool isBuy);

public:
    PersistentOrderBook(const std::string &sym, BufferManager *bm, WriteAheadLog *wal = nullptr);
    PersistentOrderBook(const std::string &sym, BufferManager *bm,
                        WriteAheadLog *wal, MatchingEngine *me = nullptr)
        : symbol(sym), bufferManager(bm), wal(wal), matchingEngine(me),
          bidsTree(bm, -1), asksTree(bm, -1), orderIdIndex(bm, -1)
    {
        std::cout << "Created PersistentOrderBook for " << symbol << std::endl;
    }

    void setMatchingEngine(MatchingEngine *me)
    {
        matchingEngine = me;
    }
    ~PersistentOrderBook();

    // Core operations - NO CONST
    bool addOrder(const Order &order);
    bool cancelOrder(int64_t orderId);
    Order getOrder(int64_t orderId);
    std::vector<Order> getBestBids(int count = 5);
    std::vector<Order> getBestAsks(int count = 5);
    double getBestBidPrice();
    double getBestAskPrice();
    double getSpread();

    // Matching engine
    std::vector<Trade> matchOrders();

    // Utility
    void flush();
    void printOrderBook(int depth = 5);
    size_t getOrderCount();

    // NEW: Update order in tree after partial fill
    void updateOrderInTree(int64_t orderId, const Order &updatedOrder);

    // Statistics
    struct Stats
    {
        int totalOrders = 0;
        int totalTrades = 0;
        double totalVolume = 0;
    } stats;
    double decodePriceFromKey(int64_t key, bool isBid) const;
};