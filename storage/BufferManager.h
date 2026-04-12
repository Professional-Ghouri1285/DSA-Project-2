// trading_platform/storage/BufferManager.h
#pragma once

#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <iomanip>
#include "FileManager.h"
#include "Page.h"
#include "WAL.h" // ADD THIS LINE

using namespace std;

class BufferManager
{
public:
    // MODIFIED: Add WAL parameter (default nullptr for backward compatibility)
    explicit BufferManager(FileManager *file_manager, WriteAheadLog *wal = nullptr, size_t capacity = 100);
    ~BufferManager();

    // Core operations
    Page *getPage(int page_id);
    Page *allocatePage();
    void markDirty(int page_id);
    void unpinPage(int page_id);
    void flushPage(int page_id);
    void flushAll();
    bool freePage(int page_id);

    // WAL operations
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
    void checkpoint();

    // Statistics and debug
    size_t getHitCount() const { return hit_count; }
    size_t getMissCount() const { return miss_count; }
    size_t getCacheSize() const { return cache.size(); }
    size_t getDirtyCount() const { return dirty_pages.size(); }
    size_t getCapacity() const { return capacity; }
    WriteAheadLog *getWAL() const { return wal; } // ADD THIS

    void printStats() const;
    void printCacheContents() const;

    FileManager *getFileManager() const { return file_manager; }

private:
    struct CacheEntry
    {
        Page page;
        bool dirty;
        int pin_count;
        list<int>::iterator lru_it;

        // ADD: For WAL - store before image
        vector<char> before_image;

        CacheEntry() : dirty(false), pin_count(0) {}
    };

    FileManager *file_manager;
    WriteAheadLog *wal; // ADD THIS
    size_t capacity;

    // Cache storage
    unordered_map<int, CacheEntry> cache;

    // LRU list (most recently used at front)
    list<int> lru_list;

    // Statistics
    mutable mutex cache_mutex;
    size_t hit_count;
    size_t miss_count;

    // Track dirty pages
    unordered_set<int> dirty_pages;

    // ADD: Transaction support
    bool in_transaction;
    vector<int> transaction_dirty_pages;
    unordered_map<int, vector<char>> transaction_before_images;

    // Helper methods
    void loadPageFromDisk(int page_id, CacheEntry &entry);
    void savePageToDisk(int page_id, CacheEntry &entry);
    void evictLRU();
    void removeFromCache(int page_id);

    // ADD: WAL helper methods
    void logPageUpdate(int page_id, const Page &old_page, const Page &new_page);
    void logPageAllocation(int page_id);
    void logPageFree(int page_id);

    // ADD: Before image capture
    void captureBeforeImage(int page_id, const Page &page);

    BufferManager(const BufferManager &) = delete;
    BufferManager &operator=(const BufferManager &) = delete;
};