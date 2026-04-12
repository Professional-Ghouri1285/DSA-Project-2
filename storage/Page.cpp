// trading_platform/storage/Page.cpp
#include "Page.h"
#include <iostream>
#include <stdexcept>
#include <sstream>

Page::Page()
{
    clear();
}

Page::Page(int page_id, PageType type)
{
    clear();
    header.page_id = page_id;
    header.page_type = type;
}

void Page::clear()
{
    header = PageHeader();
    memset(data, 0, DATA_SIZE);
}

void Page::serialize(char *buffer) const
{
    header.serialize(buffer);
    memcpy(buffer + HEADER_SIZE, data, DATA_SIZE);
}

void Page::deserialize(const char *buffer)
{
    header.deserialize(buffer);
    memcpy(data, buffer + HEADER_SIZE, DATA_SIZE);
}

void Page::validateIndex(int index, int max) const
{
    if (index < 0 || index >= max)
    {
        stringstream ss;
        ss << "Index out of range: " << index << " (max: " << max << ")";
        throw out_of_range(ss.str());
    }
}

int Page::getKeyOffset(int index) const
{
    if (header.is_leaf)
    {
        return index * LEAF_ENTRY_SIZE;
    }
    else
    {
        return index * INTERNAL_ENTRY_SIZE;
    }
}

int Page::getChildOffset(int index) const
{
    if (header.is_leaf)
    {
        throw runtime_error("getChildOffset called on leaf node");
    }
    return index * INTERNAL_ENTRY_SIZE + 8; // Key is 8 bytes
}

int Page::getValueOffset(int index) const
{
    if (!header.is_leaf)
    {
        throw runtime_error("getValueOffset called on internal node");
    }
    return index * LEAF_ENTRY_SIZE + 8; // Key is 8 bytes
}

int Page::getFreeEntryOffset(int index) const
{
    return index * sizeof(int32_t);
}

int64_t Page::getKey(int index) const
{
    if (header.page_type == PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("getKey called on free list page");
    }

    // Validate against correct max based on page type
    int max_keys;
    if (header.is_leaf)
    {
        max_keys = MAX_LEAF_KEYS;
    }
    else
    {
        max_keys = MAX_INTERNAL_KEYS;
    }
    validateIndex(index, max_keys);

    int offset = getKeyOffset(index);
    int64_t key;
    memcpy(&key, data + offset, sizeof(int64_t));
    return key;
}

void Page::setKey(int index, int64_t key)
{
    if (header.page_type == PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("setKey called on free list page");
    }
    validateIndex(index, getMaxKeys());
    int offset = getKeyOffset(index);
    memcpy(data + offset, &key, sizeof(int64_t));
}

int32_t Page::getChild(int index) const
{
    if (header.is_leaf || header.page_type == PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("getChild called on leaf or free list page");
    }
    validateIndex(index, getMaxKeys() + 1);
    int32_t child;
    int offset = getChildOffset(index);
    memcpy(&child, data + offset, sizeof(int32_t));
    return child;
}

void Page::setChild(int index, int32_t child)
{
    if (header.is_leaf || header.page_type == PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("setChild called on leaf or free list page");
    }
    validateIndex(index, getMaxKeys() + 1);
    int offset = getChildOffset(index);
    memcpy(data + offset, &child, sizeof(int32_t));
}

int64_t Page::getValue(int index) const
{
    if (!header.is_leaf || header.page_type == PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("getValue called on internal or free list page");
    }
    validateIndex(index, getMaxKeys());
    int64_t value;
    int offset = getValueOffset(index);
    memcpy(&value, data + offset, sizeof(int64_t));
    return value;
}

void Page::setValue(int index, int64_t value)
{
    if (!header.is_leaf || header.page_type == PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("setValue called on internal or free list page");
    }
    validateIndex(index, getMaxKeys());
    int offset = getValueOffset(index);
    memcpy(data + offset, &value, sizeof(int64_t));
}

int32_t Page::getFreeEntry(int index) const
{
    if (header.page_type != PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("getFreeEntry called on non-free list page");
    }
    validateIndex(index, MAX_FREE_LIST_ENTRIES);
    int32_t entry;
    int offset = getFreeEntryOffset(index);
    memcpy(&entry, data + offset, sizeof(int32_t));
    return entry;
}

void Page::setFreeEntry(int index, int32_t page_id)
{
    if (header.page_type != PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("setFreeEntry called on non-free list page");
    }
    validateIndex(index, MAX_FREE_LIST_ENTRIES);
    int offset = getFreeEntryOffset(index);
    memcpy(data + offset, &page_id, sizeof(int32_t));
}

void Page::addFreeEntry(int32_t page_id)
{
    if (header.page_type != PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("addFreeEntry called on non-free list page");
    }
    if (header.num_keys >= MAX_FREE_LIST_ENTRIES)
    {
        throw runtime_error("Free list page is full");
    }
    setFreeEntry(header.num_keys, page_id);
    header.num_keys++;
}

int32_t Page::popFreeEntry()
{
    if (header.page_type != PAGE_TYPE_FREE_LIST)
    {
        throw runtime_error("popFreeEntry called on non-free list page");
    }
    if (header.num_keys <= 0)
    {
        return -1;
    }
    header.num_keys--;
    return getFreeEntry(header.num_keys);
}

void Page::printDebug() const
{
    cout << "=== Page Debug Info ===\n";
    cout << "Page ID: " << header.page_id << "\n";
    cout << "Page Type: " << header.page_type << "\n";
    cout << "Parent: " << header.parent_page << "\n";
    cout << "Is Leaf: " << (header.is_leaf ? "Yes" : "No") << "\n";
    cout << "Num Keys: " << header.num_keys << "\n";
    cout << "Next Page: " << header.next_page << "\n";
    cout << "LSN: " << header.lsn << "\n";

    // Safe print - don't call methods that might throw
    if (header.page_type == PAGE_TYPE_FREE_LIST && header.num_keys > 0)
    {
        cout << "Free Page IDs: ";
        int to_print = min(header.num_keys, 10);
        for (int i = 0; i < to_print; ++i)
        {
            // Read directly from data buffer
            int offset = getFreeEntryOffset(i);
            int32_t entry;
            if (offset >= 0 && offset + sizeof(int32_t) <= DATA_SIZE)
            {
                memcpy(&entry, data + offset, sizeof(int32_t));
                cout << entry << " ";
            }
            else
            {
                cout << "[invalid] ";
            }
        }
        if (header.num_keys > 10)
        {
            cout << "... (+" << (header.num_keys - 10) << " more)";
        }
        cout << "\n";
    }
    else if (header.num_keys > 0)
    {
        cout << "Keys: ";
        int to_print = min(header.num_keys, 10);
        for (int i = 0; i < to_print; ++i)
        {
            // Read directly from data buffer
            int offset = getKeyOffset(i);
            if (offset >= 0 && offset + sizeof(int64_t) <= DATA_SIZE)
            {
                int64_t key;
                memcpy(&key, data + offset, sizeof(int64_t));
                cout << key << " ";
            }
            else
            {
                cout << "[invalid] ";
            }
        }
        if (header.num_keys > 10)
        {
            cout << "... (+" << (header.num_keys - 10) << " more)";
        }
        cout << "\n";
    }
    cout << "====================\n";
}