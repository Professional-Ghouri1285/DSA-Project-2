#include "Page.h"
#include <iostream>
#include <stdexcept>

Page::Page()
{
    clear();
}

Page::Page(int page_id)
{
    clear();
    header.page_id = page_id;
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

void Page::validateIndex(int index) const
{
    if (index < 0 || index >= MAX_KEYS)
    {
        throw out_of_range("Index out of range: " + to_string(index));
    }
}

int Page::getKeyOffset(int index) const
{
    if (header.is_leaf)
    {
        return index * 16;
    }
    else
    {
        return index * 12;
    }
}

int Page::getChildOffset(int index) const
{
    if (header.is_leaf)
    {
        throw runtime_error("getChildOffset called on leaf node");
    }
    return index * 12 + 8;
}

int Page::getValueOffset(int index) const
{
    if (!header.is_leaf)
    {
        throw runtime_error("getValueOffset called on internal node");
    }
    return index * 16 + 8;
}

int64_t Page::getKey(int index) const
{
    validateIndex(index);
    int64_t key;
    int offset = getKeyOffset(index);
    memcpy(&key, data + offset, sizeof(int64_t));
    return key;
}

void Page::setKey(int index, int64_t key)
{
    validateIndex(index);
    int offset = getKeyOffset(index);
    memcpy(data + offset, &key, sizeof(int64_t));
}

int32_t Page::getChild(int index) const
{
    if (header.is_leaf)
    {
        throw runtime_error("getChild called on leaf node");
    }
    validateIndex(index);
    int32_t child;
    int offset = getChildOffset(index);
    memcpy(&child, data + offset, sizeof(int32_t));
    return child;
}

void Page::setChild(int index, int32_t child)
{
    if (header.is_leaf)
    {
        throw runtime_error("setChild called on leaf node");
    }
    validateIndex(index);
    int offset = getChildOffset(index);
    memcpy(data + offset, &child, sizeof(int32_t));
}

int64_t Page::getValue(int index) const
{
    if (!header.is_leaf)
    {
        throw runtime_error("getValue called on internal node");
    }
    validateIndex(index);
    int64_t value;
    int offset = getValueOffset(index);
    memcpy(&value, data + offset, sizeof(int64_t));
    return value;
}

void Page::setValue(int index, int64_t value)
{
    if (!header.is_leaf)
    {
        throw runtime_error("setValue called on internal node");
    }
    validateIndex(index);
    int offset = getValueOffset(index);
    memcpy(data + offset, &value, sizeof(int64_t));
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

    if (header.num_keys > 0)
    {
        cout << "Keys: ";
        for (int i = 0; i < header.num_keys && i < 10; ++i)
        {
            cout << getKey(i) << " ";
        }
        if (header.num_keys > 10)
        {
            cout << "... (+" << (header.num_keys - 10) << " more)";
        }
        cout << "\n";
    }
    cout << "====================\n";
}
