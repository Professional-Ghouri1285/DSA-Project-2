// trading_platform/btree/BTree.h
#pragma once

#include "../storage/BufferManager.h"
#include "../storage/Page.h"
#include <vector>
#include <utility>
#include <memory>

using namespace std;

class BTree
{
public:
    // Constructor
    BTree(BufferManager *buffer_manager, int root_page_id = -1);

    // Destructor
    ~BTree();

    // Core operations
    bool insert(int64_t key, int64_t value);
    bool remove(int64_t key);
    pair<bool, int64_t> search(int64_t key);

    // Tree information
    int getRootPageId() const { return root_page_id; }
    int getHeight();
    int getTotalKeys();

    // Debug
    void printTree();
    void validate();

    // Persistence
    void flush() { buffer_manager->flushAll(); }

private:
    BufferManager *buffer_manager;
    int root_page_id;

    int getMaxKeysForPage(int page_id)
    {
        PageHandle page(buffer_manager, page_id);
        if (!page.valid())
            return 0;
        return page->isLeaf() ? Page::MAX_LEAF_KEYS : Page::MAX_INTERNAL_KEYS;
    }
    // B-Tree properties (derived from Page constants)
    static const int T; // Minimum degree
    static const int MAX_KEYS;
    static const int MIN_KEYS;
    const int MIN_LEAF_KEYS = (Page::MAX_LEAF_KEYS + 1) / 2 - 1;
    const int MIN_INTERNAL_KEYS = T - 1;

    // Helper methods
    struct SearchResult
    {
        int page_id;
        int index;
        bool found;
    };

    // Core algorithms
    SearchResult searchInternal(int page_id, int64_t key);
    void splitChild(int parent_id, int child_index);
    void insertNonFull(int page_id, int64_t key, int64_t value);

    // Utility
    int findKeyIndex(Page *page, int64_t key);

    // Page management helpers (RAII style)
    class PageHandle
    {
    public:
        PageHandle(BufferManager *bm, int page_id)
            : bm(bm), page_id(page_id), page(bm->getPage(page_id)) {}

        // Overload for allocation
        static PageHandle allocate(BufferManager *bm)
        {
            Page *new_page = bm->allocatePage();
            return PageHandle(bm, new_page->getPageId());
        }

        ~PageHandle()
        {
            if (page)
                bm->unpinPage(page_id);
        }
        Page *operator->() { return page; }
        Page *get() { return page; }
        bool valid() const { return page != nullptr; }
        int getPageId() const { return page_id; }

    private:
        BufferManager *bm;
        int page_id;
        Page *page;
    };

    // Debug helpers
    void printNode(int page_id, int depth);
    int countKeys(int page_id);

    // Deletion helpers (if implementing remove)

    bool removeFromLeaf(int page_id, int key_index);
    bool removeFromInternal(int page_id, int key_index);

    // Merge/borrow operations
    void mergeWithSibling(int parent_id, int child_index, bool merge_with_left);
    void borrowFromLeftSibling(int parent_id, int child_index);
    void borrowFromRightSibling(int parent_id, int child_index);

    // Utility for deletion
    int64_t getPredecessor(int page_id, int key_index);
    int64_t getSuccessor(int page_id, int key_index);
    void fillChild(int page_id, int child_index);

    // Tree fixing after deletion
    void fixAfterDeletion(int page_id);
};