// trading_platform/storage/Page.h
#pragma once

#include <cstdint>
#include <vector>
#include <cstring>

using namespace std;

enum PageType
{
    PAGE_TYPE_FREE = 0,
    PAGE_TYPE_BT_INTERNAL = 1,
    PAGE_TYPE_BT_LEAF = 2,
    PAGE_TYPE_BP_INTERNAL = 3,
    PAGE_TYPE_BP_LEAF = 4,
    PAGE_TYPE_HASH_BUCKET = 5,
    PAGE_TYPE_DATA = 6
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

    static constexpr int LEAF_ENTRY_SIZE = 16;
    static constexpr int INTERNAL_ENTRY_SIZE = 12;

    static constexpr int MAX_LEAF_KEYS = DATA_SIZE / LEAF_ENTRY_SIZE;
    static constexpr int MAX_INTERNAL_KEYS = DATA_SIZE / INTERNAL_ENTRY_SIZE;

    static constexpr int MAX_KEYS = MAX_INTERNAL_KEYS;

    static constexpr int KEYS_PER_PAGE = MAX_INTERNAL_KEYS;
    static constexpr int BPTREE_ORDER = MAX_INTERNAL_KEYS / 2;
    static constexpr int BTREE_ORDER = (MAX_INTERNAL_KEYS + 1) / 2;

    Page();
    Page(int page_id);

    void serialize(char *buffer) const;
    void deserialize(const char *buffer);
    void clear();

    void setHeader(const PageHeader &header) { this->header = header; }
    const PageHeader &getHeader() const { return header; }
    PageHeader &getHeader() { return header; }

    int64_t getKey(int index) const;
    void setKey(int index, int64_t key);
    int32_t getChild(int index) const;
    void setChild(int index, int32_t child);
    int64_t getValue(int index) const;
    void setValue(int index, int64_t value);

    bool isLeaf() const { return header.is_leaf == 1; }
    int getNumKeys() const { return header.num_keys; }
    void setNumKeys(int n) { header.num_keys = n; }
    int getPageId() const { return header.page_id; }
    void setPageId(int id) { header.page_id = id; }

    int getMaxLeafKeys() const { return MAX_LEAF_KEYS; }
    int getMaxInternalKeys() const { return MAX_INTERNAL_KEYS; }
    int getMaxKeys() const { return isLeaf() ? MAX_LEAF_KEYS : MAX_INTERNAL_KEYS; }

    void printDebug() const;

private:
    PageHeader header;
    char data[DATA_SIZE];

    void validateIndex(int index) const;
    int getKeyOffset(int index) const;
    int getChildOffset(int index) const;
    int getValueOffset(int index) const;
};
