// test/simple_buffer_test.cpp - UPDATED
#include <iostream>
#include "../storage/FileManager.h"
#include "../storage/BufferManager.h"

int main()
{
    std::cout << "=== SIMPLE BufferManager Test ===\n";

    // Create new database
    std::remove("simple_test.db"); // Start fresh

    FileManager fm("simple_test.db");
    if (!fm.open())
    {
        std::cerr << "Failed to open database\n";
        return 1;
    }

    // Very small cache for testing
    BufferManager bm(&fm, 3);

    std::cout << "\n1. Allocating pages...\n";
    Page *p1 = bm.allocatePage();
    p1->setKey(0, 100);
    p1->setNumKeys(1);
    bm.markDirty(p1->getPageId());
    bm.unpinPage(p1->getPageId()); // Important!
    std::cout << "   Page " << p1->getPageId() << " allocated\n";

    Page *p2 = bm.allocatePage();
    p2->setKey(0, 200);
    p2->setNumKeys(1);
    bm.markDirty(p2->getPageId());
    bm.unpinPage(p2->getPageId());
    std::cout << "   Page " << p2->getPageId() << " allocated\n";

    Page *p3 = bm.allocatePage();
    p3->setKey(0, 300);
    p3->setNumKeys(1);
    bm.markDirty(p3->getPageId());
    bm.unpinPage(p3->getPageId());
    std::cout << "   Page " << p3->getPageId() << " allocated\n";

    std::cout << "\n2. Reading pages (should be in cache)...\n";
    Page *r1 = bm.getPage(p1->getPageId());
    std::cout << "   Page " << p1->getPageId() << " key: " << r1->getKey(0) << "\n";
    bm.unpinPage(p1->getPageId());

    Page *r2 = bm.getPage(p2->getPageId());
    std::cout << "   Page " << p2->getPageId() << " key: " << r2->getKey(0) << "\n";
    bm.unpinPage(p2->getPageId());

    std::cout << "\n3. Allocating 4th page (cache full, should evict)...\n";
    Page *p4 = bm.allocatePage();
    p4->setKey(0, 400);
    p4->setNumKeys(1);
    bm.markDirty(p4->getPageId());
    bm.unpinPage(p4->getPageId());
    std::cout << "   Page " << p4->getPageId() << " allocated\n";

    std::cout << "\n4. Statistics:\n";
    bm.printStats();

    std::cout << "\n5. Flushing...\n";
    bm.flushAll();

    fm.close();
    std::cout << "\n=== Test PASSED ===\n";
    return 0;
}