#pragma once

#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <unordered_set>
#include "FileManager.h"
#include "Page.h"

using namespace std;

class BufferManager
{
public:
    explicit BufferManager(FileManager *file_manager, size_t capacity = 100);
    ~BufferManager();

    Page *getPage(int page_id);
    Page *allocatePage();
    void markDirty(int page_id);
    void unpinPage(int page_id);

    void flushPage(int page_id);
    void flushAll();
    void flushMetadata();

    size_t getHitCount() const { return hit_count; }
    size_t getMissCount() const { return miss_count; }
    size_t getDirtyCount() const { return dirty_pages.size(); }
    size_t getCacheSize() const { return cache.size(); }
    size_t getCapacity() const { return capacity; }

    void printStats() const;

private:
    struct CacheEntry
    {
        Page page;
        bool dirty;
        int pin_count;
        list<int>::iterator lru_it;

        CacheEntry() : dirty(false), pin_count(0) {}
    };

    FileManager *file_manager;
    size_t capacity;

    unordered_map<int, CacheEntry> cache;
    list<int> lru_list;

    mutable mutex stats_mutex;
    size_t hit_count;
    size_t miss_count;

    unordered_set<int> dirty_pages;

    void evictPage();
    void loadPageFromDisk(int page_id, CacheEntry &entry);
    void savePageToDisk(int page_id, CacheEntry &entry);
    bool shouldEvict(const CacheEntry &entry) const;

    BufferManager(const BufferManager &) = delete;
    BufferManager &operator=(const BufferManager &) = delete;
};
