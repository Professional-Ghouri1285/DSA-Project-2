#include "OrderBook.h"
#include <climits>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

PersistentOrderBook::PersistentOrderBook(const std::string &sym, BufferManager *bm, WriteAheadLog *wal)
    : symbol(sym), bufferManager(bm), wal(wal),
      bidsTree(bm, -1), asksTree(bm, -1), orderIdIndex(bm, -1)
{
    std::cout << "Created PersistentOrderBook for " << symbol << std::endl;
}

PersistentOrderBook::~PersistentOrderBook()
{
    flush();
}

// Create price-time keys
int64_t PersistentOrderBook::createBidKey(double price, int64_t timestamp, int64_t orderId) const
{
    // For bids: higher price first, then earlier timestamp
    int64_t scaledPrice = static_cast<int64_t>(price * 10000); // 4 decimal places
    int64_t maxPrice = 1000000;                                // Max price $100.00
    int64_t invertedPrice = maxPrice - scaledPrice;

    return (invertedPrice << 44) | ((timestamp & 0xFFFFFFFF) << 12) | (orderId & 0xFFF);
}

int64_t PersistentOrderBook::createAskKey(double price, int64_t timestamp, int64_t orderId) const
{
    // For asks: lower price first, then earlier timestamp
    int64_t scaledPrice = static_cast<int64_t>(price * 10000);
    return (scaledPrice << 44) | ((timestamp & 0xFFFFFFFF) << 12) | (orderId & 0xFFF);
}

// NEW: Helper function for updateOrderInTree
int64_t PersistentOrderBook::generatePriceTimeKey(double price, int64_t timestamp, bool isBuy) const
{
    if (isBuy)
    {
        return createBidKey(price, timestamp, 0); // orderId not needed for key matching
    }
    else
    {
        return createAskKey(price, timestamp, 0);
    }
}

// Store order data
int PersistentOrderBook::storeOrderData(const Order &order)
{
    // Simple approach: use order ID as pseudo-page ID
    return static_cast<int>(order.order_id % 1000000); // Limit to reasonable page ID
}

Order PersistentOrderBook::loadOrderData(int64_t orderId)
{
    auto cacheIt = orderCache.find(orderId);
    if (cacheIt != orderCache.end())
    {
        return cacheIt->second;
    }

    // Not in cache (shouldn't happen)
    std::cerr << "Order " << orderId << " not found in cache" << std::endl;
    return Order();
}

bool PersistentOrderBook::deleteOrderData(int64_t orderId)
{
    bool removed = orderIdIndex.remove(orderId);
    orderCache.erase(orderId);
    orderIdToKey.erase(orderId); // Also remove from reverse lookup
    return removed;
}

bool PersistentOrderBook::insertOrderInTree(const Order &order)
{
    // Add to orderId index
    orderIdIndex.insert(order.order_id, order.order_id);

    // Create price-time key
    int64_t key;
    if (order.isBuy())
    {
        key = createBidKey(order.price, order.timestamp, order.order_id);
        bidsTree.insert(key, order.order_id);
    }
    else
    {
        key = createAskKey(order.price, order.timestamp, order.order_id);
        asksTree.insert(key, order.order_id);
    }

    // Store reverse lookup
    orderIdToKey[order.order_id] = key; // ADDED: Store key for later updates

    // Update cache
    orderCache[order.order_id] = order;

    return true;
}

bool PersistentOrderBook::removeOrderFromTree(int64_t orderId)
{
    auto cacheIt = orderCache.find(orderId);
    if (cacheIt == orderCache.end())
    {
        return false;
    }

    const Order &order = cacheIt->second;

    // Recreate the same key
    int64_t key;
    if (order.isBuy())
    {
        key = createBidKey(order.price, order.timestamp, order.order_id);
        bidsTree.remove(key);
    }
    else
    {
        key = createAskKey(order.price, order.timestamp, order.order_id);
        asksTree.remove(key);
    }

    // Remove from orderId index
    orderIdIndex.remove(orderId);

    // Remove from reverse lookup
    orderIdToKey.erase(orderId); // ADDED: Remove from reverse lookup

    // Remove from cache
    orderCache.erase(orderId);

    return true;
}

Order PersistentOrderBook::loadOrderFromTree(int64_t orderId)
{
    auto it = orderCache.find(orderId);
    if (it != orderCache.end())
    {
        return it->second;
    }

    return Order();
}

std::vector<int64_t> PersistentOrderBook::getBestBidKeys(int count)
{
    std::vector<int64_t> keys;
    auto allBids = bidsTree.rangeQuery(LLONG_MIN, LLONG_MAX);

    // Sort bids by price DESCENDING (highest first)
    std::sort(allBids.begin(), allBids.end(),
              [this](const std::pair<int64_t, int64_t> &a, const std::pair<int64_t, int64_t> &b)
              {
                  double priceA = decodePriceFromKey(a.first, true); // true = bid
                  double priceB = decodePriceFromKey(b.first, true);
                  return priceA > priceB; // DESCENDING
              });

    for (size_t i = 0; i < std::min(allBids.size(), static_cast<size_t>(count)); i++)
    {
        keys.push_back(allBids[i].first);
    }

    return keys;
}

std::vector<int64_t> PersistentOrderBook::getBestAskKeys(int count)
{
    std::vector<int64_t> keys;
    auto allAsks = asksTree.rangeQuery(LLONG_MIN, LLONG_MAX);

    // Sort asks by price ASCENDING (lowest first)
    std::sort(allAsks.begin(), allAsks.end(),
              [this](const std::pair<int64_t, int64_t> &a, const std::pair<int64_t, int64_t> &b)
              {
                  double priceA = decodePriceFromKey(a.first, false); // false = ask
                  double priceB = decodePriceFromKey(b.first, false);
                  return priceA < priceB; // ASCENDING
              });

    for (size_t i = 0; i < std::min(allAsks.size(), static_cast<size_t>(count)); i++)
    {
        keys.push_back(allAsks[i].first);
    }

    return keys;
}

Order PersistentOrderBook::loadOrderByKey(int64_t key, bool isBuy)
{
    std::pair<bool, int64_t> result;
    if (isBuy)
    {
        result = bidsTree.search(key);
    }
    else
    {
        result = asksTree.search(key);
    }

    if (result.first)
    {
        int64_t orderId = result.second;
        return loadOrderFromTree(orderId);
    }

    return Order();
}

bool PersistentOrderBook::addOrder(const Order &order)
{
    bool success = false;

    // Lock only for order insertion
    {
        std::lock_guard<std::mutex> lock(orderbookMutex);

        // Validate order
        if (order.quantity <= 0 || order.price <= 0)
        {
            std::cerr << "Invalid order: quantity=" << order.quantity
                      << ", price=" << order.price << std::endl;
            return false;
        }

        if (symbol != order.symbol)
        {
            std::cerr << "Order symbol mismatch: " << order.symbol
                      << " != " << symbol << std::endl;
            return false;
        }

        // Log to WriteAheadLog if available
        if (wal)
        {
            char orderData[Order::serializedSize()];
            order.serialize(orderData);
            int pageId = static_cast<int>(order.order_id % 1000000);
            wal->append(LOG_INSERT, pageId, orderData, Order::serializedSize());
        }

        // Insert into trees
        success = insertOrderInTree(order);

        if (success)
        {
            std::cout << "Order " << order.order_id << " added: "
                      << (order.isBuy() ? "BUY" : "SELL") << " "
                      << order.quantity << " " << symbol
                      << " @ $" << std::fixed << std::setprecision(2) << order.price << std::endl;

            stats.totalOrders++;
        }
    } // Lock released here

    // Try to match (will acquire its own lock)
    if (success)
    {
        auto trades = matchOrders();
        if (!trades.empty())
        {
            std::cout << "Generated " << trades.size() << " trades" << std::endl;
        }
    }

    return success;
}

bool PersistentOrderBook::cancelOrder(int64_t orderId)
{
    // Get order without lock first
    Order order;
    {
        std::lock_guard<std::mutex> lock(orderbookMutex);
        auto it = orderCache.find(orderId);
        if (it == orderCache.end())
        {
            std::cerr << "Order " << orderId << " not found" << std::endl;
            return false;
        }
        order = it->second;
    }

    if (order.order_id == 0)
    {
        std::cerr << "Order " << orderId << " not found" << std::endl;
        return false;
    }

    if (!order.isActive())
    {
        std::cerr << "Order " << orderId << " is not active (status="
                  << static_cast<int>(order.status) << ")" << std::endl;
        return false;
    }

    // Now cancel with lock
    std::lock_guard<std::mutex> lock(orderbookMutex);

    // Log to WriteAheadLog if available
    if (wal)
    {
        std::string cancelData = "CANCEL:" + std::to_string(orderId);
        int pageId = static_cast<int>(orderId % 1000000);
        wal->append(LOG_DELETE, pageId, cancelData.c_str(), cancelData.size());
    }

    bool success = removeOrderFromTree(orderId);

    if (success)
    {
        std::cout << "Order " << orderId << " cancelled" << std::endl;
    }
    else
    {
        std::cerr << "Failed to cancel order " << orderId << std::endl;
    }

    return success;
}

Order PersistentOrderBook::getOrder(int64_t orderId)
{
    std::lock_guard<std::mutex> lock(orderbookMutex);
    return loadOrderFromTree(orderId);
}

std::vector<Order> PersistentOrderBook::getBestBids(int count)
{
    std::lock_guard<std::mutex> lock(orderbookMutex);
    std::vector<Order> bids;

    auto keys = getBestBidKeys(count);
    for (int64_t key : keys)
    {
        Order order = loadOrderByKey(key, true);
        if (order.order_id != 0)
        {
            bids.push_back(order);
        }
    }

    return bids;
}

std::vector<Order> PersistentOrderBook::getBestAsks(int count)
{
    std::lock_guard<std::mutex> lock(orderbookMutex);
    std::vector<Order> asks;

    auto keys = getBestAskKeys(count);
    for (int64_t key : keys)
    {
        Order order = loadOrderByKey(key, false);
        if (order.order_id != 0)
        {
            asks.push_back(order);
        }
    }

    return asks;
}

double PersistentOrderBook::getBestBidPrice()
{
    // Use getBestBidsNoLock instead of getBestBids to avoid recursive lock
    std::lock_guard<std::mutex> lock(orderbookMutex);

    auto keys = getBestBidKeys(1);
    if (keys.empty())
        return 0.0;

    Order order = loadOrderByKey(keys[0], true);
    return order.order_id != 0 ? order.price : 0.0;
}

double PersistentOrderBook::getBestAskPrice()
{
    // Use getBestAsksNoLock instead of getBestAsks to avoid recursive lock
    std::lock_guard<std::mutex> lock(orderbookMutex);

    auto keys = getBestAskKeys(1);
    if (keys.empty())
        return 0.0;

    Order order = loadOrderByKey(keys[0], false);
    return order.order_id != 0 ? order.price : 0.0;
}

double PersistentOrderBook::getSpread()
{
    // Direct implementation to avoid recursive locks
    std::lock_guard<std::mutex> lock(orderbookMutex);

    double bid = 0.0, ask = 0.0;

    // Get best bid
    auto bidKeys = getBestBidKeys(1);
    if (!bidKeys.empty())
    {
        Order bidOrder = loadOrderByKey(bidKeys[0], true);
        if (bidOrder.order_id != 0)
            bid = bidOrder.price;
    }

    // Get best ask
    auto askKeys = getBestAskKeys(1);
    if (!askKeys.empty())
    {
        Order askOrder = loadOrderByKey(askKeys[0], false);
        if (askOrder.order_id != 0)
            ask = askOrder.price;
    }

    if (bid == 0.0 || ask == 0.0)
    {
        return 0.0;
    }

    return ask - bid;
}

std::vector<Trade> PersistentOrderBook::matchOrders()
{
    std::lock_guard<std::mutex> lock(orderbookMutex);
    std::vector<Trade> trades;
    std::cout << "\n=== DEBUG: matchOrders() for " << symbol << " ===" << std::endl;
    while (true)
    {
        // Get best bids and asks directly without calling getBestBids/getBestAsks
        std::vector<Order> bids, asks;

        // Get best bid keys and load orders
        auto bidKeys = getBestBidKeys(1);
        for (int64_t key : bidKeys)
        {
            Order order = loadOrderByKey(key, true);
            if (order.order_id != 0 && !order.isFilled()) // ADD: !order.isFilled()
                bids.push_back(order);
        }

        // Get best ask keys and load orders
        auto askKeys = getBestAskKeys(1);
        for (int64_t key : askKeys)
        {
            Order order = loadOrderByKey(key, false);
            if (order.order_id != 0 && !order.isFilled()) // ADD: !order.isFilled()
                asks.push_back(order);
        }

        if (bids.empty() || asks.empty())
        {

            break;
        }

        Order &bestBid = orderCache[bids[0].order_id];
        Order &bestAsk = orderCache[asks[0].order_id];

        // Check if prices cross
        if (bestBid.price < bestAsk.price)
        {
            break;
        }

        // Determine match price
        double matchPrice = bestBid.isMarket() ? bestAsk.price : bestBid.price;

        // Determine match quantity
        int buyRemaining = bestBid.getRemainingQuantity();
        int sellRemaining = bestAsk.getRemainingQuantity();
        int matchQuantity = std::min(buyRemaining, sellRemaining);

        if (matchQuantity <= 0)
        {
            break;
        }

        // Create trade
        Trade trade;
        trade.trade_id = stats.totalTrades + 1;
        trade.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        strncpy(trade.symbol, symbol.c_str(), sizeof(trade.symbol) - 1);
        trade.symbol[sizeof(trade.symbol) - 1] = '\0';
        trade.price = matchPrice;
        trade.quantity = matchQuantity;
        trade.buy_order_id = bestBid.order_id;
        trade.sell_order_id = bestAsk.order_id;
        trade.buyer_id = bestBid.user_id;
        trade.seller_id = bestAsk.user_id;

        trades.push_back(trade);

        // Log trade to WriteAheadLog
        if (wal)
        {
            char tradeData[Trade::serializedSize()];
            trade.serialize(tradeData);

            // Use trade ID as page ID for logging
            int pageId = static_cast<int>(trade.trade_id % 1000000);

            // Append to WAL
            wal->append(LOG_INSERT, pageId, tradeData, Trade::serializedSize());
        }

        // Update orders in cache
        bestBid.fill(matchQuantity, matchPrice);
        bestAsk.fill(matchQuantity, matchPrice);

        // Update cache with new quantities
        orderCache[bestBid.order_id] = bestBid;
        orderCache[bestAsk.order_id] = bestAsk;

        // *** CRITICAL FIX: Update partially filled orders in B+Tree ***
        if (!bestBid.isFilled())
        {
            updateOrderInTree(bestBid.order_id, bestBid);
        }
        else
        {
            removeOrderFromTree(bestBid.order_id);
        }

        if (!bestAsk.isFilled())
        {
            updateOrderInTree(bestAsk.order_id, bestAsk);
        }
        else
        {
            removeOrderFromTree(bestAsk.order_id);
        }

        // Update statistics
        stats.totalTrades++;
        stats.totalVolume += matchQuantity * matchPrice;

        std::cout << "TRADE: " << symbol << " " << matchQuantity
                  << " @ $" << std::fixed << std::setprecision(2) << matchPrice
                  << " (Buyer: " << bestBid.user_id
                  << ", Seller: " << bestAsk.user_id << ")" << std::endl;

        // If both orders are still active after partial fill, continue matching
        // This handles multiple matches between the same bid/ask pair
        if (bestBid.isFilled() || bestAsk.isFilled())
        {
            // One or both orders filled completely, break to re-evaluate best bids/asks
            break;
        }
        // Otherwise continue with same orders (they still have quantity)
    }

    std::cout << "DEBUG: matchOrders completed. Found " << trades.size() << " trades." << std::endl;

    if (!trades.empty())
    {
        std::cout << "DEBUG: Checking matchingEngine pointer: "
                  << (matchingEngine ? "VALID (address: " + std::to_string((uintptr_t)matchingEngine) + ")" : "NULL") << std::endl;

        if (matchingEngine)
        {
            std::cout << "DEBUG: Calling processTrades()..." << std::endl;
            matchingEngine->processTrades(trades);
            std::cout << "DEBUG: processTrades() call completed successfully." << std::endl;
        }
        else
        {
            std::cerr << "ERROR: matchingEngine is NULL! Cannot process trades." << std::endl;
            std::cerr << "       Make sure to pass MatchingEngine to OrderBook constructor." << std::endl;
        }
    }
    return trades;
}

void PersistentOrderBook::flush()
{
    std::lock_guard<std::mutex> lock(orderbookMutex);
    if (bufferManager)
    {
        bufferManager->flushAll();
    }
}

void PersistentOrderBook::printOrderBook(int depth)
{
    std::lock_guard<std::mutex> lock(orderbookMutex);

    // Calculate spread directly to avoid recursive locks
    double spread = 0.0;
    {
        double bid = 0.0, ask = 0.0;

        auto bidKeys = getBestBidKeys(1);
        if (!bidKeys.empty())
        {
            Order bidOrder = loadOrderByKey(bidKeys[0], true);
            if (bidOrder.order_id != 0)
                bid = bidOrder.price;
        }

        auto askKeys = getBestAskKeys(1);
        if (!askKeys.empty())
        {
            Order askOrder = loadOrderByKey(askKeys[0], false);
            if (askOrder.order_id != 0)
                ask = askOrder.price;
        }

        if (bid != 0.0 && ask != 0.0)
            spread = ask - bid;
    }

    std::cout << "\n=== Persistent Order Book: " << symbol << " ===" << std::endl;
    std::cout << "Spread: $" << std::fixed << std::setprecision(2) << spread << std::endl;
    std::cout << "Orders: " << orderCache.size() << std::endl;

    std::cout << "\nASKS (Sell):" << std::endl;
    std::cout << "Price\t\tVolume\t\tOrder ID" << std::endl;
    std::cout << "---------------------------------" << std::endl;

    // Get asks directly to avoid recursive lock
    std::vector<Order> asks;
    auto askKeys = getBestAskKeys(depth);
    for (int64_t key : askKeys)
    {
        Order order = loadOrderByKey(key, false);
        if (order.order_id != 0)
        {
            asks.push_back(order);
            std::cout << "$" << std::fixed << std::setprecision(2) << order.price
                      << "\t\t" << order.getRemainingQuantity()
                      << "\t\t" << order.order_id << std::endl;
        }
    }

    if (asks.empty())
    {
        std::cout << "No asks" << std::endl;
    }

    std::cout << "\nBIDS (Buy):" << std::endl;
    std::cout << "Price\t\tVolume\t\tOrder ID" << std::endl;
    std::cout << "---------------------------------" << std::endl;

    // Get bids directly to avoid recursive lock
    std::vector<Order> bids;
    auto bidKeys = getBestBidKeys(depth);
    for (int64_t key : bidKeys)
    {
        Order order = loadOrderByKey(key, true);
        if (order.order_id != 0)
        {
            bids.push_back(order);
            std::cout << "$" << std::fixed << std::setprecision(2) << order.price
                      << "\t\t" << order.getRemainingQuantity()
                      << "\t\t" << order.order_id << std::endl;
        }
    }

    if (bids.empty())
    {
        std::cout << "No bids" << std::endl;
    }

    std::cout << "=================================\n"
              << std::endl;
}

size_t PersistentOrderBook::getOrderCount()
{
    std::lock_guard<std::mutex> lock(orderbookMutex);
    return orderCache.size();
}

void PersistentOrderBook::updateOrderInTree(int64_t orderId, const Order &updatedOrder)
{
    bool isBuy = updatedOrder.isBuy();
    BPlusTree *tree = isBuy ? &bidsTree : &asksTree; // FIXED: Use addresses

    // Generate the same key that was used when order was inserted
    int64_t key;
    if (isBuy)
    {
        key = createBidKey(updatedOrder.price, updatedOrder.timestamp, orderId);
    }
    else
    {
        key = createAskKey(updatedOrder.price, updatedOrder.timestamp, orderId);
    }

    // Remove old entry
    tree->remove(key);

    // Re-insert with updated order data (using orderId as value)
    tree->insert(key, orderId); // FIXED: BPlusTree expects (key, value)

    // Update reverse lookup
    orderIdToKey[orderId] = key;

    std::cout << "DEBUG: Updated order " << orderId << " in tree (filled: "
              << updatedOrder.filled_quantity << "/" << updatedOrder.quantity << ")" << std::endl;
}

double PersistentOrderBook::decodePriceFromKey(int64_t key, bool isBid) const
{
    // Extract price from key (key = price << 44 | ...)
    int64_t pricePart = key >> 44;

    if (isBid)
    {
        // For bids: key encodes (maxPrice - scaledPrice)
        const int64_t maxPrice = 1000000; // Must match createBidKey()
        return static_cast<double>(maxPrice - pricePart) / 10000.0;
    }
    else
    {
        // For asks: key encodes scaledPrice directly
        return static_cast<double>(pricePart) / 10000.0;
    }
}