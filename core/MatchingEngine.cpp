// MatchingEngine.cpp - COMPLETE IMPLEMENTATION
#include "MatchingEngine.h"
#include "UserManager.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <algorithm>

// In MatchingEngine.cpp
MatchingEngine::MatchingEngine(BufferManager *bm, WriteAheadLog *wal, UserManager *um)
    : bufferManager(bm), wal(wal), userManager(um),
      metadataTree(bm, -1) // Initialize metadata B+Tree
{
    std::cout << "MatchingEngine initialized" << std::endl;
    std::cout << "DEBUG: UserManager pointer: " << (userManager ? "VALID" : "NULL") << std::endl;

    // Load persistent data
    loadPersistentData();

    // Initialize with some default symbols
    std::vector<std::string> default_symbols = {"AAPL", "GOOGL", "TSLA", "MSFT", "AMZN"};
    for (const auto &symbol : default_symbols)
    {

        // CRITICAL: Pass 'this' as the MatchingEngine parameter
        std::cout << "DEBUG: Creating OrderBook for " << symbol
                  << " with MatchingEngine: " << this << std::endl;
        orderBooks[symbol].reset(new PersistentOrderBook(symbol, bufferManager, wal, this));
    }
}

MatchingEngine::~MatchingEngine()
{
    // Save persistent data before destruction
    savePersistentData();
    flushAll();
    std::cout << "MatchingEngine shutdown complete" << std::endl;
}

// ADDED: Helper to flush metadata tree
void MatchingEngine::flushMetadataTree()
{
    // BPlusTree doesn't have flush(), but we can flush through BufferManager
    // The metadata will be flushed when BufferManager flushes
    if (bufferManager)
    {
        bufferManager->flushAll();
    }
}

// ADDED: Load persistent data
void MatchingEngine::loadPersistentData()
{
    try
    {
        // Load nextOrderId from metadata tree
        auto orderIdResult = metadataTree.search(METADATA_NEXT_ORDER_ID);
        if (orderIdResult.first)
        {
            nextOrderId = orderIdResult.second;
            std::cout << "Loaded nextOrderId from storage: " << nextOrderId << std::endl;
        }
        else
        {
            nextOrderId = 1000;
            std::cout << "Using default nextOrderId: " << nextOrderId << std::endl;
        }

        // Load nextTradeId from metadata tree
        auto tradeIdResult = metadataTree.search(METADATA_NEXT_TRADE_ID);
        if (tradeIdResult.first)
        {
            nextTradeId = tradeIdResult.second;
            std::cout << "Loaded nextTradeId from storage: " << nextTradeId << std::endl;
        }
        else
        {
            nextTradeId = 10000;
            std::cout << "Using default nextTradeId: " << nextTradeId << std::endl;
        }

        // Load statistics
        auto ordersResult = metadataTree.search(METADATA_TOTAL_ORDERS);
        if (ordersResult.first)
        {
            stats.totalOrders = static_cast<int>(ordersResult.second);
        }

        auto tradesResult = metadataTree.search(METADATA_TOTAL_TRADES);
        if (tradesResult.first)
        {
            stats.totalTrades = static_cast<int>(tradesResult.second);
        }

        auto volumeResult = metadataTree.search(METADATA_TOTAL_VOLUME);
        if (volumeResult.first)
        {
            stats.totalVolume = static_cast<double>(volumeResult.second) / 100.0; // Stored as cents
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading persistent data: " << e.what() << std::endl;
        // Keep defaults if loading fails
        nextOrderId = 1000;
        nextTradeId = 10000;
    }
}

// ADDED: Save persistent data
void MatchingEngine::savePersistentData()
{
    try
    {
        // Save current IDs
        metadataTree.insert(METADATA_NEXT_ORDER_ID, nextOrderId.load());
        metadataTree.insert(METADATA_NEXT_TRADE_ID, nextTradeId.load());

        // Save statistics (store volume as integer cents)
        metadataTree.insert(METADATA_TOTAL_ORDERS, stats.totalOrders);
        metadataTree.insert(METADATA_TOTAL_TRADES, stats.totalTrades);
        metadataTree.insert(METADATA_TOTAL_VOLUME, static_cast<int64_t>(stats.totalVolume * 100));

        // Flush through BufferManager
        flushMetadataTree();
        std::cout << "Saved persistent data to storage" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error saving persistent data: " << e.what() << std::endl;
    }
}

int64_t MatchingEngine::generateOrderId()
{
    int64_t id = nextOrderId.fetch_add(1);

    // Save to persistent storage
    metadataTree.insert(METADATA_NEXT_ORDER_ID, nextOrderId.load());

    stats.totalOrders++;
    return id;
}

int64_t MatchingEngine::generateTradeId()
{
    int64_t id = nextTradeId.fetch_add(1);

    // Save to persistent storage
    metadataTree.insert(METADATA_NEXT_TRADE_ID, nextTradeId.load());

    stats.totalTrades++;
    return id;
}

// ADDED BACK: This was missing!
void MatchingEngine::addSymbol(const std::string &symbol)
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);

    if (orderBooks.find(symbol) == orderBooks.end())
    {
        orderBooks[symbol].reset(new PersistentOrderBook(symbol, bufferManager, wal, this));
        std::cout << "Added symbol: " << symbol << std::endl;
    }
}

bool MatchingEngine::placeOrder(Order order)
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);

    // Validate order
    if (order.quantity <= 0 || order.price <= 0)
    {
        std::cerr << "Invalid order: quantity=" << order.quantity
                  << ", price=" << order.price << std::endl;
        return false;
    }

    // Generate order ID if not set
    if (order.order_id == 0)
    {
        order.order_id = generateOrderId();
    }

    std::string symbol = order.symbol;

    // Get or create OrderBook for symbol
    if (orderBooks.find(symbol) == orderBooks.end())
    {
        orderBooks[symbol].reset(new PersistentOrderBook(symbol, bufferManager, wal, this));
        std::cout << "Created new order book for symbol: " << symbol << std::endl;
    }

    // Add order to book
    bool added = orderBooks[symbol]->addOrder(order);

    if (!added)
    {
        std::cerr << "Failed to add order " << order.order_id
                  << " to book " << symbol << std::endl;
        return false;
    }

    stats.totalOrders++;

    // Trigger matching
    auto trades = orderBooks[symbol]->matchOrders();

    // Process trades (update portfolios, etc.)
    if (!trades.empty())
    {
        processTrades(trades);
    }

    return true;
}

bool MatchingEngine::cancelOrder(const std::string &symbol, int64_t orderId)
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);

    auto it = orderBooks.find(symbol);
    if (it == orderBooks.end())
    {
        std::cerr << "No order book for symbol: " << symbol << std::endl;
        return false;
    }

    return it->second->cancelOrder(orderId);
}

PersistentOrderBook *MatchingEngine::getOrderBook(const std::string &symbol)
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    auto it = orderBooks.find(symbol);
    return (it != orderBooks.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> MatchingEngine::getSymbols()
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    std::vector<std::string> symbols;
    for (const auto &pair : orderBooks)
    {
        symbols.push_back(pair.first);
    }
    return symbols;
}

std::vector<Trade> MatchingEngine::processTrades(const std::vector<Trade> &trades)
{
    std::cout << "DEBUG: ========== ENTERING processTrades() ==========" << std::endl;
    std::cout << "DEBUG: Received " << trades.size() << " trade(s)" << std::endl;
    std::cout << "DEBUG: userManager pointer: " << (userManager ? "VALID" : "NULL") << std::endl;

    std::vector<Trade> processed_trades;

    std::cout << "DEBUG: Starting to process trades..." << std::endl;

    for (size_t i = 0; i < trades.size(); i++)
    {
        std::cout << "DEBUG: Processing trade " << (i + 1) << "/" << trades.size() << std::endl;
        const auto &trade = trades[i];

        std::cout << "DEBUG: Creating processed trade..." << std::endl;
        Trade processed_trade = trade;

        std::cout << "DEBUG: Generating trade ID..." << std::endl;
        processed_trade.trade_id = generateTradeId();

        std::cout << "DEBUG: Setting timestamp..." << std::endl;
        processed_trade.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

        // Add to trade history
        {
            std::cout << "DEBUG: Acquiring engineMutex..." << std::endl;
            std::lock_guard<std::recursive_mutex> lock(engineMutex);
            std::cout << "DEBUG: Adding to trade history..." << std::endl;
            tradeHistory[processed_trade.symbol].push_back(processed_trade);
            std::cout << "DEBUG: Released engineMutex" << std::endl;
        }

        // Log the trade
        std::cout << "DEBUG: Logging trade..." << std::endl;
        logTrade(processed_trade);

        // CRITICAL: Update user portfolios via UserManager
        std::cout << "DEBUG: Checking userManager..." << std::endl;
        if (userManager)
        {
            std::cout << "DEBUG: Calling UserManager::executeTrade()..." << std::endl;
            std::cout << "DEBUG: Trade details - Buyer: " << processed_trade.buyer_id
                      << ", Seller: " << processed_trade.seller_id
                      << ", Symbol: " << processed_trade.symbol
                      << ", Quantity: " << processed_trade.quantity
                      << ", Price: " << processed_trade.price << std::endl;

            bool success = userManager->executeTrade(
                processed_trade.buyer_id,
                processed_trade.seller_id,
                processed_trade.symbol,
                processed_trade.quantity,
                processed_trade.price);

            std::cout << "DEBUG: UserManager::executeTrade() returned: "
                      << (success ? "true" : "false") << std::endl;
        }
        else
        {
            std::cerr << "ERROR: UserManager not connected to MatchingEngine! "
                      << "Trade will not be reflected in user accounts." << std::endl;
        }

        std::cout << "DEBUG: Adding to processed_trades..." << std::endl;
        processed_trades.push_back(processed_trade);

        // Update statistics
        {
            std::cout << "DEBUG: Updating statistics..." << std::endl;
            std::lock_guard<std::recursive_mutex> lock(engineMutex);
            stats.totalTrades++;
            stats.totalVolume += processed_trade.quantity * processed_trade.price;
        }

        std::cout << "TRADE EXECUTED: " << processed_trade.toString() << std::endl;
        std::cout << "DEBUG: Finished processing trade " << (i + 1) << "/" << trades.size() << std::endl;
    }

    std::cout << "DEBUG: ========== EXITING processTrades() ==========" << std::endl;
    return processed_trades;
}

void MatchingEngine::printAllBooks()
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    std::cout << "\n=== ALL ORDER BOOKS ===" << std::endl;
    for (const auto &pair : orderBooks)
    {
        std::cout << "\n--- " << pair.first << " ---" << std::endl;
        pair.second->printOrderBook(5);
    }
    std::cout << "========================\n"
              << std::endl;
}

void MatchingEngine::printMarketSummary()
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);

    std::cout << "\n=== MARKET SUMMARY ===" << std::endl;
    std::cout << "Symbols: " << orderBooks.size() << std::endl;
    std::cout << "Total Orders: " << stats.totalOrders << std::endl;
    std::cout << "Total Trades: " << stats.totalTrades << std::endl;
    std::cout << "Total Volume: $" << std::fixed << std::setprecision(2) << stats.totalVolume << std::endl;

    for (const auto &pair : orderBooks)
    {
        const std::string &symbol = pair.first;
        PersistentOrderBook *book = pair.second.get();

        if (book)
        {
            // Get ACTIVE orders (not just cache size)
            size_t activeOrders = book->getOrderCount();

            if (activeOrders > 0)
            {
                double bestBidPrice = book->getBestBidPrice();
                double bestAskPrice = book->getBestAskPrice();
                double spread = book->getSpread();

                std::cout << symbol << ": ";
                std::cout << activeOrders << " active orders, ";

                if (bestBidPrice > 0 && bestAskPrice > 0)
                {
                    std::cout << "Bid $" << std::fixed << std::setprecision(2) << bestBidPrice
                              << " | Ask $" << bestAskPrice
                              << " | Spread $" << spread;
                }
                else if (bestBidPrice > 0)
                {
                    std::cout << "Bid $" << bestBidPrice << " | No asks";
                }
                else if (bestAskPrice > 0)
                {
                    std::cout << "No bids | Ask $" << bestAskPrice;
                }
                else
                {
                    std::cout << "No active quotes";
                }
            }
            else
            {
                std::cout << symbol << ": No active orders";
            }

            auto it = tradeHistory.find(symbol);
            if (it != tradeHistory.end() && !it->second.empty())
            {
                std::cout << " | Trades today: " << it->second.size();
            }

            std::cout << std::endl;
        }
    }
    std::cout << "=====================\n"
              << std::endl;
}

std::vector<Trade> MatchingEngine::getTradeHistory(const std::string &symbol, int limit) // NO CONST
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);

    auto it = tradeHistory.find(symbol);
    if (it == tradeHistory.end())
    {
        return {};
    }

    const auto &trades = it->second;
    int startIdx = std::max(0, static_cast<int>(trades.size()) - limit);
    std::vector<Trade> result;

    for (int i = startIdx; i < trades.size(); i++)
    {
        result.push_back(trades[i]);
    }

    return result;
}

void MatchingEngine::flushAll()
{
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    for (auto &pair : orderBooks)
    {
        pair.second->flush();
    }
    // Also flush metadata through BufferManager
    if (bufferManager)
    {
        bufferManager->flushAll();
    }
    std::cout << "MatchingEngine flushed all books and metadata" << std::endl;
}

void MatchingEngine::logTrade(const Trade &trade)
{
    if (wal)
    {
        char tradeData[Trade::serializedSize()];
        trade.serialize(tradeData);

        int pageId = static_cast<int>(trade.trade_id % 1000000);
        wal->append(LOG_INSERT, pageId, tradeData, Trade::serializedSize());
    }
}