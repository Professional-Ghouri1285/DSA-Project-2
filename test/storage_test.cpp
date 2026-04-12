// trading_platform/test/storage_test.cpp
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include "../storage/FileManager.h"
#include "../storage/Page.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h" // ADD THIS

using namespace std;

// Test FileManager with the correct API
void testFileManager()
{
    cout << "=== Testing FileManager ===\n";

    // Remove old test file
    remove("test_filemanager.db");

    // Create FileManager (constructor opens the file)
    FileManager fm("test_filemanager.db");

    // Test 1: Check file was created and opened
    assert(fm.isOpen() && "File should be open");
    cout << "File opened successfully\n";

    // Test 2: Read metadata
    DatabaseMetadata metadata;
    bool metadata_read = fm.readMetadata(metadata);
    assert(metadata_read && "Should read metadata");

    // Check magic number
    cout << "Magic number: 0x" << hex << metadata.magic << dec << "\n";
    assert(metadata.magic == MAGIC_NUMBER && "Magic number should match");
    cout << "Magic number OK\n";

    // Test 3: Get total pages
    int total_pages = fm.getTotalPages();
    cout << "Total pages: " << total_pages << "\n";
    assert(total_pages >= 1 && "Should have at least metadata page");

    // Test 4: Write and read a page
    int test_page_id = 1;

    // Create a test page
    Page test_page(test_page_id);
    test_page.setPageType(PAGE_TYPE_DATA);
    test_page.setNumKeys(3);

    // Fill with test data
    test_page.setKey(0, 100);
    test_page.setKey(1, 200);
    test_page.setKey(2, 300);

    // Serialize to buffer
    char write_buffer[PAGE_SIZE];
    test_page.serialize(write_buffer);

    // Write page
    bool write_ok = fm.writePage(test_page_id, write_buffer);
    assert(write_ok && "Should write page");
    cout << "Wrote page " << test_page_id << "\n";

    // Read back
    char read_buffer[PAGE_SIZE];
    bool read_ok = fm.readPage(test_page_id, read_buffer);
    assert(read_ok && "Should read page");

    // Deserialize
    Page read_page;
    read_page.deserialize(read_buffer);

    // Verify
    assert(read_page.getPageId() == test_page_id);
    assert(read_page.getNumKeys() == 3);
    assert(read_page.getKey(0) == 100);
    assert(read_page.getKey(2) == 300);

    cout << "✓ FileManager basic operations OK\n";

    // Test 5: Allocate new pages
    cout << "\nTesting page allocation...\n";

    vector<int> allocated_pages;
    for (int i = 0; i < 5; i++)
    {
        int page_id = fm.allocatePage();
        assert(page_id > 0 && "Should allocate valid page ID");
        allocated_pages.push_back(page_id);
        cout << "Allocated page: " << page_id << "\n";
    }

    // Verify total pages increased
    int new_total = fm.getTotalPages();
    cout << "Total pages after allocation: " << new_total << "\n";
    assert(new_total >= total_pages + 5);

    // Test 6: Free some pages
    cout << "\nTesting page freeing...\n";
    for (int i = 0; i < 2; i++)
    {
        bool freed = fm.freePage(allocated_pages[i]);
        assert(freed && "Should free page");
        cout << "Freed page: " << allocated_pages[i] << "\n";
    }

    // Test 7: Print metadata and free list
    fm.printMetadata();
    fm.printFreeList();

    cout << "✓ FileManager complete test OK\n";
}

void testPageSerialization()
{
    cout << "\n=== Testing Page Serialization ===\n";

    // Test 1: Leaf page
    cout << "Testing leaf page...\n";
    Page leaf_page(123, PAGE_TYPE_BT_LEAF);
    leaf_page.getHeader().is_leaf = 1;
    leaf_page.setNumKeys(5);

    // Set some keys and values
    for (int i = 0; i < 5; i++)
    {
        leaf_page.setKey(i, i * 100);
        leaf_page.setValue(i, i * 1000);
    }

    // Serialize
    char buffer[PAGE_SIZE];
    leaf_page.serialize(buffer);

    // Deserialize to new page
    Page leaf_page2;
    leaf_page2.deserialize(buffer);

    // Verify
    assert(leaf_page2.getPageId() == 123);
    assert(leaf_page2.getPageType() == PAGE_TYPE_BT_LEAF);
    assert(leaf_page2.isLeaf());
    assert(leaf_page2.getNumKeys() == 5);

    for (int i = 0; i < 5; i++)
    {
        assert(leaf_page2.getKey(i) == i * 100);
        assert(leaf_page2.getValue(i) == i * 1000);
    }
    cout << "✓ Leaf page serialization OK\n";

    // Test 2: Internal page
    cout << "\nTesting internal page...\n";
    Page internal_page(456, PAGE_TYPE_BT_INTERNAL);
    internal_page.getHeader().is_leaf = 0;
    internal_page.setNumKeys(3);

    // Set keys and children
    for (int i = 0; i < 3; i++)
    {
        internal_page.setKey(i, (i + 1) * 50);
    }
    for (int i = 0; i <= 3; i++)
    { // n+1 children
        internal_page.setChild(i, i * 10);
    }

    // Serialize and deserialize
    internal_page.serialize(buffer);
    Page internal_page2;
    internal_page2.deserialize(buffer);

    // Verify
    assert(internal_page2.getPageId() == 456);
    assert(!internal_page2.isLeaf());
    assert(internal_page2.getNumKeys() == 3);

    for (int i = 0; i < 3; i++)
    {
        assert(internal_page2.getKey(i) == (i + 1) * 50);
    }
    for (int i = 0; i <= 3; i++)
    {
        assert(internal_page2.getChild(i) == i * 10);
    }
    cout << "✓ Internal page serialization OK\n";

    // Test 3: Free list page
    cout << "\nTesting free list page...\n";
    Page free_page(789, PAGE_TYPE_FREE_LIST);
    free_page.setNumKeys(0);

    // Add some free page entries
    free_page.addFreeEntry(10);
    free_page.addFreeEntry(20);
    free_page.addFreeEntry(30);

    assert(free_page.getNumKeys() == 3);
    assert(free_page.getFreeEntry(0) == 10);
    assert(free_page.getFreeEntry(1) == 20);
    assert(free_page.getFreeEntry(2) == 30);

    // Test pop
    int popped = free_page.popFreeEntry();
    assert(popped == 30);
    assert(free_page.getNumKeys() == 2);

    free_page.serialize(buffer);
    Page free_page2;
    free_page2.deserialize(buffer);

    assert(free_page2.getNumKeys() == 2);
    assert(free_page2.getFreeEntry(0) == 10);
    assert(free_page2.getFreeEntry(1) == 20);

    cout << "✓ Free list page serialization OK\n";
}

void testBufferManager()
{
    cout << "\n=== Testing BufferManager ===\n";

    // Clean up old file
    remove("test_buffer.db");

    FileManager fm("test_buffer.db");
    BufferManager bm(&fm, nullptr, 5); // Cache size 5

    cout << "Cache capacity: " << bm.getCapacity() << "\n";

    // Test 1: Allocate pages through buffer manager
    cout << "\nTesting page allocation...\n";
    vector<int> page_ids;

    for (int i = 0; i < 7; i++)
    { // More than cache size
        Page *page = bm.allocatePage();
        assert(page != nullptr);

        page->setPageType(PAGE_TYPE_DATA);
        page->setNumKeys(2);
        page->setKey(0, i * 100);
        page->setKey(1, i * 100 + 1);

        if (page->isLeaf())
        {
            page->setValue(0, i * 1000);
            page->setValue(1, i * 1000 + 1);
        }

        bm.markDirty(page->getPageId());
        page_ids.push_back(page->getPageId());

        cout << "Allocated page " << page->getPageId()
             << " (cache size: " << bm.getCacheSize() << ")\n";

        // Unpin some pages to allow eviction
        if (i % 2 == 0)
        {
            bm.unpinPage(page->getPageId());
        }
    }

    bm.printStats();

    // Test 2: Read pages back (some will cause evictions)
    cout << "\nReading pages back...\n";
    for (int i = 0; i < 7; i++)
    {
        Page *page = bm.getPage(page_ids[i]);
        assert(page != nullptr);

        // Verify data
        assert(page->getNumKeys() == 2);
        assert(page->getKey(0) == i * 100);
        assert(page->getKey(1) == i * 100 + 1);

        bm.unpinPage(page_ids[i]);
        cout << "Read page " << page_ids[i] << " OK\n";
    }

    // Test 3: Test flushing
    cout << "\nTesting flush...\n";
    size_t dirty_before = bm.getDirtyCount();
    cout << "Dirty pages before flush: " << dirty_before << "\n";

    bm.flushAll();

    size_t dirty_after = bm.getDirtyCount();
    cout << "Dirty pages after flush: " << dirty_after << "\n";
    assert(dirty_after == 0);

    // Test 4: Test freeing pages
    cout << "\nTesting page freeing...\n";
    for (int i = 0; i < 2; i++)
    {
        bool freed = bm.freePage(page_ids[i]);
        assert(freed);
        cout << "Freed page " << page_ids[i] << "\n";
    }

    bm.printStats();

    cout << "✓ BufferManager test OK\n";
}

void testPageOperations()
{
    cout << "\n=== Testing Page Operations ===\n";
    cout << "Starting page operations test...\n"; // ADD THIS

    // Test leaf page
    Page leaf_page(999, PAGE_TYPE_BP_LEAF);
    leaf_page.getHeader().is_leaf = 1;

    cout << "Created leaf page\n"; // ADD THIS

    // Check capacities
    int max_leaf_keys = leaf_page.getMaxLeafKeys();
    int max_internal_keys = leaf_page.getMaxInternalKeys();

    cout << "Max leaf keys: " << max_leaf_keys << "\n";
    cout << "Max internal keys: " << max_internal_keys << "\n";

    assert(max_leaf_keys > 0);
    assert(max_internal_keys > 0);
    assert(max_leaf_keys != max_internal_keys); // Should be different

    cout << "Capacities checked\n"; // ADD THIS

    // Test adding keys to leaf
    leaf_page.setNumKeys(3);
    for (int i = 0; i < 3; i++)
    {
        leaf_page.setKey(i, 1000 + i);
        leaf_page.setValue(i, 10000 + i);
    }

    cout << "Keys added to leaf\n"; // ADD THIS

    // Verify
    assert(leaf_page.getNumKeys() == 3);
    assert(leaf_page.getKey(0) == 1000);
    assert(leaf_page.getValue(0) == 10000);
    assert(leaf_page.getKey(2) == 1002);
    assert(leaf_page.getValue(2) == 10002);

    cout << "Leaf page verified\n"; // ADD THIS

    // Test bounds checking
    bool caught = false;
    try
    {
        leaf_page.setKey(max_leaf_keys, 9999);            // Should throw
        cout << "ERROR: Should have thrown exception!\n"; // ADD THIS
    }
    catch (const out_of_range &)
    {
        caught = true;
        cout << "Caught out_of_range exception (expected)\n"; // ADD THIS
    }
    assert(caught && "Should throw out_of_range for invalid index");

    cout << "Bounds checking passed\n"; // ADD THIS

    // Test internal page
    Page internal_page(888, PAGE_TYPE_BP_INTERNAL);
    internal_page.getHeader().is_leaf = 0;

    cout << "Created internal page\n"; // ADD THIS

    internal_page.setNumKeys(2);
    internal_page.setKey(0, 500);
    internal_page.setKey(1, 600);
    internal_page.setChild(0, 100);
    internal_page.setChild(1, 200);
    internal_page.setChild(2, 300); // n+1 children

    assert(internal_page.getChild(0) == 100);
    assert(internal_page.getChild(2) == 300);

    cout << "Internal page operations passed\n"; // ADD THIS

    // Print debug info
    cout << "\nLeaf page debug info:\n";
    leaf_page.printDebug();

    cout << "\nInternal page debug info:\n";
    internal_page.printDebug();

    cout << "✓ Page operations test OK\n";
}

void testEdgeCases()
{
    cout << "\n=== Testing Edge Cases ===\n";

    // Test 1: Page 0 operations (metadata page)
    cout << "Testing page 0 (metadata)...\n";

    FileManager fm("test_edge.db");
    BufferManager bm(&fm, nullptr, 3);

    Page *metadata_page = bm.getPage(0);
    assert(metadata_page != nullptr);

    // Page 0 should have valid metadata
    DatabaseMetadata metadata;
    memcpy(&metadata, metadata_page->getData(), sizeof(DatabaseMetadata));
    assert(metadata.magic == MAGIC_NUMBER);

    bm.unpinPage(0);
    cout << "✓ Page 0 operations OK\n";

    // Test 2: Large number of pages
    cout << "\nTesting large page allocation...\n";

    vector<int> large_pages;
    for (int i = 0; i < 20; i++)
    {
        Page *page = bm.allocatePage();
        assert(page != nullptr);

        // Fill with pattern
        page->setNumKeys(1);
        page->setKey(0, i * 1000);
        bm.markDirty(page->getPageId());
        bm.unpinPage(page->getPageId());

        large_pages.push_back(page->getPageId());
    }

    // Read them all back (will cause many evictions)
    for (int page_id : large_pages)
    {
        Page *page = bm.getPage(page_id);
        assert(page != nullptr);
        assert(page->getKey(0) >= 0);
        bm.unpinPage(page_id);
    }

    bm.flushAll();
    cout << "✓ Large page allocation OK\n";

    // Test 3: Free and reuse
    cout << "\nTesting free and reuse...\n";

    // Free some pages
    for (int i = 0; i < 5; i++)
    {
        bm.freePage(large_pages[i]);
    }

    // Allocate new pages (should reuse freed ones)
    vector<int> reused_pages;
    for (int i = 0; i < 3; i++)
    {
        Page *page = bm.allocatePage();
        assert(page != nullptr);
        reused_pages.push_back(page->getPageId());
        bm.unpinPage(page->getPageId());
    }

    // Check that some page IDs were reused
    bool found_reuse = false;
    for (int reused : reused_pages)
    {
        for (int freed : large_pages)
        {
            if (reused == freed)
            {
                found_reuse = true;
                break;
            }
        }
        if (found_reuse)
            break;
    }

    assert(found_reuse && "Should reuse freed pages");
    cout << "✓ Free and reuse OK\n";

    bm.printStats();
}

void testWALBasic()
{
    cout << "\n=== Testing WriteAheadLog Basic Operations ===\n";

    // Clean up old files
    remove("test_wal_basic.log");

    // Create WAL
    WriteAheadLog wal("test_wal_basic.log");

    assert(wal.getLastLSN() >= 0);
    cout << "Initial LSN: " << wal.getLastLSN() << "\n";

    // Test 1: Append simple records
    cout << "\nTest 1: Appending log records...\n";

    vector<int64_t> lsns;
    for (int i = 1; i <= 5; i++)
    {
        char data[32];
        sprintf(data, "Test data %d", i);

        int64_t lsn = wal.append(LOG_INSERT, i, data, strlen(data) + 1);
        lsns.push_back(lsn);

        cout << "Appended LSN " << lsn
             << " for page " << i
             << " (data: " << data << ")\n";

        assert(lsn > 0);
        assert(wal.getLastLSN() == lsn);
    }

    // Test 2: Commit
    cout << "\nTest 2: Commit operation...\n";
    wal.commit(lsns.back());
    cout << "Commit logged\n";

    // Test 3: Checkpoint
    cout << "\nTest 3: Checkpoint operation...\n";
    wal.checkpoint();
    cout << "Checkpoint created\n";

    // Test 4: Replay (should show all records)
    cout << "\nTest 4: Replay WAL...\n";
    wal.replay();

    cout << "✓ WAL basic operations OK\n";
}

void testWALWithBufferManager()
{
    cout << "\n=== Testing WAL Integration with BufferManager ===\n";

    // Clean up old files
    remove("test_wal_integration.db");
    remove("test_wal_integration.log");

    // Create WAL and BufferManager
    WriteAheadLog wal("test_wal_integration.log");
    FileManager fm("test_wal_integration.db");
    BufferManager bm(&fm, &wal, 10); // Note: Added WAL parameter

    // Test 1: Transaction with page modifications
    cout << "\nTest 1: Transaction with page modifications...\n";

    bm.beginTransaction();

    // Allocate and modify pages in transaction
    vector<int> page_ids;
    for (int i = 0; i < 3; i++)
    {
        Page *page = bm.allocatePage();
        assert(page != nullptr);

        page->setPageType(PAGE_TYPE_DATA);
        page->setNumKeys(2);
        page->setKey(0, 100 + i);
        page->setKey(1, 200 + i);

        bm.markDirty(page->getPageId());
        page_ids.push_back(page->getPageId());

        cout << "Modified page " << page->getPageId() << " in transaction\n";
        bm.unpinPage(page->getPageId());
    }

    bm.commitTransaction();
    cout << "Transaction committed\n";

    // Test 2: Non-transactional modifications
    cout << "\nTest 2: Non-transactional modifications...\n";

    Page *page4 = bm.allocatePage();
    page4->setPageType(PAGE_TYPE_DATA);
    page4->setNumKeys(1);
    page4->setKey(0, 999);
    bm.markDirty(page4->getPageId());
    bm.unpinPage(page4->getPageId());

    cout << "Modified page " << page4->getPageId() << " outside transaction\n";

    // Flush all to ensure WAL records are written
    bm.flushAll();

    // Test 3: Check WAL file size
    ifstream wal_file("test_wal_integration.log", ios::binary | ios::ate);
    streampos file_size = wal_file.tellg();
    wal_file.close();

    cout << "\nWAL file size: " << file_size << " bytes\n";
    assert(file_size > 0 && "WAL file should not be empty");

    // Test 4: Replay from WAL
    cout << "\nTest 4: Simulating crash and recovery...\n";

    // Simulate crash by destroying objects
    wal_file.close();

    // Create new WAL for recovery
    WriteAheadLog wal_recover("test_wal_integration.log");

    cout << "Replaying WAL...\n";
    wal_recover.replay();

    cout << "✓ WAL-BufferManager integration test OK\n";
}

void testWALCrashRecovery()
{
    cout << "\n=== Testing WAL Crash Recovery ===\n";

    // Clean up old files
    remove("test_crash_recovery.db");
    remove("test_crash_recovery.log");

    // Phase 1: Normal operation with crash in middle
    cout << "\nPhase 1: Normal operation (simulating user activity)...\n";

    {
        WriteAheadLog wal("test_crash_recovery.log");
        FileManager fm("test_crash_recovery.db");
        BufferManager bm(&fm, &wal, 15);

        // Start transaction 1
        bm.beginTransaction();

        Page *page1 = bm.allocatePage();
        page1->setPageType(PAGE_TYPE_BP_LEAF); // Use BP_LEAF
        page1->getHeader().is_leaf = 1;        // Explicitly mark as leaf
        page1->setNumKeys(3);
        for (int i = 0; i < 3; i++)
        {
            page1->setKey(i, 1000 + i);
            // setValue should work for leaf pages
            page1->setValue(i, 10000 + i);
        }
        bm.markDirty(page1->getPageId());
        bm.unpinPage(page1->getPageId());

        bm.commitTransaction();
        cout << "Transaction 1 committed (3 keys inserted)\n";

        // Start transaction 2 (will be interrupted by crash)
        bm.beginTransaction();

        Page *page2 = bm.allocatePage();
        page2->setPageType(PAGE_TYPE_BP_LEAF); // Use BP_LEAF
        page2->getHeader().is_leaf = 1;        // Explicitly mark as leaf
        page2->setNumKeys(2);
        for (int i = 0; i < 2; i++)
        {
            page2->setKey(i, 2000 + i);
            page2->setValue(i, 20000 + i);
        }
        bm.markDirty(page2->getPageId());
        bm.unpinPage(page2->getPageId());

        // DON'T commit - simulate crash!
        cout << "Transaction 2 NOT committed (simulating crash)\n";

        // Destructors will be called here, simulating crash
    }

    // Phase 2: Recovery
    cout << "\nPhase 2: Recovery after crash...\n";

    // Check WAL file exists and has content
    ifstream wal_file("test_crash_recovery.log", ios::binary | ios::ate);
    streampos file_size = wal_file.tellg();
    wal_file.close();

    cout << "WAL file size after crash: " << file_size << " bytes\n";
    assert(file_size > 0 && "WAL should have records");

    // Recover using WAL
    {
        WriteAheadLog wal_recover("test_crash_recovery.log");
        cout << "Starting WAL replay for recovery...\n";
        wal_recover.replay();
    }

    // Phase 3: Verify recovered state
    cout << "\nPhase 3: Verifying recovered database...\n";

    {
        // Reopen database after recovery
        FileManager fm("test_crash_recovery.db");
        BufferManager bm(&fm, nullptr, 10); // No WAL for verification

        // Try to read pages that should exist
        Page *page1 = bm.getPage(1); // Page from committed transaction
        if (page1)
        {
            cout << "Page 1 recovered: ";
            cout << "Type=" << page1->getPageType();
            cout << ", Keys=" << page1->getNumKeys();
            cout << ", IsLeaf=" << page1->isLeaf() << "\n";
            assert(page1->getNumKeys() == 3);
            bm.unpinPage(1);
        }

        Page *page2 = bm.getPage(2); // Page from uncommitted transaction
        if (page2)
        {
            cout << "Page 2 state after recovery: ";
            cout << "Type=" << page2->getPageType();
            cout << ", Keys=" << page2->getNumKeys();
            cout << ", IsLeaf=" << page2->isLeaf() << "\n";
            bm.unpinPage(2);
        }
    }

    cout << "✓ WAL crash recovery test OK\n";
}

void testWALTransactionRollback()
{
    cout << "\n=== Testing WAL Transaction Rollback ===\n";

    // Clean up old files
    remove("test_rollback.db");
    remove("test_rollback.log");

    // Setup
    WriteAheadLog wal("test_rollback.log");
    FileManager fm("test_rollback.db");
    BufferManager bm(&fm, &wal, 10);

    cout << "\nTest 1: Successful rollback...\n";

    // Start transaction
    bm.beginTransaction();

    // Make some changes
    Page *page1 = bm.allocatePage();
    page1->setPageType(PAGE_TYPE_DATA);
    page1->setNumKeys(2);
    page1->setKey(0, 111);
    page1->setKey(1, 222);
    bm.markDirty(page1->getPageId());

    Page *page2 = bm.allocatePage();
    page2->setPageType(PAGE_TYPE_DATA);
    page2->setNumKeys(1);
    page2->setKey(0, 333);
    bm.markDirty(page2->getPageId());

    cout << "Made changes to pages " << page1->getPageId()
         << " and " << page2->getPageId() << "\n";

    // Rollback transaction
    bm.rollbackTransaction();
    cout << "Transaction rolled back\n";

    bm.unpinPage(page1->getPageId());
    bm.unpinPage(page2->getPageId());

    // Flush to see what gets written
    bm.flushAll();

    cout << "\nTest 2: Mixed transaction and non-transaction work...\n";

    // Non-transactional work
    Page *page3 = bm.allocatePage();
    page3->setPageType(PAGE_TYPE_DATA);
    page3->setNumKeys(1);
    page3->setKey(0, 444);
    bm.markDirty(page3->getPageId());
    bm.unpinPage(page3->getPageId());

    // Transactional work
    bm.beginTransaction();
    Page *page4 = bm.allocatePage();
    page4->setPageType(PAGE_TYPE_DATA);
    page4->setNumKeys(1);
    page4->setKey(0, 555);
    bm.markDirty(page4->getPageId());
    bm.unpinPage(page4->getPageId());
    bm.commitTransaction();

    bm.flushAll();

    // Check WAL file
    ifstream wal_file("test_rollback.log", ios::binary | ios::ate);
    streampos file_size = wal_file.tellg();
    wal_file.close();

    cout << "Final WAL file size: " << file_size << " bytes\n";
    assert(file_size > 0);

    cout << "✓ WAL transaction rollback test OK\n";
}

void testWALPerformance()
{
    cout << "\n=== Testing WAL Performance ===\n";

    // Clean up old files
    remove("test_performance.db");
    remove("test_performance.log");

    WriteAheadLog wal("test_performance.log");
    FileManager fm("test_performance.db");
    BufferManager bm(&fm, &wal, 50);

    const int NUM_OPERATIONS = 1000;
    const int BATCH_SIZE = 100;

    cout << "Performing " << NUM_OPERATIONS << " operations with WAL logging...\n";

    auto start_time = chrono::high_resolution_clock::now();

    int transaction_count = 0;
    for (int i = 0; i < NUM_OPERATIONS; i++)
    {
        if (i % BATCH_SIZE == 0)
        {
            // Start new transaction every BATCH_SIZE operations
            if (transaction_count > 0)
            {
                bm.commitTransaction();
            }
            bm.beginTransaction();
            transaction_count++;
        }

        // Simulate some database operation
        Page *page = bm.allocatePage();
        if (page)
        {
            page->setPageType(PAGE_TYPE_DATA);
            page->setNumKeys(1);
            page->setKey(0, i);
            bm.markDirty(page->getPageId());
            bm.unpinPage(page->getPageId());
        }

        if (i % (BATCH_SIZE / 2) == 0 && i > 0)
        {
            // Simulate some reads
            for (int j = 1; j <= 5; j++)
            {
                Page *read_page = bm.getPage(j);
                if (read_page)
                {
                    bm.unpinPage(j);
                }
            }
        }
    }

    // Commit last transaction
    if (transaction_count > 0)
    {
        bm.commitTransaction();
    }

    // Flush everything
    bm.flushAll();

    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);

    // Get file sizes
    ifstream db_file("test_performance.db", ios::binary | ios::ate);
    ifstream wal_file("test_performance.log", ios::binary | ios::ate);

    streampos db_size = db_file.tellg();
    streampos wal_size = wal_file.tellg();

    db_file.close();
    wal_file.close();

    cout << "\nPerformance Results:\n";
    cout << "Total time: " << duration.count() << " ms\n";
    cout << "Operations per second: "
         << (NUM_OPERATIONS * 1000.0 / duration.count()) << "\n";
    cout << "Database file size: " << db_size << " bytes\n";
    cout << "WAL file size: " << wal_size << " bytes\n";
    cout << "WAL overhead: " << fixed << setprecision(1)
         << (wal_size * 100.0 / db_size) << "% of database size\n";

    cout << "✓ WAL performance test OK\n";
}

// UPDATE main() function to include WAL tests:
int main()
{
    cout << "=== Storage System Tests ===\n\n";

    try
    {
        cout << "Running testFileManager()...\n";
        testFileManager();

        cout << "\nRunning testPageSerialization()...\n";
        testPageSerialization();

        cout << "\nRunning testBufferManager()...\n";
        testBufferManager();

        cout << "\nRunning testPageOperations()...\n";
        testPageOperations();

        // ADD WAL TESTS:
        cout << "\nRunning testWALBasic()...\n";
        testWALBasic();

        cout << "\nRunning testWALWithBufferManager()...\n";
        testWALWithBufferManager();

        cout << "\nRunning testWALCrashRecovery()...\n";
        testWALCrashRecovery();

        cout << "\nRunning testWALTransactionRollback()...\n";
        testWALTransactionRollback();

        cout << "\nRunning testWALPerformance()...\n";
        testWALPerformance();

        cout << "\n=== ALL TESTS PASSED ===\n";

        // Clean up test files
        cout << "Cleaning up test files...\n";
        remove("test_filemanager.db");
        remove("test_buffer.db");
        remove("test_wal_basic.log");
        remove("test_wal_integration.db");
        remove("test_wal_integration.log");
        remove("test_crash_recovery.db");
        remove("test_crash_recovery.log");
        remove("test_rollback.db");
        remove("test_rollback.log");
        remove("test_performance.db");
        remove("test_performance.log");

        cout << "Test cleanup complete!\n";
        return 0;
    }
    catch (const exception &e)
    {
        cerr << "\nTest failed: " << e.what() << endl;
        return 1;
    }
    catch (...)
    {
        cerr << "\nTest failed with unknown exception" << endl;
        return 1;
    }
}
