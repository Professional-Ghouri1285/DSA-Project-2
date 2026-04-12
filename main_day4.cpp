// main_day4.cpp - Basic trading platform (Days 1-4)
#include <iostream>
#include <thread>
#include <chrono>
#include "storage/FileManager.h"
#include "storage/BufferManager.h"
#include "storage/WAL.h"
#include "btree/BTree.h"
#include "btree/BPlusTree.h"
#include "index/HashIndex.h"
#include "core/OrderBook.h"
#include "core/MatchingEngine.h"
#include "core/UserManager.h"

using namespace std;

int main()
{
    cout << "=== Trading Platform (Days 1-4) ===\n";

    try
    {
        // Initialize components - FIXED CONSTRUCTOR
        FileManager fm("trading_platform.db");
        BufferManager bm(&fm, nullptr, 1000); // Added nullptr for WAL

        // Create a simple test - FIXED createUser call
        cout << "1. Creating test user...\n";
        UserManager user_manager(&bm, nullptr, -1, -1);
        int user_id = user_manager.createUser("test_user", "password", 10000.0); // Removed email

        cout << "2. Initializing matching engine...\n";
        MatchingEngine engine(&bm, nullptr, &user_manager);
        engine.addSymbol("AAPL");
        engine.addSymbol("GOOGL");

        cout << "3. Testing basic order placement...\n";
        Order buy_order;
        buy_order.user_id = user_id;
        strncpy(buy_order.symbol, "AAPL", sizeof(buy_order.symbol));
        buy_order.side = SIDE_BUY;
        buy_order.type = ORDER_LIMIT;
        buy_order.price = 150.0;
        buy_order.quantity = 10;
        buy_order.status = STATUS_PENDING;
        buy_order.timestamp = chrono::duration_cast<chrono::nanoseconds>(
                                  chrono::system_clock::now().time_since_epoch())
                                  .count();

        int64_t order_id = engine.placeOrder(buy_order);
        cout << "   Order placed with ID: " << order_id << "\n";

        cout << "4. Testing order book...\n";
        auto *order_book = engine.getOrderBook("AAPL");
        if (order_book)
        {
            cout << "   Order book exists for AAPL\n";
        }

        cout << "5. Testing user portfolio...\n";
        double balance = user_manager.getBalance(user_id);
        cout << "   User balance: $" << balance << "\n";

        cout << "\n✅ Day 1-4 functionality working!\n";
        cout << "   Run 'trading_platform_day5' for the full web interface.\n";
    }
    catch (const exception &e)
    {
        cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}