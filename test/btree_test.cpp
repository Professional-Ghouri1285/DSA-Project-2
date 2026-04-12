// trading_platform/test/btree_test.cpp
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include "../storage/FileManager.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h" // Add WAL header
#include "../btree/BTree.h"
#include "../btree/BPlusTree.h"

using namespace std;
using namespace std::chrono;

// Helper function to create WAL for testing
WriteAheadLog *createTestWAL(const string &base_name)
{
    string wal_file = base_name + ".wal";
    remove(wal_file.c_str()); // Clean up old WAL file
    return new WriteAheadLog(wal_file);
}

void testBTreeBasic()
{
    cout << "\n=== Testing B-Tree Basic Operations ===\n";

    remove("test_btree.db");

    FileManager fm("test_btree.db");
    WriteAheadLog *wal = createTestWAL("test_btree");
    BufferManager bm(&fm, wal, 50); // ✅ Now with WAL parameter

    BTree btree(&bm);

    cout << "B-Tree created with root: " << btree.getRootPageId() << "\n";

    cout << "\nInserting keys...\n";
    for (int i = 1; i <= 20; i++)
    {
        btree.insert(i * 10, i * 100);
        cout << "Inserted key " << (i * 10) << " -> value " << (i * 100) << "\n";
    }

    btree.printTree();

    cout << "\nSearching for keys...\n";
    for (int i = 1; i <= 5; i++)
    {
        auto result = btree.search(i * 10);
        if (result.first)
        {
            cout << "Found key " << (i * 10) << " -> value " << result.second << "\n";
        }
    }

    auto result = btree.search(999);
    if (!result.first)
    {
        cout << "Key 999 correctly not found\n";
    }

    btree.validate();

    // Commit transaction
    wal->commit(0); // Pass the LSN you want to commit

    delete wal; // Clean up WAL
    cout << "\n✓ B-Tree basic test passed\n";
}

void testBTreePersistence()
{
    cout << "\n=== Testing B-Tree Persistence ===\n";

    FileManager fm("test_btree.db");
    WriteAheadLog *wal = createTestWAL("test_btree");
    BufferManager bm(&fm, wal, 50);

    // Try to load existing B-Tree
    BTree btree(&bm, 1); // Assuming root is page 1

    cout << "Loaded B-Tree with root: " << btree.getRootPageId() << "\n";

    cout << "\nVerifying persisted data...\n";
    for (int i = 1; i <= 5; i++)
    {
        auto result = btree.search(i * 10);
        if (result.first)
        {
            cout << "Persisted key " << (i * 10) << " -> value " << result.second << "\n";
        }
    }

    cout << "\nInserting more data...\n";
    for (int i = 21; i <= 30; i++)
    {
        btree.insert(i * 10, i * 100);
    }

    btree.printTree();

    wal->commit(0);
    delete wal;
    cout << "\n✓ B-Tree persistence test passed\n";
}

void testBTreeDeletion()
{
    cout << "\n=== Testing B-Tree Deletion Operations ===\n";

    remove("test_btree_delete.db");

    FileManager fm("test_btree_delete.db");
    WriteAheadLog *wal = createTestWAL("test_btree_delete");
    BufferManager bm(&fm, wal, 100);
    BTree btree(&bm);

    cout << "Inserting 100 keys...\n";
    for (int i = 1; i <= 100; i++)
    {
        btree.insert(i, i * 10);
        if (i % 20 == 0)
            cout << "  Inserted " << i << " keys\n";
    }

    cout << "\nInitial tree:\n";
    btree.printTree();

    // Test 1: Simple deletions
    cout << "\n--- Test 1: Simple deletions ---\n";
    vector<int> keys_to_delete = {50, 25, 75, 10, 90};
    for (int key : keys_to_delete)
    {
        cout << "Deleting key " << key << "... ";
        if (btree.remove(key))
        {
            cout << "SUCCESS\n";
            // Verify deletion
            auto result = btree.search(key);
            if (result.first)
            {
                cout << "ERROR: Key " << key << " still exists after deletion!\n";
            }
        }
        else
        {
            cout << "FAILED (key not found)\n";
        }
    }

    // Test 2: Verify remaining keys
    cout << "\n--- Test 2: Verifying remaining keys ---\n";
    int found_count = 0;
    for (int i = 1; i <= 100; i++)
    {
        auto result = btree.search(i);
        if (result.first)
        {
            found_count++;
            if (result.second != i * 10)
            {
                cout << "ERROR: Key " << i << " has wrong value: "
                     << result.second << " (expected " << (i * 10) << ")\n";
            }
        }
        else
        {
            // Check if this key should have been deleted
            if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
            {
                cout << "ERROR: Key " << i << " missing but wasn't deleted!\n";
            }
        }
    }
    cout << "Found " << found_count << "/" << (100 - keys_to_delete.size())
         << " expected keys after deletion\n";

    // Test 3: Sequential deletions causing underflow
    cout << "\n--- Test 3: Sequential deletions (testing underflow) ---\n";
    for (int i = 1; i <= 30; i++)
    {
        if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
        {
            cout << "Deleting key " << i << "... ";
            if (btree.remove(i))
            {
                cout << "SUCCESS";
                if (i % 10 == 0)
                {
                    cout << " (tree now has " << btree.getTotalKeys() << " keys)";
                }
                cout << "\n";
            }
        }
    }

    cout << "\nTree after sequential deletions:\n";
    btree.printTree();

    // Test 4: Re-insert deleted keys
    cout << "\n--- Test 4: Re-inserting deleted keys ---\n";
    for (int i = 1; i <= 30; i++)
    {
        if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
        {
            btree.insert(i, i * 100); // New value
            cout << "Re-inserted key " << i << " with value " << (i * 100) << "\n";
        }
    }

    // Verify new values
    cout << "\nVerifying re-inserted keys have new values...\n";
    for (int i = 1; i <= 30; i++)
    {
        if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
        {
            auto result = btree.search(i);
            if (result.first)
            {
                int expected = (i >= 1 && i <= 30) ? (i * 100) : (i * 10);
                if (result.second != expected)
                {
                    cout << "ERROR: Key " << i << " has value " << result.second
                         << " (expected " << expected << ")\n";
                }
            }
        }
    }

    btree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B-Tree deletion test passed\n";
}

void testBTreeLargeWithDeletion()
{
    cout << "\n=== Testing B-Tree with Large Dataset and Deletions ===\n";

    remove("test_btree_large_delete.db");

    FileManager fm("test_btree_large_delete.db");
    WriteAheadLog *wal = createTestWAL("test_btree_large_delete");
    BufferManager bm(&fm, wal, 200);
    BTree btree(&bm);

    // Phase 1: Insert 1000 keys
    auto start = high_resolution_clock::now();
    cout << "\nPhase 1: Inserting 1000 keys...\n";
    for (int i = 1; i <= 1000; i++)
    {
        btree.insert(i, i * 10);
        if (i % 200 == 0)
        {
            cout << "  Inserted " << i << " keys\n";
        }
    }
    auto insert_end = high_resolution_clock::now();

    cout << "After insertion:\n";
    cout << "  Tree height: " << btree.getHeight() << "\n";
    cout << "  Total keys: " << btree.getTotalKeys() << "\n";

    // Phase 2: Delete 300 random keys
    cout << "\nPhase 2: Deleting 300 random keys...\n";
    vector<int> all_keys(1000);
    iota(all_keys.begin(), all_keys.end(), 1);

    random_device rd;
    mt19937 g(rd());
    shuffle(all_keys.begin(), all_keys.end(), g);

    vector<int> deleted_keys(all_keys.begin(), all_keys.begin() + 300);
    sort(deleted_keys.begin(), deleted_keys.end());

    int delete_count = 0;
    auto delete_start = high_resolution_clock::now();
    for (int key : deleted_keys)
    {
        if (btree.remove(key))
        {
            delete_count++;
            if (delete_count % 60 == 0)
            {
                cout << "  Deleted " << delete_count << " keys\n";
            }
        }
    }
    auto delete_end = high_resolution_clock::now();

    cout << "After deletion:\n";
    cout << "  Tree height: " << btree.getHeight() << "\n";
    cout << "  Total keys: " << btree.getTotalKeys() << "\n";

    // Phase 3: Verify deletions
    cout << "\nPhase 3: Verifying deletions...\n";
    int correctly_deleted = 0;
    int incorrectly_deleted = 0;
    int still_present = 0;

    for (int i = 1; i <= 1000; i++)
    {
        auto result = btree.search(i);
        bool should_exist = !binary_search(deleted_keys.begin(), deleted_keys.end(), i);

        if (should_exist && !result.first)
        {
            incorrectly_deleted++;
            cout << "ERROR: Key " << i << " should exist but was deleted!\n";
        }
        else if (!should_exist && result.first)
        {
            still_present++;
            cout << "ERROR: Key " << i << " should be deleted but still exists!\n";
        }
        else if (should_exist && result.first)
        {
            if (result.second == i * 10)
            {
                correctly_deleted++;
            }
            else
            {
                cout << "ERROR: Key " << i << " has wrong value: "
                     << result.second << " (expected " << (i * 10) << ")\n";
            }
        }
    }

    cout << "Verification results:\n";
    cout << "  Correctly present: " << correctly_deleted << "\n";
    cout << "  Incorrectly deleted: " << incorrectly_deleted << "\n";
    cout << "  Still present (should be deleted): " << still_present << "\n";

    // Phase 4: Re-insert deleted keys
    cout << "\nPhase 4: Re-inserting 100 deleted keys...\n";
    auto reinsert_start = high_resolution_clock::now();
    for (int i = 0; i < 100; i++)
    {
        int key = deleted_keys[i];
        btree.insert(key, key * 100); // New value
        if ((i + 1) % 20 == 0)
        {
            cout << "  Re-inserted " << (i + 1) << " keys\n";
        }
    }
    auto reinsert_end = high_resolution_clock::now();

    // Phase 5: Final verification
    cout << "\nPhase 5: Final verification...\n";
    int correct_final = 0;
    for (int i = 1; i <= 1000; i++)
    {
        auto result = btree.search(i);
        if (result.first)
        {
            int expected_value;
            if (i <= 100 && binary_search(deleted_keys.begin(), deleted_keys.begin() + 100, i))
            {
                expected_value = i * 100; // Re-inserted with new value
            }
            else if (binary_search(deleted_keys.begin() + 100, deleted_keys.end(), i))
            {
                expected_value = -1; // Should have been deleted
            }
            else
            {
                expected_value = i * 10; // Original value
            }

            if (expected_value != -1 && result.second == expected_value)
            {
                correct_final++;
            }
        }
    }

    cout << "Final tree:\n";
    btree.printTree();

    // Performance statistics
    auto total_end = high_resolution_clock::now();

    auto insert_duration = duration_cast<milliseconds>(insert_end - start);
    auto delete_duration = duration_cast<milliseconds>(delete_end - delete_start);
    auto reinsert_duration = duration_cast<milliseconds>(reinsert_end - reinsert_start);
    auto total_duration = duration_cast<milliseconds>(total_end - start);

    cout << "\nPerformance statistics:\n";
    cout << "  Insert 1000 keys: " << insert_duration.count() << " ms ("
         << (1000.0 / insert_duration.count() * 1000) << " ops/sec)\n";
    cout << "  Delete 300 keys: " << delete_duration.count() << " ms ("
         << (300.0 / delete_duration.count() * 1000) << " ops/sec)\n";
    cout << "  Re-insert 100 keys: " << reinsert_duration.count() << " ms ("
         << (100.0 / reinsert_duration.count() * 1000) << " ops/sec)\n";
    cout << "  Total time: " << total_duration.count() << " ms\n";

    btree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B-Tree large dataset with deletions test passed\n";
}

void testBTreeEdgeCases()
{
    cout << "\n=== Testing B-Tree Edge Cases ===\n";

    remove("test_btree_edge.db");

    FileManager fm("test_btree_edge.db");
    WriteAheadLog *wal = createTestWAL("test_btree_edge");
    BufferManager bm(&fm, wal, 50);
    BTree btree(&bm);

    // Edge case 1: Delete from empty tree
    cout << "\nEdge case 1: Delete from empty tree...\n";
    if (!btree.remove(1))
    {
        cout << "Correctly failed to delete from empty tree\n";
    }

    // Edge case 2: Insert and delete single key
    cout << "\nEdge case 2: Insert and delete single key...\n";
    btree.insert(999, 9990);
    cout << "Tree with single key:\n";
    btree.printTree();

    btree.remove(999);
    cout << "Tree after deleting single key:\n";
    btree.printTree();

    // Edge case 3: Delete non-existent key
    cout << "\nEdge case 3: Delete non-existent key...\n";
    for (int i = 1; i <= 10; i++)
    {
        btree.insert(i, i * 10);
    }

    if (!btree.remove(999))
    {
        cout << "Correctly failed to delete non-existent key 999\n";
    }

    // Edge case 4: Delete all keys from small tree
    cout << "\nEdge case 4: Delete all keys sequentially...\n";
    for (int i = 1; i <= 10; i++)
    {
        cout << "Deleting key " << i << "... ";
        if (btree.remove(i))
        {
            cout << "SUCCESS (tree now has " << btree.getTotalKeys() << " keys)\n";
        }
    }

    cout << "Empty tree:\n";
    btree.printTree();

    // Edge case 5: Delete causing tree height reduction
    cout << "\nEdge case 5: Delete causing tree height reduction...\n";

    // Create a 2-level tree
    for (int i = 1; i <= 500; i++)
    {
        btree.insert(i, i * 10);
    }

    cout << "Initial 2-level tree:\n";
    cout << "  Height: " << btree.getHeight() << "\n";
    cout << "  Keys: " << btree.getTotalKeys() << "\n";

    // Delete most keys
    for (int i = 400; i <= 500; i++)
    {
        btree.remove(i);
    }

    cout << "After deleting many keys:\n";
    cout << "  Height: " << btree.getHeight() << "\n";
    cout << "  Keys: " << btree.getTotalKeys() << "\n";

    btree.printTree();

    btree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B-Tree edge cases test passed\n";
}

void testBTreeStress()
{
    cout << "\n=== Testing B-Tree Stress Test ===\n";

    remove("test_btree_stress.db");

    FileManager fm("test_btree_stress.db");
    WriteAheadLog *wal = createTestWAL("test_btree_stress");
    BufferManager bm(&fm, wal, 500); // Larger buffer for stress test
    BTree btree(&bm);

    const int TOTAL_OPERATIONS = 10000;
    const int BATCH_SIZE = 1000;

    cout << "Performing " << TOTAL_OPERATIONS << " random operations...\n";

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> op_dist(0, 2); // 0=insert, 1=delete, 2=search
    uniform_int_distribution<> key_dist(1, 2000);

    int insert_count = 0;
    int delete_count = 0;
    int search_count = 0;
    int success_count = 0;

    auto start = high_resolution_clock::now();

    for (int i = 0; i < TOTAL_OPERATIONS; i++)
    {
        int operation = op_dist(gen);
        int key = key_dist(gen);

        switch (operation)
        {
        case 0: // Insert
            if (btree.insert(key, key * 10))
            {
                insert_count++;
                success_count++;
            }
            break;

        case 1: // Delete
            if (btree.remove(key))
            {
                delete_count++;
                success_count++;
            }
            break;

        case 2: // Search
            btree.search(key);
            search_count++;
            success_count++;
            break;
        }

        if ((i + 1) % BATCH_SIZE == 0)
        {
            cout << "  Completed " << (i + 1) << " operations...\n";

            // Quick validation
            int current_keys = btree.getTotalKeys();
            cout << "    Current keys: " << current_keys
                 << ", Height: " << btree.getHeight() << "\n";
        }
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);

    cout << "\nStress test results:\n";
    cout << "  Total operations: " << TOTAL_OPERATIONS << "\n";
    cout << "  Successful operations: " << success_count << "\n";
    cout << "  Insert operations: " << insert_count << "\n";
    cout << "  Delete operations: " << delete_count << "\n";
    cout << "  Search operations: " << search_count << "\n";
    cout << "  Final key count: " << btree.getTotalKeys() << "\n";
    cout << "  Final tree height: " << btree.getHeight() << "\n";
    cout << "  Total time: " << duration.count() << " ms\n";
    cout << "  Operations per second: "
         << (TOTAL_OPERATIONS * 1000.0 / duration.count()) << "\n";

    // Final validation
    btree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B-Tree stress test passed\n";
}

// Add these test functions after the B-Tree tests

void testBPlusTreeBasic()
{
    cout << "\n=== Testing B+Tree Basic Operations ===\n";

    remove("test_bplustree.db");
    remove("test_bplustree.db.wal");

    FileManager fm("test_bplustree.db");
    WriteAheadLog *wal = createTestWAL("test_bplustree");
    BufferManager bm(&fm, wal, 50);
    BPlusTree bptree(&bm);

    cout << "B+Tree created with root: " << bptree.getRootPageId() << "\n";

    cout << "\nInserting keys...\n";
    for (int i = 1; i <= 20; i++)
    {
        bptree.insert(i * 10, i * 100);
        cout << "Inserted key " << (i * 10) << " -> value " << (i * 100) << "\n";
    }

    bptree.printTree();
    bptree.printLeaves();

    cout << "\nSearching for keys...\n";
    for (int i = 1; i <= 5; i++)
    {
        auto result = bptree.search(i * 10);
        if (result.first)
        {
            cout << "Found key " << (i * 10) << " -> value " << result.second << "\n";
        }
    }

    // Test non-existent key
    auto result = bptree.search(999);
    if (!result.first)
    {
        cout << "Key 999 correctly not found\n";
    }

    // Test range query
    cout << "\nTesting range query (50-150)...\n";
    auto range = bptree.rangeQuery(50, 150);
    cout << "Range query found " << range.size() << " keys: ";
    for (const auto &kv : range)
    {
        cout << kv.first << " ";
    }
    cout << "\n";

    bptree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B+Tree basic test passed\n";
}

void testBPlusTreePersistence()
{
    cout << "\n=== Testing B+Tree Persistence ===\n";

    // First create and save a tree
    {
        remove("test_bplustree_persist.db");
        remove("test_bplustree_persist.db.wal");

        FileManager fm("test_bplustree_persist.db");
        WriteAheadLog *wal = createTestWAL("test_bplustree_persist");
        BufferManager bm(&fm, wal, 50);
        BPlusTree bptree(&bm);

        for (int i = 1; i <= 50; i++)
        {
            bptree.insert(i, i * 10);
        }

        cout << "Created tree with " << bptree.getTotalKeys() << " keys, root: "
             << bptree.getRootPageId() << "\n";

        wal->commit(0);
        delete wal;
    }

    // Now reload it
    {
        FileManager fm("test_bplustree_persist.db");
        WriteAheadLog *wal = createTestWAL("test_bplustree_persist");
        BufferManager bm(&fm, wal, 50);

        // Assuming root page is 1 (first page after metadata)
        BPlusTree bptree(&bm, 1);

        cout << "Loaded B+Tree with root: " << bptree.getRootPageId() << "\n";

        cout << "\nVerifying persisted data...\n";
        int found_count = 0;
        for (int i = 1; i <= 50; i++)
        {
            auto result = bptree.search(i);
            if (result.first)
            {
                found_count++;
                if (result.second != i * 10)
                {
                    cout << "ERROR: Key " << i << " has wrong value: "
                         << result.second << " (expected " << (i * 10) << ")\n";
                }
            }
            else
            {
                cout << "ERROR: Key " << i << " missing from persisted tree!\n";
            }
        }
        cout << "Verified " << found_count << "/50 keys\n";

        cout << "\nInserting more data...\n";
        for (int i = 51; i <= 100; i++)
        {
            bptree.insert(i, i * 10);
        }

        bptree.printTree();
        bptree.validate();

        wal->commit(0);
        delete wal;

        cout << "\n✓ B+Tree persistence test passed\n";
    }
}

void testBPlusTreeDeletion()
{
    cout << "\n=== Testing B+Tree Deletion Operations ===\n";

    remove("test_bplustree_delete.db");
    remove("test_bplustree_delete.db.wal");

    FileManager fm("test_bplustree_delete.db");
    WriteAheadLog *wal = createTestWAL("test_bplustree_delete");
    BufferManager bm(&fm, wal, 100);
    BPlusTree bptree(&bm);

    cout << "Inserting 100 keys...\n";
    for (int i = 1; i <= 100; i++)
    {
        bptree.insert(i, i * 10);
        if (i % 20 == 0)
            cout << "  Inserted " << i << " keys\n";
    }

    cout << "\nInitial tree:\n";
    bptree.printTree();
    bptree.printLeaves();

    // Test 1: Simple deletions
    cout << "\n--- Test 1: Simple deletions ---\n";
    vector<int> keys_to_delete = {50, 25, 75, 10, 90};
    for (int key : keys_to_delete)
    {
        cout << "Deleting key " << key << "... ";
        if (bptree.remove(key))
        {
            cout << "SUCCESS\n";
            // Verify deletion
            auto result = bptree.search(key);
            if (result.first)
            {
                cout << "ERROR: Key " << key << " still exists after deletion!\n";
            }
        }
        else
        {
            cout << "FAILED (key not found)\n";
        }
    }

    // Test 2: Verify remaining keys
    cout << "\n--- Test 2: Verifying remaining keys ---\n";
    int found_count = 0;
    for (int i = 1; i <= 100; i++)
    {
        auto result = bptree.search(i);
        if (result.first)
        {
            found_count++;
            if (result.second != i * 10)
            {
                cout << "ERROR: Key " << i << " has wrong value: "
                     << result.second << " (expected " << (i * 10) << ")\n";
            }
        }
    }
    cout << "Found " << found_count << "/" << (100 - keys_to_delete.size())
         << " expected keys after deletion\n";

    // Test 3: Range queries after deletions
    cout << "\n--- Test 3: Range queries after deletions ---\n";

    // Range that includes deleted keys
    auto range1 = bptree.rangeQuery(20, 80);
    cout << "Range query 20-80 found " << range1.size() << " keys\n";

    // Range that doesn't include deleted keys
    auto range2 = bptree.rangeQuery(1, 9);
    cout << "Range query 1-9 found " << range2.size() << " keys\n";

    // Full range
    auto range3 = bptree.rangeQuery(1, 100);
    cout << "Range query 1-100 found " << range3.size() << " keys (should be "
         << (100 - keys_to_delete.size()) << ")\n";

    // Test 4: Sequential deletions
    cout << "\n--- Test 4: Sequential deletions ---\n";
    int seq_deleted = 0;
    for (int i = 1; i <= 30; i++)
    {
        if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
        {
            if (bptree.remove(i))
            {
                seq_deleted++;
                if (seq_deleted % 10 == 0)
                {
                    cout << "Deleted " << seq_deleted << " sequential keys\n";
                }
            }
        }
    }

    cout << "\nTree after sequential deletions:\n";
    bptree.printLeaves();

    // Test 5: Re-insert with new values
    cout << "\n--- Test 5: Re-inserting deleted keys ---\n";
    for (int i = 1; i <= 30; i++)
    {
        if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
        {
            bptree.insert(i, i * 100); // New value
        }
    }

    // Verify new values
    cout << "\nVerifying re-inserted keys have new values...\n";
    for (int i = 1; i <= 30; i++)
    {
        if (find(keys_to_delete.begin(), keys_to_delete.end(), i) == keys_to_delete.end())
        {
            auto result = bptree.search(i);
            if (result.first)
            {
                if (result.second != i * 100)
                {
                    cout << "ERROR: Key " << i << " has value " << result.second
                         << " (expected " << (i * 100) << ")\n";
                }
            }
        }
    }

    bptree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B+Tree deletion test passed\n";
}

void testBPlusTreeRangeQueries()
{
    cout << "\n=== Testing B+Tree Range Queries ===\n";

    remove("test_bplustree_range.db");
    remove("test_bplustree_range.db.wal");

    FileManager fm("test_bplustree_range.db");
    WriteAheadLog *wal = createTestWAL("test_bplustree_range");
    BufferManager bm(&fm, wal, 100);
    BPlusTree bptree(&bm);

    // Insert keys in a specific pattern for range testing
    vector<int> keys = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
                        15, 25, 35, 45, 55, 65, 75, 85, 95};

    cout << "Inserting " << keys.size() << " keys...\n";
    for (int key : keys)
    {
        bptree.insert(key, key * 10);
    }

    bptree.printLeaves();

    // Test various range queries
    struct RangeTest
    {
        int start;
        int end;
        string description;
    };

    vector<RangeTest> tests = {
        {20, 80, "Middle range"},
        {5, 15, "Start of range"},
        {90, 110, "End of range"},
        {1000, 2000, "Non-existent range"},
        {0, 1000, "Full range"},
        {45, 45, "Single key range"},
        {60, 40, "Invalid range (start > end)"}};

    int passed = 0;
    for (const auto &test : tests)
    {
        cout << "\nTest: " << test.description << " ("
             << test.start << "-" << test.end << ")\n";

        if (test.start > test.end)
        {
            auto range = bptree.rangeQuery(test.start, test.end);
            if (range.empty())
            {
                cout << "  ✓ Correctly returned empty range for invalid query\n";
                passed++;
            }
            continue;
        }

        auto range = bptree.rangeQuery(test.start, test.end);

        // Count expected keys
        int expected_count = 0;
        for (int key : keys)
        {
            if (key >= test.start && key <= test.end)
            {
                expected_count++;
            }
        }

        cout << "  Found " << range.size() << " keys, expected " << expected_count << "\n";

        if (range.size() == expected_count)
        {
            // Verify keys are in range and sorted
            bool sorted = true;
            bool in_range = true;
            for (size_t i = 0; i < range.size(); i++)
            {
                if (range[i].first < test.start || range[i].first > test.end)
                {
                    in_range = false;
                    break;
                }
                if (i > 0 && range[i].first <= range[i - 1].first)
                {
                    sorted = false;
                    break;
                }
            }

            if (sorted && in_range)
            {
                cout << "  ✓ Range query correct\n";
                passed++;
            }
            else
            {
                cout << "  ✗ Range query failed: ";
                if (!sorted)
                    cout << "keys not sorted";
                if (!in_range)
                    cout << "keys out of range";
                cout << "\n";
            }
        }
        else
        {
            cout << "  ✗ Wrong number of keys\n";
        }
    }

    cout << "\nRange query tests: " << passed << "/" << tests.size() << " passed\n";

    // Test sequential range queries (follow leaf chain)
    cout << "\n--- Testing sequential access via range queries ---\n";

    // Query in chunks to test leaf chain traversal
    int chunk_size = 20;
    int total_from_chunks = 0;

    for (int start = 0; start <= 100; start += chunk_size)
    {
        int end = start + chunk_size - 1;
        auto range = bptree.rangeQuery(start, end);
        total_from_chunks += range.size();

        if (!range.empty())
        {
            cout << "  Range " << start << "-" << end << ": "
                 << range.size() << " keys\n";
        }
    }

    // Compare with direct count
    auto full_range = bptree.rangeQuery(0, 1000);
    if (total_from_chunks == full_range.size())
    {
        cout << "✓ Sequential range queries match direct query\n";
    }
    else
    {
        cout << "✗ Sequential range queries don't match: "
             << total_from_chunks << " vs " << full_range.size() << "\n";
    }

    bptree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B+Tree range query test passed\n";
}

void testBPlusTreeLargeScale()
{
    cout << "\n=== Testing B+Tree Large Scale Operations ===\n";

    remove("test_bplustree_large.db");
    remove("test_bplustree_large.db.wal");

    FileManager fm("test_bplustree_large.db");
    WriteAheadLog *wal = createTestWAL("test_bplustree_large");
    BufferManager bm(&fm, wal, 500); // Large buffer
    BPlusTree bptree(&bm);

    const int TOTAL_KEYS = 5000;

    cout << "Inserting " << TOTAL_KEYS << " keys...\n";

    auto insert_start = high_resolution_clock::now();
    for (int i = 1; i <= TOTAL_KEYS; i++)
    {
        bptree.insert(i, i * 10);
        if (i % 500 == 0)
        {
            cout << "  Inserted " << i << " keys\n";
        }
    }
    auto insert_end = high_resolution_clock::now();

    auto insert_duration = duration_cast<milliseconds>(insert_end - insert_start);
    cout << "Insertion completed in " << insert_duration.count() << " ms ("
         << (TOTAL_KEYS * 1000.0 / insert_duration.count()) << " ops/sec)\n";

    cout << "\nTree statistics:\n";
    cout << "  Height: " << bptree.getHeight() << "\n";
    cout << "  Total keys: " << bptree.getTotalKeys() << "\n";

    // Test search performance
    cout << "\nTesting search performance...\n";

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dist(1, TOTAL_KEYS);

    const int SEARCH_COUNT = 1000;
    int found_count = 0;

    auto search_start = high_resolution_clock::now();
    for (int i = 0; i < SEARCH_COUNT; i++)
    {
        int key = dist(gen);
        auto result = bptree.search(key);
        if (result.first && result.second == key * 10)
        {
            found_count++;
        }
    }
    auto search_end = high_resolution_clock::now();

    auto search_duration = duration_cast<milliseconds>(search_end - search_start);
    cout << "Searched " << SEARCH_COUNT << " random keys in "
         << search_duration.count() << " ms ("
         << (SEARCH_COUNT * 1000.0 / search_duration.count()) << " ops/sec)\n";
    cout << "  Found " << found_count << "/" << SEARCH_COUNT << " keys correctly\n";

    // Test range query performance
    cout << "\nTesting range query performance...\n";

    const int RANGE_COUNT = 100;
    int total_range_keys = 0;

    auto range_start = high_resolution_clock::now();
    for (int i = 0; i < RANGE_COUNT; i++)
    {
        int start = dist(gen);
        int end = start + 100; // Fixed size range

        auto range = bptree.rangeQuery(start, end);
        total_range_keys += range.size();
    }
    auto range_end = high_resolution_clock::now();

    auto range_duration = duration_cast<milliseconds>(range_end - range_start);
    cout << "Executed " << RANGE_COUNT << " range queries in "
         << range_duration.count() << " ms\n";
    cout << "  Average keys per range: " << (total_range_keys / (double)RANGE_COUNT) << "\n";
    cout << "  Average time per range: " << (range_duration.count() / (double)RANGE_COUNT) << " ms\n";

    // Test mixed operations
    cout << "\nTesting mixed operations...\n";

    const int MIXED_OPS = 2000;
    int inserts = 0, deletes = 0, searches = 0;

    auto mixed_start = high_resolution_clock::now();
    for (int i = 0; i < MIXED_OPS; i++)
    {
        int op = i % 3;
        int key = dist(gen);

        switch (op)
        {
        case 0:                           // Insert (or update)
            bptree.insert(key, key * 20); // New value
            inserts++;
            break;

        case 1: // Delete
            bptree.remove(key);
            deletes++;
            break;

        case 2: // Search
            bptree.search(key);
            searches++;
            break;
        }
    }
    auto mixed_end = high_resolution_clock::now();

    auto mixed_duration = duration_cast<milliseconds>(mixed_end - mixed_start);
    cout << "Mixed operations completed in " << mixed_duration.count() << " ms\n";
    cout << "  Inserts/updates: " << inserts << "\n";
    cout << "  Deletes: " << deletes << "\n";
    cout << "  Searches: " << searches << "\n";
    cout << "  Operations per second: "
         << (MIXED_OPS * 1000.0 / mixed_duration.count()) << "\n";

    // Final tree state
    cout << "\nFinal tree state:\n";
    cout << "  Height: " << bptree.getHeight() << "\n";
    cout << "  Total keys: " << bptree.getTotalKeys() << "\n";

    // Quick validation
    bptree.validate();

    wal->commit(0);
    delete wal;

    cout << "\n✓ B+Tree large scale test passed\n";
}

void testBPlusTreeEdgeCases()
{
    cout << "\n=== Testing B+Tree Edge Cases ===\n";

    remove("test_bplustree_edge.db");
    remove("test_bplustree_edge.db.wal");

    FileManager fm("test_bplustree_edge.db");
    WriteAheadLog *wal = createTestWAL("test_bplustree_edge");
    BufferManager bm(&fm, wal, 50);
    BPlusTree bptree(&bm);

    // Edge case 1: Empty tree operations
    cout << "\nEdge case 1: Empty tree operations...\n";

    // Search in empty tree
    auto result = bptree.search(1);
    if (!result.first)
    {
        cout << "✓ Search in empty tree correctly returns false\n";
    }

    // Range query in empty tree
    auto range = bptree.rangeQuery(1, 10);
    if (range.empty())
    {
        cout << "✓ Range query in empty tree correctly returns empty\n";
    }

    // Delete from empty tree
    if (!bptree.remove(1))
    {
        cout << "✓ Delete from empty tree correctly returns false\n";
    }

    // Edge case 2: Single key tree
    cout << "\nEdge case 2: Single key tree...\n";
    bptree.insert(999, 9990);

    cout << "Single key tree:\n";
    bptree.printTree();

    // Verify operations
    result = bptree.search(999);
    if (result.first && result.second == 9990)
    {
        cout << "✓ Search in single key tree works\n";
    }

    range = bptree.rangeQuery(1, 1000);
    if (range.size() == 1 && range[0].first == 999)
    {
        cout << "✓ Range query in single key tree works\n";
    }

    // Delete the single key
    if (bptree.remove(999))
    {
        cout << "✓ Delete single key works\n";
    }

    // Tree should be empty again
    result = bptree.search(999);
    if (!result.first)
    {
        cout << "✓ Tree correctly empty after deleting single key\n";
    }

    // Edge case 3: Duplicate keys (should update value)
    cout << "\nEdge case 3: Duplicate key insertion...\n";

    bptree.insert(100, 1000);
    bptree.insert(100, 2000); // Should update value

    result = bptree.search(100);
    if (result.first && result.second == 2000)
    {
        cout << "✓ Duplicate key correctly updates value\n";
    }

    // Edge case 4: Delete and re-insert same key
    cout << "\nEdge case 4: Delete and re-insert same key...\n";

    bptree.remove(100);
    result = bptree.search(100);
    if (!result.first)
    {
        cout << "✓ Key correctly deleted\n";
    }

    bptree.insert(100, 3000); // New value
    result = bptree.search(100);
    if (result.first && result.second == 3000)
    {
        cout << "✓ Key correctly re-inserted with new value\n";
    }

    // Edge case 5: Leaf chain integrity after many operations
    cout << "\nEdge case 5: Leaf chain integrity...\n";

    // Insert keys in reverse order
    for (int i = 10; i >= 1; i--)
    {
        bptree.insert(i, i * 10);
    }

    cout << "Tree after reverse insertion:\n";
    bptree.printLeaves();

    // Verify leaf chain is sorted
    bool leaf_chain_ok = true;
    auto leaves_range = bptree.rangeQuery(1, 10);
    for (size_t i = 1; i < leaves_range.size(); i++)
    {
        if (leaves_range[i].first <= leaves_range[i - 1].first)
        {
            leaf_chain_ok = false;
            cout << "✗ Leaf chain not sorted at position " << i << "\n";
            break;
        }
    }

    if (leaf_chain_ok)
    {
        cout << "✓ Leaf chain maintains sorted order\n";
    }

    // Edge case 6: Operations near boundaries
    cout << "\nEdge case 6: Boundary operations...\n";

    // Test with very large key
    bptree.insert(INT64_MAX - 1, 99999);
    result = bptree.search(INT64_MAX - 1);
    if (result.first)
    {
        cout << "✓ Can handle large keys\n";
    }

    // Test with very small key
    bptree.insert(INT64_MIN + 1, -99999);
    result = bptree.search(INT64_MIN + 1);
    if (result.first)
    {
        cout << "✓ Can handle small keys\n";
    }

    // Edge case 7: Stress leaf splits/merges
    cout << "\nEdge case 7: Stress leaf splits and merges...\n";

    // Clear tree
    remove("test_bplustree_edge2.db");
    remove("test_bplustree_edge2.db.wal");

    FileManager fm2("test_bplustree_edge2.db");
    WriteAheadLog *wal2 = createTestWAL("test_bplustree_edge2");
    BufferManager bm2(&fm2, wal2, 30); // Small buffer to force splits
    BPlusTree bptree2(&bm2);

    // Insert many keys to force splits
    for (int i = 1; i <= 200; i++)
    {
        bptree2.insert(i, i * 10);
    }

    cout << "Tree after many inserts (should have multiple leaves):\n";
    bptree2.printLeaves();
    cout << "Tree height: " << bptree2.getHeight() << "\n";

    // Delete many keys to force merges
    for (int i = 1; i <= 150; i++)
    {
        bptree2.remove(i);
    }

    cout << "Tree after many deletes (should have fewer leaves):\n";
    bptree2.printLeaves();
    cout << "Tree height: " << bptree2.getHeight() << "\n";

    bptree2.validate();

    wal2->commit(0);
    delete wal2;

    wal->commit(0);
    delete wal;

    cout << "\n✓ B+Tree edge cases test passed\n";
}

void testBPlusTreeComparison()
{
    cout << "\n=== Comparing B-Tree and B+Tree Performance ===\n";

    const int NUM_KEYS = 1000;
    const int NUM_SEARCHES = 100;
    const int NUM_RANGES = 50;

    // Test B-Tree
    cout << "\n--- B-Tree Performance ---\n";
    {
        remove("test_comparison_btree.db");
        remove("test_comparison_btree.db.wal");

        FileManager fm("test_comparison_btree.db");
        WriteAheadLog *wal = createTestWAL("test_comparison_btree");
        BufferManager bm(&fm, wal, 100);
        BTree btree(&bm);

        auto start = high_resolution_clock::now();

        // Insert
        for (int i = 1; i <= NUM_KEYS; i++)
        {
            btree.insert(i, i * 10);
        }
        auto insert_end = high_resolution_clock::now();

        // Search
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dist(1, NUM_KEYS);

        for (int i = 0; i < NUM_SEARCHES; i++)
        {
            btree.search(dist(gen));
        }
        auto search_end = high_resolution_clock::now();

        auto insert_time = duration_cast<microseconds>(insert_end - start);
        auto search_time = duration_cast<microseconds>(search_end - insert_end);

        cout << "Insert " << NUM_KEYS << " keys: " << insert_time.count() << " μs\n";
        cout << "Search " << NUM_SEARCHES << " keys: " << search_time.count() << " μs\n";
        cout << "Tree height: " << btree.getHeight() << "\n";
        cout << "Total keys: " << btree.getTotalKeys() << "\n";

        wal->commit(0);
        delete wal;
    }

    // Test B+Tree
    cout << "\n--- B+Tree Performance ---\n";
    {
        remove("test_comparison_bplustree.db");
        remove("test_comparison_bplustree.db.wal");

        FileManager fm("test_comparison_bplustree.db");
        WriteAheadLog *wal = createTestWAL("test_comparison_bplustree");
        BufferManager bm(&fm, wal, 100);
        BPlusTree bptree(&bm);

        auto start = high_resolution_clock::now();

        // Insert
        for (int i = 1; i <= NUM_KEYS; i++)
        {
            bptree.insert(i, i * 10);
        }
        auto insert_end = high_resolution_clock::now();

        // Search
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dist(1, NUM_KEYS);

        for (int i = 0; i < NUM_SEARCHES; i++)
        {
            bptree.search(dist(gen));
        }
        auto search_end = high_resolution_clock::now();

        // Range queries (B+Tree advantage)
        int range_total = 0;
        auto range_start = high_resolution_clock::now();
        for (int i = 0; i < NUM_RANGES; i++)
        {
            int start = dist(gen);
            int end = start + 10;
            auto range = bptree.rangeQuery(start, end);
            range_total += range.size();
        }
        auto range_end = high_resolution_clock::now();

        auto insert_time = duration_cast<microseconds>(insert_end - start);
        auto search_time = duration_cast<microseconds>(search_end - insert_end);
        auto range_time = duration_cast<microseconds>(range_end - range_start);

        cout << "Insert " << NUM_KEYS << " keys: " << insert_time.count() << " μs\n";
        cout << "Search " << NUM_SEARCHES << " keys: " << search_time.count() << " μs\n";
        cout << "Range queries " << NUM_RANGES << " ranges: " << range_time.count() << " μs\n";
        cout << "Tree height: " << bptree.getHeight() << "\n";
        cout << "Total keys: " << bptree.getTotalKeys() << "\n";
        cout << "Average keys per range: " << (range_total / (double)NUM_RANGES) << "\n";

        wal->commit(0);
        delete wal;
    }

    cout << "\n✓ B-Tree vs B+Tree comparison completed\n";
}

// Update main function to include B+Tree tests
int main()
{
    try
    {
        cout << "=== COMPREHENSIVE B-Tree & B+Tree System Tests ===\n";

        cout << "\n========== B-TREE TESTS ==========\n";
        testBTreeBasic();
        testBTreePersistence();
        testBTreeDeletion();
        testBTreeLargeWithDeletion();
        testBTreeEdgeCases();
        testBTreeStress();

        cout << "\n\n========== B+Tree TESTS ==========\n";
        testBPlusTreeBasic();
        testBPlusTreePersistence();
        testBPlusTreeDeletion();
        testBPlusTreeRangeQueries();
        testBPlusTreeLargeScale();
        testBPlusTreeEdgeCases();
        testBPlusTreeComparison();

        cout << "\n=== ALL B-TREE & B+Tree TESTS PASSED SUCCESSFULLY ===\n";
        return 0;
    }
    catch (const exception &e)
    {
        cerr << "\n❌ TEST FAILED: " << e.what() << endl;
        return 1;
    }
}