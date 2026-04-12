// trading_platform/storage/Page.h
#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace std;

enum PageType
{
    PAGE_TYPE_FREE = 0,
    PAGE_TYPE_BT_INTERNAL = 1,
    PAGE_TYPE_BT_LEAF = 2,
    PAGE_TYPE_BP_INTERNAL = 3,
    PAGE_TYPE_BP_LEAF = 4,
    PAGE_TYPE_HASH_BUCKET = 5,
    PAGE_TYPE_DATA = 6,
    PAGE_TYPE_FREE_LIST = 7
};

struct PageHeader
{
    int32_t page_type;
    int32_t page_id;
    int32_t parent_page;
    int32_t is_leaf;
    int32_t num_keys;
    int64_t lsn;
    int32_t next_page;
    char reserved[32];

    PageHeader()
    {
        page_type = PAGE_TYPE_FREE;
        page_id = -1;
        parent_page = -1;
        is_leaf = 0;
        num_keys = 0;
        lsn = 0;
        next_page = -1;
        memset(reserved, 0, sizeof(reserved));
    }

    void serialize(char *buffer) const
    {
        memcpy(buffer, this, sizeof(PageHeader));
    }

    void deserialize(const char *buffer)
    {
        memcpy(this, buffer, sizeof(PageHeader));
    }
};

class Page
{
public:
    static constexpr int HEADER_SIZE = sizeof(PageHeader);
    static constexpr int DATA_SIZE = 4096 - HEADER_SIZE;

    // For free list pages (each entry is 4 bytes)
    static constexpr int FREE_LIST_CAPACITY = DATA_SIZE / sizeof(int32_t);

    // For B-Tree pages
    static constexpr int LEAF_ENTRY_SIZE = 16;     // 8-byte key + 8-byte value
    static constexpr int INTERNAL_ENTRY_SIZE = 12; // 8-byte key + 4-byte child

    static constexpr int MAX_LEAF_KEYS = DATA_SIZE / LEAF_ENTRY_SIZE;
    static constexpr int MAX_INTERNAL_KEYS = DATA_SIZE / INTERNAL_ENTRY_SIZE;
    static constexpr int MAX_FREE_LIST_ENTRIES = FREE_LIST_CAPACITY;

    static constexpr int BTREE_ORDER = (MAX_INTERNAL_KEYS + 1) / 2;

    Page();
    Page(int page_id, PageType type = PAGE_TYPE_FREE);

    void serialize(char *buffer) const;
    void deserialize(const char *buffer);
    void clear();

    void setHeader(const PageHeader &header) { this->header = header; }
    const PageHeader &getHeader() const { return header; }
    PageHeader &getHeader() { return header; }

    // Key operations
    int64_t getKey(int index) const;
    void setKey(int index, int64_t key);

    // Child operations (for internal nodes)
    int32_t getChild(int index) const;
    void setChild(int index, int32_t child);

    // Value operations (for leaf nodes)
    int64_t getValue(int index) const;
    void setValue(int index, int64_t value);

    // Free list operations
    int32_t getFreeEntry(int index) const;
    void setFreeEntry(int index, int32_t page_id);
    void addFreeEntry(int32_t page_id);
    int32_t popFreeEntry();

    // Helper methods
    bool isLeaf() const { return header.is_leaf == 1; }
    int getNumKeys() const { return header.num_keys; }
    void setNumKeys(int n) { header.num_keys = n; }
    int getPageId() const { return header.page_id; }
    void setPageId(int id) { header.page_id = id; }
    PageType getPageType() const { return static_cast<PageType>(header.page_type); }
    void setPageType(PageType type) { header.page_type = type; }
    int getNextPage() const { return header.next_page; }
    void setNextPage(int next) { header.next_page = next; }

    int getMaxLeafKeys() const { return MAX_LEAF_KEYS; }
    int getMaxInternalKeys() const { return MAX_INTERNAL_KEYS; }
    int getMaxFreeListEntries() const { return MAX_FREE_LIST_ENTRIES; }
    int getMaxKeys() const
    {
        return isLeaf() ? MAX_LEAF_KEYS : MAX_INTERNAL_KEYS;
    }

    void printDebug() const;
    const char *getData() const { return data; }
    char *getData() { return data; }

private:
    PageHeader header;
    char data[DATA_SIZE];

    void validateIndex(int index, int max) const;
    int getKeyOffset(int index) const;
    int getChildOffset(int index) const;
    int getValueOffset(int index) const;
    int getFreeEntryOffset(int index) const;
};