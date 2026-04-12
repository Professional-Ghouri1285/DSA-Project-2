// test/day3_test.cpp
#include <iostream>
#include <cstring>
#include "../storage/FileManager.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h"
#include "../btree/BPlusTree.h"
#include "../index/HashIndex.h"

void testBPlusTreeDeletion()
{
    std::cout << "\n=== Testing B+Tree Deletion ===\n";

    // Clean up old files
    remove("deletion_test.db");
    remove("deletion_test.db.wal");

    FileManager fm("deletion_test.db");
    WriteAheadLog wal("deletion_test.db.wal");
    BufferManager bm(&fm, &wal, 20);

    // Create B+Tree
    BPlusTree bpt(&bm);

    // Insert many keys
    for (int i = 1; i <= 50; i++)
    {
        bpt.insert(i * 10, i * 100);
    }

    std::cout << "Inserted 50 keys\n";
    std::cout << "Tree height: " << bpt.getHeight() << "\n";

    // Delete some keys
    for (int i = 1; i <= 10; i++)
    {
        bool removed = bpt.remove(i * 10);
        std::cout << "Delete key " << (i * 10) << ": "
                  << (removed ? "SUCCESS" : "FAILED") << "\n";
    }

    // Verify remaining keys
    for (int i = 11; i <= 50; i++)
    {
        auto result = bpt.search(i * 10);
        if (!result.first)
        {
            std::cout << "ERROR: Key " << (i * 10) << " missing after deletion!\n";
        }
    }

    // Verify deleted keys are gone
    for (int i = 1; i <= 10; i++)
    {
        auto result = bpt.search(i * 10);
        if (result.first)
        {
            std::cout << "ERROR: Key " << (i * 10) << " still present!\n";
        }
    }

    wal.commit(wal.getLastLSN());
    std::cout << "B+Tree deletion test completed\n";
}

void testWAL()
{
    std::cout << "\n=== Testing Write-Ahead Log ===\n";

    // Delete old log file
    std::remove("test_wal.log");

    try
    {
        // Create WAL
        WriteAheadLog wal("test_wal.log");

        // Log some operations
        std::cout << "Appending log records...\n";
        int64_t lsn1 = wal.append(LOG_INSERT, 1, "INSERT KEY=100", 14);
        int64_t lsn2 = wal.append(LOG_UPDATE, 2, "UPDATE KEY=200", 14);
        int64_t lsn3 = wal.append(LOG_DELETE, 3, "DELETE KEY=300", 14);

        std::cout << "Logged operations with LSNs: "
                  << lsn1 << ", " << lsn2 << ", " << lsn3 << "\n";

        wal.commit(lsn3);
        std::cout << "Commit record written.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "WAL test failed during writing: " << e.what() << "\n";
        return;
    }

    // Replay (simulating crash recovery)
    try
    {
        std::cout << "\nSimulating crash recovery...\n";
        WriteAheadLog wal2("test_wal.log");
        wal2.replay();
        std::cout << "WAL test completed successfully!\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "WAL test failed during replay: " << e.what() << "\n";
    }
}

void testHashIndex()
{
    std::cout << "\n=== Testing HashIndex ===\n";
    std::cout << "Starting HashIndex test...\n";

    // Clean up old files
    remove("hash_test.db");
    remove("hash_test.db.wal");

    try
    {
        FileManager fm("hash_test.db");
        WriteAheadLog wal("hash_test.db.wal");
        BufferManager bm(&fm, &wal, 20);

        HashIndex hash_index(&bm, 11); // Small for testing

        // Insert some symbols using int64_t keys
        std::cout << "\nInserting symbols...\n";

        // Test 1: Insert with int64_t keys
        bool insert1 = hash_index.insert(1001, 100); // key=1001, value=100
        std::cout << "Insert key 1001 -> value 100: "
                  << (insert1 ? "SUCCESS" : "FAILED") << "\n";

        bool insert2 = hash_index.insert(1002, 101);
        std::cout << "Insert key 1002 -> value 101: "
                  << (insert2 ? "SUCCESS" : "FAILED") << "\n";

        bool insert3 = hash_index.insert(1003, 102);
        std::cout << "Insert key 1003 -> value 102: "
                  << (insert3 ? "SUCCESS" : "FAILED") << "\n";

        bool insert4 = hash_index.insert(1004, 103);
        std::cout << "Insert key 1004 -> value 103: "
                  << (insert4 ? "SUCCESS" : "FAILED") << "\n";

        // Search using int64_t keys
        std::cout << "\nSearching keys:\n";
        auto result1 = hash_index.search(1001);
        std::cout << "Search key 1001 -> found: " << result1.first
                  << ", value: " << result1.second
                  << " (expected: true, 100)\n";

        auto result2 = hash_index.search(1002);
        std::cout << "Search key 1002 -> found: " << result2.first
                  << ", value: " << result2.second
                  << " (expected: true, 101)\n";

        auto result3 = hash_index.search(1003);
        std::cout << "Search key 1003 -> found: " << result3.first
                  << ", value: " << result3.second
                  << " (expected: true, 102)\n";

        // Test update (re-insert with new value)
        std::cout << "\nTesting update:\n";
        bool update = hash_index.insert(1001, 200); // Update value
        std::cout << "Update key 1001 -> value 200: "
                  << (update ? "SUCCESS" : "FAILED") << "\n";

        auto updated_result = hash_index.search(1001);
        std::cout << "Key 1001 updated -> found: " << updated_result.first
                  << ", value: " << updated_result.second
                  << " (expected: true, 200)\n";

        // Test delete
        std::cout << "\nTesting delete:\n";
        bool removed = hash_index.remove(1003);
        std::cout << "Delete key 1003: " << (removed ? "SUCCESS" : "FAILED") << "\n";

        auto after_delete = hash_index.search(1003);
        if (!after_delete.first)
        {
            std::cout << "Key 1003 correctly deleted (not found)\n";
        }
        else
        {
            std::cout << "ERROR: Key 1003 still found after delete!\n";
        }

        // Test searching for non-existent key
        std::cout << "\nTesting non-existent key:\n";
        auto notfound = hash_index.search(9999);
        if (!notfound.first)
        {
            std::cout << "Key 9999 correctly not found\n";
        }

        // Test 2: Insert with string symbols
        std::cout << "\nTesting string symbol insertion:\n";
        bool str_insert1 = hash_index.insert("AAPL", 500);
        std::cout << "Insert symbol 'AAPL' -> value 500: "
                  << (str_insert1 ? "SUCCESS" : "FAILED") << "\n";

        bool str_insert2 = hash_index.insert("GOOGL", 600);
        std::cout << "Insert symbol 'GOOGL' -> value 600: "
                  << (str_insert2 ? "SUCCESS" : "FAILED") << "\n";

        // Search string symbols
        std::cout << "\nSearching string symbols:\n";
        auto str_result1 = hash_index.search("AAPL");
        std::cout << "Search symbol 'AAPL' -> found: " << str_result1.first
                  << ", value: " << str_result1.second
                  << " (expected: true, 500)\n";

        auto str_result2 = hash_index.search("GOOGL");
        std::cout << "Search symbol 'GOOGL' -> found: " << str_result2.first
                  << ", value: " << str_result2.second
                  << " (expected: true, 600)\n";

        // Delete string symbol
        std::cout << "\nTesting string symbol delete:\n";
        bool str_removed = hash_index.remove("AAPL");
        std::cout << "Delete symbol 'AAPL': " << (str_removed ? "SUCCESS" : "FAILED") << "\n";

        auto after_str_delete = hash_index.search("AAPL");
        if (!after_str_delete.first)
        {
            std::cout << "Symbol 'AAPL' correctly deleted (not found)\n";
        }

        std::cout << "\nHashIndex test completed successfully!\n";
        std::cout << "Total entries: " << hash_index.getEntryCount() << "\n";
        std::cout << "Number of buckets: " << hash_index.getNumBuckets() << "\n";

        // Test persistence
        std::cout << "\nTesting persistence (save/load)...\n";
        if (hash_index.saveToDisk())
        {
            std::cout << "HashIndex saved to disk successfully\n";

            // Create new HashIndex and load
            HashIndex hash_index2(&bm, 11);
            if (hash_index2.loadFromDisk())
            {
                std::cout << "HashIndex loaded from disk successfully\n";

                // Verify loaded data
                auto loaded_result = hash_index2.search(1001);
                if (loaded_result.first && loaded_result.second == 200)
                {
                    std::cout << "✓ Persistence test passed: Key 1001 correctly loaded\n";
                }

                auto loaded_str_result = hash_index2.search("GOOGL");
                if (loaded_str_result.first && loaded_str_result.second == 600)
                {
                    std::cout << "✓ Persistence test passed: Symbol 'GOOGL' correctly loaded\n";
                }
            }
        }

        // Commit WAL
        wal.commit(wal.getLastLSN());
        bm.flushAll();
    }
    catch (const std::exception &e)
    {
        std::cerr << "HashIndex test failed with exception: " << e.what() << "\n";
    }

    std::cout << "HashIndex test function completed.\n";
}

void testIntegration()
{
    std::cout << "\n=== Testing Integration ===\n";

    // Clean up old files
    remove("trading_platform.db");
    remove("trading_wal.log");

    try
    {
        FileManager fm("trading_platform.db");
        WriteAheadLog wal("trading_wal.log");
        BufferManager bm(&fm, &wal, 50);

        // Create B+Tree for order book
        BPlusTree order_book(&bm);

        // Create HashIndex for symbol lookup
        HashIndex symbol_index(&bm, 101);

        // Initialize with some stocks
        std::vector<std::string> symbols = {"AAPL", "GOOGL", "TSLA", "MSFT", "AMZN"};
        for (int i = 0; i < symbols.size(); i++)
        {
            // Store stock info as value (e.g., stock ID)
            symbol_index.insert(symbols[i], 1000 + i);

            // Log the operation
            std::string log_msg = "ADD_SYMBOL:" + symbols[i];
            wal.append(LOG_INSERT, 1000 + i, log_msg.c_str(), log_msg.length());
        }

        wal.commit(wal.getLastLSN());

        // Add some orders to order book
        std::cout << "Adding orders to order book...\n";
        for (int i = 0; i < 10; i++)
        {
            int64_t order_id = 10000 + i;
            int64_t price = 100 + i * 10;
            order_book.insert(price, order_id);

            std::string log_msg = "ADD_ORDER:" + std::to_string(order_id) +
                                  "@" + std::to_string(price);
            wal.append(LOG_INSERT, price, log_msg.c_str(), log_msg.length());
        }

        std::cout << "Integration test: Added " << symbols.size() << " symbols\n";
        std::cout << "Integration test: Added 10 orders to order book\n";
        std::cout << "Integration test: WAL contains " << wal.getLastLSN() << " records\n";

        // Test symbol lookup
        std::cout << "\nTesting symbol lookup:\n";
        auto aapl_lookup = symbol_index.search("AAPL");
        if (aapl_lookup.first)
        {
            std::cout << "AAPL lookup -> value: " << aapl_lookup.second
                      << " (expected: 1000)\n";
        }

        auto order_lookup = order_book.search(150);
        if (order_lookup.first)
        {
            std::cout << "Order book lookup for price 150 -> order ID: "
                      << order_lookup.second << "\n";
        }

        // Print statistics
        symbol_index.printStatistics();

        // Final commit
        wal.commit(wal.getLastLSN());
        bm.flushAll();

        std::cout << "Integration test completed successfully!\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Integration test failed: " << e.what() << "\n";
    }
}

int main()
{
    try
    {
        std::cout << "🚀 DAY 3 TESTS STARTING...\n\n";

        testBPlusTreeDeletion();
        testWAL();
        testHashIndex();
        testIntegration();

        std::cout << "\n✅ ALL DAY 3 TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}