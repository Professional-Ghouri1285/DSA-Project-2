// MatchingEngine.h - MINIMAL CHANGES ONLY
#pragma once

#include "Order.h"
#include "OrderBook.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h"
#include "../btree/BPlusTree.h" // ADDED
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <iostream>
#include <vector>
#include <string>

class PersistentOrderBook;
class UserManager;

class MatchingEngine
{
private:
    std::recursive_mutex engineMutex;
    BufferManager *bufferManager;
    WriteAheadLog *wal;
    UserManager *userManager;
    std::atomic<int64_t> nextOrderId{1000};
    std::atomic<int64_t> nextTradeId{10000};

    // ADDED: Persistent storage for IDs
    BPlusTree metadataTree; // Store nextOrderId, nextTradeId, stats

    // Trade history by symbol
    std::unordered_map<std::string, std::vector<Trade>> tradeHistory;
    std::unordered_map<std::string, std::unique_ptr<PersistentOrderBook>> orderBooks;

public:
    MatchingEngine(BufferManager *bm, WriteAheadLog *wal = nullptr, UserManager *um = nullptr);
    ~MatchingEngine(); // ADDED destructor

    // ADDED: Persistence methods
    void loadPersistentData();
    void savePersistentData();

    // Order management
    int64_t generateOrderId();
    int64_t generateTradeId();
    bool placeOrder(Order order);
    bool cancelOrder(const std::string &symbol, int64_t orderId);
    PersistentOrderBook *getOrderBook(const std::string &symbol);
    std::vector<std::string> getSymbols();
    void addSymbol(const std::string &symbol);
    void printAllBooks();
    void flushAll();

    // Trade processing
    std::vector<Trade> processTrades(const std::vector<Trade> &trades);

    // Market data - NO CONST
    void printMarketSummary();

    // Trade history - NO CONST
    std::vector<Trade> getTradeHistory(const std::string &symbol, int limit = 50);

    // Statistics
    struct Statistics
    {
        int totalOrders = 0;
        int totalTrades = 0;
        double totalVolume = 0;
    } stats;

private:
    void logTrade(const Trade &trade);

    // ADDED: Metadata keys
    enum MetadataKeys
    {
        METADATA_NEXT_ORDER_ID = 1,
        METADATA_NEXT_TRADE_ID = 2,
        METADATA_TOTAL_ORDERS = 3,
        METADATA_TOTAL_TRADES = 4,
        METADATA_TOTAL_VOLUME = 5
    };

    // ADDED: Helper to flush metadata tree through BufferManager
    void flushMetadataTree();
};