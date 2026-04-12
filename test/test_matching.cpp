// test_matching.cpp
#include "../core/MatchingEngine.h"
#include <iostream>

int main()
{
    FileManager fm("trading.db");
    BufferManager bm(&fm);
    WriteAheadLog wal("trading.log");

    MatchingEngine engine(&bm, &wal);

    // Test adding orders
    Order buyOrder("AAPL", 0, 1001, SIDE_BUY, ORDER_LIMIT, 150.50, 100);
    Order sellOrder("AAPL", 0, 1002, SIDE_SELL, ORDER_LIMIT, 150.50, 50);

    engine.placeOrder(buyOrder);
    engine.placeOrder(sellOrder);

    engine.printMarketSummary();
    engine.printAllBooks();

    return 0;
}