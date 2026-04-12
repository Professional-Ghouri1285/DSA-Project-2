#pragma once

#include "../storage/BufferManager.h"
#include "../storage/Page.h"
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <memory>

class HashIndex
{
public:
    // Constructor/Destructor
    HashIndex(BufferManager *bm, int num_buckets = 101); // Prime number
    ~HashIndex();

    // Core operations
    bool insert(int64_t key, int64_t value);
    bool insert(const std::string &symbol, int64_t value);
    bool remove(int64_t key);
    bool remove(const std::string &symbol);
    std::pair<bool, int64_t> search(int64_t key);
    std::pair<bool, int64_t> search(const std::string &symbol);

    // Persistence
    bool saveToDisk();
    bool loadFromDisk();
    void printStatistics() const;

    // Get metadata
    int getDirectoryPage() const { return directory_page; }
    int getNumBuckets() const { return num_buckets; }
    int getEntryCount() const { return entry_count; }

private:
    // Constants
    static const int MAX_ENTRIES_PER_PAGE = Page::MAX_LEAF_KEYS / 2; // Half page for entries

    // Directory entry (stored in directory page)
    struct DirectoryEntry
    {
        int32_t bucket_page;   // Page ID of bucket
        int32_t overflow_page; // First overflow page (-1 if none)
        int32_t entry_count;   // Total entries in this chain

        DirectoryEntry() : bucket_page(-1), overflow_page(-1), entry_count(0) {}
    };

    // In-memory bucket structure (for caching)
    struct BucketCache
    {
        int page_id;
        std::vector<std::pair<int64_t, int64_t>> entries;
        int overflow_page;

        BucketCache() : page_id(-1), overflow_page(-1) {}
    };

    // Core members
    BufferManager *buffer_manager;
    int num_buckets;    // Number of buckets (fixed)
    int entry_count;    // Total entries
    int directory_page; // Page containing directory

    // Directory (loaded in memory for speed)
    std::vector<DirectoryEntry> directory;

    // Bucket cache (LRU could be added)
    std::unordered_map<int, BucketCache> bucket_cache;

    // Private methods
    size_t hashFunction(int64_t key) const;
    size_t hashString(const std::string &str) const;
    int getBucketIndex(int64_t key) const;

    // Bucket operations (persistent)
    bool loadBucket(int bucket_idx, BucketCache &cache);
    bool saveBucket(const BucketCache &cache);
    bool createBucket(int bucket_idx);

    // Overflow handling
    bool addToOverflow(int bucket_idx, int64_t key, int64_t value);
    std::pair<bool, int64_t> searchOverflow(int overflow_page, int64_t key);
    bool removeFromOverflow(int bucket_idx, int64_t key);

    // Directory operations
    bool loadDirectory();
    bool saveDirectory();
    bool updateDirectoryEntry(int bucket_idx);

    // Utility
    void clearCache();

    // Symbol handling
    int64_t symbolToKey(const std::string &symbol) const;
};