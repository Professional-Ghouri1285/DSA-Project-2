// quick_test.cpp
#include <iostream>
#include "../btree/BPlusTree.h"
#include "../storage/BufferManager.h"
#include "../storage/FileManager.h"

int main()
{
    std::cout << "Testing rangeQuery directly...\n";

    FileManager fm("test_users.db");
    BufferManager bm(&fm);

    // Try to create B+Tree and test rangeQuery
    BPlusTree tree(&bm, -1);

    std::cout << "1. Inserting test key...\n";
    tree.insert(1000, 1234); // Insert test data

    std::cout << "2. Testing rangeQuery...\n";
    auto results = tree.rangeQuery(1000, 1000);

    std::cout << "Found " << results.size() << " results\n";

    if (!results.empty())
    {
        std::cout << "First result: key=" << results[0].first
                  << ", value=" << results[0].second << "\n";
    }

    return 0;
}