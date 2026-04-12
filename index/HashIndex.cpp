#include "HashIndex.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// Debug logging
#define HASH_LOG(x) std::cout << "[HashIndex] " << x << std::endl

HashIndex::HashIndex(BufferManager *bm, int num_buckets)
    : buffer_manager(bm),
      num_buckets(num_buckets),
      entry_count(0),
      directory_page(-1)
{
    HASH_LOG("Creating HashIndex with " << num_buckets << " buckets");
    HASH_LOG("Max entries per page: " << MAX_ENTRIES_PER_PAGE);

    // Initialize directory
    directory.resize(num_buckets);

    // Try to load existing hash index from default location
    // Directory is typically stored at page 1 (after metadata page 0)
    directory_page = 1;

    if (!loadFromDisk())
    {
        HASH_LOG("No existing index found, creating new one");

        // Create directory page
        Page *dir_page = buffer_manager->allocatePage();
        if (dir_page)
        {
            directory_page = dir_page->getPageId();
            dir_page->setPageType(PAGE_TYPE_HASH_BUCKET);
            dir_page->setNumKeys(0);

            // Initialize all buckets
            for (int i = 0; i < num_buckets; i++)
            {
                createBucket(i);
            }

            buffer_manager->markDirty(directory_page);
            buffer_manager->unpinPage(directory_page);

            HASH_LOG("Created new hash index at page " << directory_page);
        }
    }
}

HashIndex::~HashIndex()
{
    HASH_LOG("Destroying HashIndex");
    try
    {
        saveToDisk();
    }
    catch (...)
    {
        // Ignore errors during destruction
    }
    clearCache();
}

size_t HashIndex::hashFunction(int64_t key) const
{
    // Good hash function for 64-bit integers
    key = (~key) + (key << 21); // key = (key << 21) - key - 1;
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8); // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4); // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return static_cast<size_t>(key);
}

size_t HashIndex::hashString(const std::string &str) const
{
    // FNV-1a hash for strings
    const size_t prime = 1099511628211ULL;
    size_t hash = 14695981039346656037ULL;

    for (char c : str)
    {
        hash ^= static_cast<size_t>(c);
        hash *= prime;
    }

    return hash;
}

int HashIndex::getBucketIndex(int64_t key) const
{
    size_t hash_val = hashFunction(key);
    return static_cast<int>(hash_val % num_buckets);
}

int64_t HashIndex::symbolToKey(const std::string &symbol) const
{
    // Use hash of string as key (full 64-bit, no truncation!)
    size_t hash_val = hashString(symbol);
    return static_cast<int64_t>(hash_val);
}

bool HashIndex::insert(int64_t key, int64_t value)
{
    int bucket_idx = getBucketIndex(key);

    if (bucket_idx < 0 || bucket_idx >= num_buckets)
    {
        std::cerr << "Error: Invalid bucket index " << bucket_idx << std::endl;
        return false;
    }

    // Load bucket into cache
    BucketCache cache;
    if (!loadBucket(bucket_idx, cache))
    {
        std::cerr << "Error: Failed to load bucket " << bucket_idx << std::endl;
        return false;
    }

    // Check if key already exists in bucket
    for (const auto &entry : cache.entries)
    {
        if (entry.first == key)
        {
            HASH_LOG("Key " << key << " already exists in bucket " << bucket_idx);
            return false; // No duplicates allowed
        }
    }

    // Check if bucket has space
    if (cache.entries.size() < MAX_ENTRIES_PER_PAGE)
    {
        // Add to bucket
        cache.entries.emplace_back(key, value);

        // Save bucket
        if (saveBucket(cache))
        {
            directory[bucket_idx].entry_count++;
            entry_count++;
            updateDirectoryEntry(bucket_idx);

            HASH_LOG("Inserted key=" << key << " into bucket " << bucket_idx
                                     << " (entries: " << cache.entries.size() << ")");
            return true;
        }
    }
    else
    {
        HASH_LOG("Bucket " << bucket_idx << " full, trying overflow...");

        // Try to add to overflow chain
        if (addToOverflow(bucket_idx, key, value))
        {
            directory[bucket_idx].entry_count++;
            entry_count++;
            updateDirectoryEntry(bucket_idx);

            HASH_LOG("Added to overflow chain for bucket " << bucket_idx);
            return true;
        }
    }

    return false;
}

bool HashIndex::insert(const std::string &symbol, int64_t value)
{
    int64_t key = symbolToKey(symbol);
    HASH_LOG("Insert symbol='" << symbol << "' -> key=" << key);
    return insert(key, value);
}

bool HashIndex::remove(int64_t key)
{
    int bucket_idx = getBucketIndex(key);

    // Load bucket
    BucketCache cache;
    if (!loadBucket(bucket_idx, cache))
    {
        return false;
    }

    // Search in main bucket
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it)
    {
        if (it->first == key)
        {
            cache.entries.erase(it);

            // Save bucket
            if (saveBucket(cache))
            {
                directory[bucket_idx].entry_count--;
                entry_count--;
                updateDirectoryEntry(bucket_idx);

                HASH_LOG("Removed key=" << key << " from bucket " << bucket_idx);
                return true;
            }
            return false;
        }
    }

    // Check overflow chain
    if (cache.overflow_page != -1)
    {
        if (removeFromOverflow(bucket_idx, key))
        {
            directory[bucket_idx].entry_count--;
            entry_count--;
            updateDirectoryEntry(bucket_idx);

            HASH_LOG("Removed key=" << key << " from overflow chain");
            return true;
        }
    }

    return false;
}

bool HashIndex::remove(const std::string &symbol)
{
    int64_t key = symbolToKey(symbol);
    return remove(key);
}

std::pair<bool, int64_t> HashIndex::search(int64_t key)
{
    int bucket_idx = getBucketIndex(key);

    // Load bucket
    BucketCache cache;
    if (!loadBucket(bucket_idx, cache))
    {
        return {false, -1};
    }

    // Search in main bucket
    for (const auto &entry : cache.entries)
    {
        if (entry.first == key)
        {
            HASH_LOG("Found key=" << key << " in bucket " << bucket_idx);
            return {true, entry.second};
        }
    }

    // Check overflow chain
    if (cache.overflow_page != -1)
    {
        return searchOverflow(cache.overflow_page, key);
    }

    return {false, -1};
}

std::pair<bool, int64_t> HashIndex::search(const std::string &symbol)
{
    int64_t key = symbolToKey(symbol);
    return search(key);
}

// ==================== BUCKET OPERATIONS ====================

bool HashIndex::createBucket(int bucket_idx)
{
    if (bucket_idx < 0 || bucket_idx >= num_buckets)
    {
        return false;
    }

    // Allocate new page for bucket
    Page *page = buffer_manager->allocatePage();
    if (!page)
    {
        std::cerr << "Error: Failed to allocate page for bucket " << bucket_idx << std::endl;
        return false;
    }

    int page_id = page->getPageId();

    // Initialize page as hash bucket
    page->setPageType(PAGE_TYPE_HASH_BUCKET);
    page->setPageId(page_id);
    page->setNumKeys(0);
    page->setNextPage(-1); // No overflow initially

    // Create empty bucket cache
    BucketCache cache;
    cache.page_id = page_id;
    cache.overflow_page = -1;

    // Save to cache and disk
    bucket_cache[page_id] = cache;

    // Update directory
    directory[bucket_idx].bucket_page = page_id;
    directory[bucket_idx].overflow_page = -1;
    directory[bucket_idx].entry_count = 0;

    buffer_manager->markDirty(page_id);
    buffer_manager->unpinPage(page_id);

    HASH_LOG("Created bucket " << bucket_idx << " at page " << page_id);
    return true;
}

bool HashIndex::loadBucket(int bucket_idx, BucketCache &cache)
{
    if (bucket_idx < 0 || bucket_idx >= num_buckets)
    {
        return false;
    }

    int page_id = directory[bucket_idx].bucket_page;
    if (page_id == -1)
    {
        // Bucket doesn't exist yet
        return createBucket(bucket_idx);
    }

    // Check cache first
    auto it = bucket_cache.find(page_id);
    if (it != bucket_cache.end())
    {
        cache = it->second;
        return true;
    }

    // Load from disk
    Page *page = buffer_manager->getPage(page_id);
    if (!page)
    {
        std::cerr << "Error: Failed to load page " << page_id << std::endl;
        return false;
    }

    // Verify page type
    if (page->getPageType() != PAGE_TYPE_HASH_BUCKET)
    {
        std::cerr << "Error: Page " << page_id << " is not a hash bucket" << std::endl;
        buffer_manager->unpinPage(page_id);
        return false;
    }

    // Load bucket data
    cache.page_id = page_id;
    cache.overflow_page = page->getNextPage();
    cache.entries.clear();

    // Read entries from page
    int num_keys = page->getNumKeys();
    for (int i = 0; i < num_keys; i += 2)
    {
        if (i + 1 < num_keys)
        {
            int64_t key = page->getKey(i);
            int64_t value = page->getKey(i + 1);
            cache.entries.emplace_back(key, value);
        }
    }

    buffer_manager->unpinPage(page_id);

    // Add to cache
    bucket_cache[page_id] = cache;

    return true;
}

bool HashIndex::saveBucket(const BucketCache &cache)
{
    if (cache.page_id == -1)
    {
        return false;
    }

    Page *page = buffer_manager->getPage(cache.page_id);
    if (!page)
    {
        std::cerr << "Error: Failed to get page " << cache.page_id << std::endl;
        return false;
    }

    // Set page properties
    page->setPageType(PAGE_TYPE_HASH_BUCKET);
    page->setNumKeys(cache.entries.size() * 2); // Each entry = key + value
    page->setNextPage(cache.overflow_page);

    // Write entries
    for (size_t i = 0; i < cache.entries.size(); i++)
    {
        page->setKey(i * 2, cache.entries[i].first);
        page->setKey(i * 2 + 1, cache.entries[i].second);
    }

    buffer_manager->markDirty(cache.page_id);
    buffer_manager->unpinPage(cache.page_id);

    // Update cache
    bucket_cache[cache.page_id] = cache;

    return true;
}

// ==================== OVERFLOW HANDLING ====================

bool HashIndex::addToOverflow(int bucket_idx, int64_t key, int64_t value)
{
    // Get bucket cache
    BucketCache main_cache;
    if (!loadBucket(bucket_idx, main_cache))
    {
        return false;
    }

    // Follow overflow chain to find last page
    int current_page = main_cache.overflow_page;
    int prev_page = -1;

    while (current_page != -1)
    {
        Page *page = buffer_manager->getPage(current_page);
        if (!page)
        {
            break;
        }

        int num_keys = page->getNumKeys();

        // Check if this overflow page has space
        if (num_keys < Page::MAX_LEAF_KEYS - 2)
        { // Need space for at least one more entry
            // Add to this overflow page
            page->setKey(num_keys, key);
            page->setKey(num_keys + 1, value);
            page->setNumKeys(num_keys + 2);

            buffer_manager->markDirty(current_page);
            buffer_manager->unpinPage(current_page);
            return true;
        }

        prev_page = current_page;
        current_page = page->getNextPage();
        buffer_manager->unpinPage(prev_page);
    }

    // Need to create new overflow page
    Page *new_page = buffer_manager->allocatePage();
    if (!new_page)
    {
        return false;
    }

    int new_page_id = new_page->getPageId();
    new_page->setPageType(PAGE_TYPE_HASH_BUCKET);
    new_page->setNumKeys(2); // One entry
    new_page->setKey(0, key);
    new_page->setKey(1, value);
    new_page->setNextPage(-1);

    if (prev_page == -1)
    {
        // First overflow page
        main_cache.overflow_page = new_page_id;
        saveBucket(main_cache);

        // Update directory
        directory[bucket_idx].overflow_page = new_page_id;
    }
    else
    {
        // Link to previous overflow page
        Page *prev = buffer_manager->getPage(prev_page);
        if (prev)
        {
            prev->setNextPage(new_page_id);
            buffer_manager->markDirty(prev_page);
            buffer_manager->unpinPage(prev_page);
        }
    }

    buffer_manager->markDirty(new_page_id);
    buffer_manager->unpinPage(new_page_id);

    return true;
}

std::pair<bool, int64_t> HashIndex::searchOverflow(int overflow_page, int64_t key)
{
    int current_page = overflow_page;

    while (current_page != -1)
    {
        Page *page = buffer_manager->getPage(current_page);
        if (!page)
        {
            break;
        }

        int num_keys = page->getNumKeys();
        for (int i = 0; i < num_keys; i += 2)
        {
            if (i + 1 < num_keys)
            {
                int64_t page_key = page->getKey(i);
                if (page_key == key)
                {
                    int64_t value = page->getKey(i + 1);
                    buffer_manager->unpinPage(current_page);
                    return {true, value};
                }
            }
        }

        int next_page = page->getNextPage();
        buffer_manager->unpinPage(current_page);
        current_page = next_page;
    }

    return {false, -1};
}

bool HashIndex::removeFromOverflow(int bucket_idx, int64_t key)
{
    // Get main bucket
    BucketCache main_cache;
    if (!loadBucket(bucket_idx, main_cache))
    {
        return false;
    }

    int current_page = main_cache.overflow_page;
    int prev_page = -1;

    while (current_page != -1)
    {
        Page *page = buffer_manager->getPage(current_page);
        if (!page)
        {
            break;
        }

        int num_keys = page->getNumKeys();
        bool found = false;

        // Search for key in this page
        for (int i = 0; i < num_keys; i += 2)
        {
            if (i + 1 < num_keys)
            {
                if (page->getKey(i) == key)
                {
                    found = true;

                    // Shift remaining entries left
                    for (int j = i; j < num_keys - 2; j++)
                    {
                        page->setKey(j, page->getKey(j + 2));
                    }

                    page->setNumKeys(num_keys - 2);

                    // If page becomes empty, remove it from chain
                    if (page->getNumKeys() == 0)
                    {
                        int next_page = page->getNextPage();

                        if (prev_page == -1)
                        {
                            // First overflow page
                            main_cache.overflow_page = next_page;
                            saveBucket(main_cache);

                            // Update directory
                            directory[bucket_idx].overflow_page = next_page;
                        }
                        else
                        {
                            // Link previous page to next
                            Page *prev = buffer_manager->getPage(prev_page);
                            if (prev)
                            {
                                prev->setNextPage(next_page);
                                buffer_manager->markDirty(prev_page);
                                buffer_manager->unpinPage(prev_page);
                            }
                        }

                        // Free empty page
                        buffer_manager->freePage(current_page);
                        bucket_cache.erase(current_page);
                    }
                    else
                    {
                        buffer_manager->markDirty(current_page);
                    }

                    buffer_manager->unpinPage(current_page);
                    return true;
                }
            }
        }

        prev_page = current_page;
        current_page = page->getNextPage();
        buffer_manager->unpinPage(prev_page);
    }

    return false;
}

// ==================== DIRECTORY OPERATIONS ====================
bool HashIndex::loadDirectory()
{
    if (directory_page == -1)
    {
        HASH_LOG("No directory page specified, starting fresh");
        return false;
    }

    try
    {
        // Load first page
        Page *page = buffer_manager->getPage(directory_page);
        PageHeader header = page->getHeader();

        if (header.page_type != PAGE_TYPE_HASH_BUCKET)
        {
            HASH_LOG("Page " << directory_page << " is not a hash bucket (type="
                             << header.page_type << ")");
            buffer_manager->unpinPage(directory_page);
            return false;
        }

        // Read metadata from first page
        int offset = 0;
        num_buckets = page->getKey(offset++);
        entry_count = page->getKey(offset++);
        int num_pages = page->getKey(offset++);

        // 🚨 CRITICAL FIX: Check if loaded values are valid
        if (num_buckets <= 0 || num_buckets > 10000)
        { // Reasonable bounds
            HASH_LOG("Invalid bucket count loaded: " << num_buckets);
            buffer_manager->unpinPage(directory_page);
            return false;
        }

        HASH_LOG("Loading directory: buckets=" << num_buckets
                                               << ", entries=" << entry_count << ", pages=" << num_pages);

        // Resize directory
        directory.resize(num_buckets);

        // Read entries from all pages
        int entries_read = 0;

        for (int page_idx = 0; page_idx < num_pages && entries_read < num_buckets; page_idx++)
        {
            int page_id = directory_page + page_idx;

            if (page_idx > 0)
            {
                page = buffer_manager->getPage(page_id);
                offset = 0;
            }

            // Calculate how many entries can fit on this page
            int entries_this_page = (header.num_keys - (page_idx == 0 ? 3 : 0)) / 3;

            for (int i = 0; i < entries_this_page && entries_read < num_buckets; i++)
            {
                directory[entries_read].bucket_page = page->getKey(offset++);
                directory[entries_read].overflow_page = page->getKey(offset++);
                directory[entries_read].entry_count = page->getKey(offset++);
                entries_read++;
            }

            if (page_idx > 0)
            {
                buffer_manager->unpinPage(page_id);
            }
        }

        buffer_manager->unpinPage(directory_page);

        HASH_LOG("Successfully loaded " << entries_read << " directory entries");
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading directory: " << e.what() << std::endl;
        return false;
    }
}

bool HashIndex::saveDirectory()
{
    if (directory_page == -1)
    {
        // Allocate new directory page
        Page *dir_page = buffer_manager->allocatePage();
        if (!dir_page)
        {
            std::cerr << "Error: Failed to allocate directory page" << std::endl;
            return false;
        }
        directory_page = dir_page->getPageId();
    }

    // Directory may span multiple pages if large
    int entries_per_page = Page::MAX_LEAF_KEYS / 3; // Each entry needs 3 slots
    int num_pages_needed = (num_buckets + entries_per_page - 1) / entries_per_page;

    for (int page_idx = 0; page_idx < num_pages_needed; page_idx++)
    {
        int page_id = directory_page + page_idx;
        Page *page = buffer_manager->getPage(page_id);

        if (!page)
        {
            // Allocate new page if needed
            page = buffer_manager->allocatePage();
            if (!page)
                continue;
            page_id = page->getPageId();

            PageHeader header;
            header.page_type = PAGE_TYPE_HASH_BUCKET;
            header.page_id = page_id;
            page->setHeader(header);
        }

        // Write directory entries for this page
        int start_idx = page_idx * entries_per_page;
        int end_idx = std::min(start_idx + entries_per_page, num_buckets);

        int offset = 0;

        // Write metadata on first page only
        if (page_idx == 0)
        {
            page->setKey(offset++, num_buckets);
            page->setKey(offset++, entry_count);
            page->setKey(offset++, num_pages_needed);
        }

        // Write directory entries for this page
        for (int i = start_idx; i < end_idx && offset + 2 < Page::MAX_LEAF_KEYS; i++)
        {
            page->setKey(offset++, directory[i].bucket_page);
            page->setKey(offset++, directory[i].overflow_page);
            page->setKey(offset++, directory[i].entry_count);
        }

        // Update header
        PageHeader header = page->getHeader();
        header.num_keys = offset;
        page->setHeader(header);
        page->setNumKeys(offset);

        buffer_manager->markDirty(page_id);
        buffer_manager->unpinPage(page_id);
    }

    HASH_LOG("Saved directory to " << num_pages_needed << " pages (entries: " << num_buckets << ")");
    return true;
}

bool HashIndex::updateDirectoryEntry(int bucket_idx)
{
    if (bucket_idx < 0 || bucket_idx >= num_buckets)
    {
        return false;
    }

    // We update the entire directory when saving
    return true;
}

// ==================== PERSISTENCE ====================

bool HashIndex::saveToDisk()
{
    HASH_LOG("Saving HashIndex to disk...");

    // Save all cached buckets
    for (const auto &pair : bucket_cache)
    {
        saveBucket(pair.second);
    }

    // Save directory
    if (!saveDirectory())
    {
        std::cerr << "Error: Failed to save directory" << std::endl;
        return false;
    }

    HASH_LOG("Save complete. Total entries: " << entry_count);
    return true;
}

bool HashIndex::loadFromDisk()
{
    HASH_LOG("Loading HashIndex from disk...");

    clearCache();

    if (!loadDirectory())
    {
        HASH_LOG("Load failed, starting fresh");

        // Reset to defaults
        num_buckets = 101; // Default bucket count
        entry_count = 0;
        directory.clear();
        directory.resize(num_buckets);

        // Create initial buckets
        for (int i = 0; i < num_buckets; i++)
        {
            createBucket(i);
        }

        return false; // Indicate that we started fresh
    }

    HASH_LOG("Load successful. Entries: " << entry_count);
    return true;
}

// ==================== UTILITY ====================

void HashIndex::clearCache()
{
    bucket_cache.clear();
}

void HashIndex::printStatistics() const
{
    std::cout << "\n=== HashIndex Statistics ===" << std::endl;
    std::cout << "Number of buckets: " << num_buckets << std::endl;
    std::cout << "Total entries: " << entry_count << std::endl;
    std::cout << "Directory page: " << directory_page << std::endl;
    std::cout << "Max entries per page: " << MAX_ENTRIES_PER_PAGE << std::endl;

    // 🚨 FIX: Check for division by zero
    if (num_buckets > 0 && MAX_ENTRIES_PER_PAGE > 0)
    {
        double load_factor = (double)entry_count / (num_buckets * MAX_ENTRIES_PER_PAGE);
        std::cout << "Load factor: " << (load_factor * 100) << "%" << std::endl;
    }
    else
    {
        std::cout << "Load factor: N/A (invalid bucket count)" << std::endl;
    }
    // Calculate load factor
    double load_factor = (double)entry_count / (num_buckets * MAX_ENTRIES_PER_PAGE);
    std::cout << "Load factor: " << (load_factor * 100) << "%" << std::endl;

    // Count overflow pages
    int overflow_pages = 0;
    int max_chain_length = 0;

    for (const auto &entry : directory)
    {
        if (entry.overflow_page != -1)
        {
            int current = entry.overflow_page;
            int chain_length = 0;

            while (current != -1)
            {
                chain_length++;
                Page *page = buffer_manager->getPage(current);
                if (!page)
                    break;

                current = page->getNextPage();
                buffer_manager->unpinPage(entry.overflow_page);
            }

            overflow_pages += chain_length;
            if (chain_length > max_chain_length)
            {
                max_chain_length = chain_length;
            }
        }
    }

    std::cout << "Overflow pages: " << overflow_pages << std::endl;
    std::cout << "Longest overflow chain: " << max_chain_length << std::endl;
    std::cout << "Cache size: " << bucket_cache.size() << std::endl;
    std::cout << "================================\n"
              << std::endl;
}