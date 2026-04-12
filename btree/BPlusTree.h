#pragma once

#include "../storage/BufferManager.h"
#include "../storage/Page.h"
#include <vector>
#include <utility>
#include <iostream>
#include <queue>
#include <climits>

using namespace std;

class BPlusTree
{
public:
    // Constructor
    BPlusTree(BufferManager *buffer_manager, int root_page_id = -1);

    // Core operations
    bool insert(int64_t key, int64_t value);
    bool remove(int64_t key);
    pair<bool, int64_t> search(int64_t key) const;
    vector<pair<int64_t, int64_t>> rangeQuery(int64_t start_key, int64_t end_key) const;

    // Tree information
    int getRootPageId() const
    {
        cout << "DEBUG: BPlusTree::getRootPageId() returning: " << root_page_id << endl;
        return root_page_id;
    }

    int getHeight() const;
    int getTotalKeys() const;

    // Debug
    void printTree() const;
    void printLeaves() const;
    void validate() const;

    // Statistics
    int getInsertCount() const { return insert_count; }
    int getSearchCount() const { return search_count; }
    // In BPlusTree.h, add:
    vector<pair<int64_t, int64_t>> rangeQuery(int64_t start_key, int64_t end_key, int limit) const;
    // In BPlusTree.h add:
    // In BPlusTree.h add:
    bool isEmpty() const;
    void debugLeafChain() const;
    std::vector<int64_t> getAllKeys();
    std::vector<std::pair<int64_t, int64_t>> getAllKeyValuePairs();

private:
    BufferManager *buffer_manager;
    int root_page_id;
    static const int ORDER;
    // Statistics
    mutable int insert_count;
    mutable int search_count;

    // Constants - use Page's constants directly
    static const int MAX_LEAF_KEYS;
    static const int MAX_INTERNAL_KEYS;
    static const int MIN_LEAF_KEYS;
    static const int MIN_INTERNAL_KEYS;

    // Helper structures
    struct SearchResult
    {
        int page_id;
        int index;
        bool found;
    };

    // Node operations
    SearchResult findLeaf(int64_t key) const;
    void splitLeaf(int leaf_page_id);
    void splitInternal(int internal_page_id);
    void createNewRoot(int left_child, int right_child, int64_t key);
    void insertIntoParent(int parent_id, int64_t key, int right_child);

    // Deletion operations
    bool removeFromLeaf(int leaf_page_id, int64_t key);
    void borrowFromLeftSibling(int page_id, int parent_id, int child_index);
    void borrowFromRightSibling(int page_id, int parent_id, int child_index);
    void mergeWithSibling(int page_id, int sibling_id, int parent_id, bool is_left_sibling);
    void fixUnderflow(int page_id);
    void adjustRoot();

    // Node I/O - CORRECTED for your BufferManager
    Page *loadNode(int page_id) const; // Uses buffer_manager->getPage()
    Page *allocateNode(bool is_leaf);  // Uses buffer_manager->allocatePage()
    void markDirty(int page_id) const; // Uses buffer_manager->markDirty()
    void unpinPage(int page_id) const; // Uses buffer_manager->unpinPage()

    // Utility methods
    int findKeyIndex(Page *page, int64_t key) const;
    int getLeftSibling(int parent_id, int child_id) const;
    int getRightSibling(int parent_id, int child_id) const;
    int findChildIndex(Page *parent, int child_id) const;
    void validateNode(int page_id, bool is_root, int64_t min_key, int64_t max_key) const;

    // Debug helpers
    void printNode(int page_id, int depth) const;
    int countKeys(int page_id) const;
    void collectKeysFromNode(int pageId, std::vector<int64_t> &keys, bool isLeaf);
    void collectKeyValuePairsFromNode(int pageId, std::vector<std::pair<int64_t, int64_t>> &pairs, bool isLeaf);
};