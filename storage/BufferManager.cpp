// trading_platform/storage/BufferManager.cpp
#include "BufferManager.h"
#include <iostream>
#include <cstring>

// MODIFIED: Add WAL parameter
BufferManager::BufferManager(FileManager *file_manager, WriteAheadLog *wal, size_t capacity)
    : file_manager(file_manager), wal(wal), capacity(capacity),
      hit_count(0), miss_count(0), in_transaction(false)
{
    if (capacity < 10)
    {
        this->capacity = 10;
        cout << "BufferManager: Adjusted capacity to minimum 10 pages\n";
    }

    cout << "BufferManager initialized with capacity: "
         << this->capacity << " pages\n";

    if (wal)
    {
        cout << "WAL integration: ENABLED (last LSN: " << wal->getLastLSN() << ")\n";
    }
    else
    {
        cout << "WAL integration: DISABLED\n";
    }
}

BufferManager::~BufferManager()
{
    flushAll();
}

// ADD: Capture before image for WAL
void BufferManager::captureBeforeImage(int page_id, const Page &page)
{
    if (!wal)
        return;

    auto it = cache.find(page_id);
    if (it != cache.end() && it->second.before_image.empty())
    {
        // Store before image for WAL
        char buffer[Page::HEADER_SIZE + Page::DATA_SIZE];
        page.serialize(buffer);
        it->second.before_image.assign(buffer, buffer + sizeof(buffer));
    }
}

// ADD: Log page update to WAL
void BufferManager::logPageUpdate(int page_id, const Page &old_page, const Page &new_page)
{
    if (!wal)
        return;

    // Serialize old and new page data
    char old_buffer[Page::HEADER_SIZE + Page::DATA_SIZE];
    char new_buffer[Page::HEADER_SIZE + Page::DATA_SIZE];

    old_page.serialize(old_buffer);
    new_page.serialize(new_buffer);

    // Combine old + new data for WAL
    char combined_data[2 * (Page::HEADER_SIZE + Page::DATA_SIZE)];
    memcpy(combined_data, old_buffer, sizeof(old_buffer));
    memcpy(combined_data + sizeof(old_buffer), new_buffer, sizeof(new_buffer));

    // Log the update
    wal->append(LOG_UPDATE, page_id, combined_data, sizeof(combined_data));

    cout << "WAL: Logged page " << page_id << " update (LSN: " << wal->getLastLSN() << ")\n";
}

// ADD: Log page allocation
void BufferManager::logPageAllocation(int page_id)
{
    if (!wal)
        return;

    char empty_page[Page::HEADER_SIZE + Page::DATA_SIZE] = {0};
    wal->append(LOG_INSERT, page_id, empty_page, sizeof(empty_page));

    cout << "WAL: Logged page " << page_id << " allocation (LSN: " << wal->getLastLSN() << ")\n";
}

// ADD: Log page free
void BufferManager::logPageFree(int page_id)
{
    if (!wal)
        return;

    wal->append(LOG_DELETE, page_id, nullptr, 0);

    cout << "WAL: Logged page " << page_id << " free (LSN: " << wal->getLastLSN() << ")\n";
}

// MODIFIED: Added transaction methods
void BufferManager::beginTransaction()
{
    lock_guard<mutex> lock(cache_mutex);

    if (in_transaction)
    {
        throw runtime_error("Transaction already in progress");
    }

    in_transaction = true;
    transaction_dirty_pages.clear();
    transaction_before_images.clear();

    if (wal)
    {
        // Log transaction begin with special page_id
        wal->append(LOG_COMMIT, -1, nullptr, 0);
        cout << "WAL: Transaction BEGIN logged\n";
    }
}

void BufferManager::commitTransaction()
{
    lock_guard<mutex> lock(cache_mutex);

    if (!in_transaction)
    {
        throw runtime_error("No transaction in progress");
    }

    // Flush all dirty pages in transaction
    for (int page_id : transaction_dirty_pages)
    {
        if (cache.find(page_id) != cache.end() && cache[page_id].dirty)
        {
            savePageToDisk(page_id, cache[page_id]);
        }
    }

    if (wal)
    {
        // Log transaction commit
        wal->commit(wal->getLastLSN());
        cout << "WAL: Transaction COMMIT logged\n";
    }

    in_transaction = false;
    transaction_dirty_pages.clear();
    transaction_before_images.clear();
}

void BufferManager::rollbackTransaction()
{
    lock_guard<mutex> lock(cache_mutex);

    if (!in_transaction)
    {
        throw runtime_error("No transaction in progress");
    }

    // Rollback: restore before images
    if (wal)
    {
        cout << "WAL: Rolling back transaction...\n";

        for (const auto &[page_id, before_image] : transaction_before_images)
        {
            auto it = cache.find(page_id);
            if (it != cache.end())
            {
                // Restore before image
                it->second.page.deserialize(before_image.data());
                it->second.dirty = true;

                // Log the rollback
                char current_buffer[Page::HEADER_SIZE + Page::DATA_SIZE];
                it->second.page.serialize(current_buffer);

                char combined[2 * (Page::HEADER_SIZE + Page::DATA_SIZE)];
                memcpy(combined, current_buffer, sizeof(current_buffer));
                memcpy(combined + sizeof(current_buffer), before_image.data(), before_image.size());

                wal->append(LOG_UPDATE, page_id, combined, sizeof(combined));

                cout << "WAL: Rolled back page " << page_id << "\n";
            }
        }
    }

    in_transaction = false;
    transaction_dirty_pages.clear();
    transaction_before_images.clear();
}

void BufferManager::checkpoint()
{
    if (wal)
    {
        wal->checkpoint();
        cout << "WAL: Checkpoint created\n";
    }
}

Page *BufferManager::getPage(int page_id)
{
    lock_guard<mutex> lock(cache_mutex);

    // Check cache first
    auto it = cache.find(page_id);
    if (it != cache.end())
    {
        // Cache hit
        hit_count++;

        // Update LRU: move to front
        lru_list.erase(it->second.lru_it);
        lru_list.push_front(page_id);
        it->second.lru_it = lru_list.begin();

        // Increment pin count
        it->second.pin_count++;

        return &it->second.page;
    }

    // Cache miss
    miss_count++;

    // Check if we need to evict
    if (cache.size() >= capacity)
    {
        evictLRU();
    }

    // Load page from disk
    CacheEntry entry;
    loadPageFromDisk(page_id, entry);

    // Capture before image for WAL if we're in a transaction
    if (wal && in_transaction)
    {
        captureBeforeImage(page_id, entry.page);
    }

    // Add to cache
    entry.pin_count = 1;
    lru_list.push_front(page_id);
    entry.lru_it = lru_list.begin();

    cache[page_id] = entry;

    return &cache[page_id].page;
}

Page *BufferManager::allocatePage()
{
    lock_guard<mutex> lock(cache_mutex);

    // Use file manager to allocate a new page
    int new_page_id = file_manager->allocatePage();
    if (new_page_id == -1)
    {
        return nullptr;
    }

    cout << "BufferManager allocating page: " << new_page_id << endl;

    // Check cache capacity
    if (cache.size() >= capacity)
    {
        evictLRU();
    }

    // Create new page in cache
    CacheEntry entry;
    entry.page.clear();
    entry.page.setPageId(new_page_id);
    entry.dirty = true;

    // Capture before image (empty page) for WAL
    if (wal)
    {
        captureBeforeImage(new_page_id, entry.page);

        // Log page allocation to WAL
        if (in_transaction)
        {
            logPageAllocation(new_page_id);
        }
    }

    // Add to cache and LRU
    entry.pin_count = 1;
    lru_list.push_front(new_page_id);
    entry.lru_it = lru_list.begin();
    cache[new_page_id] = entry;

    dirty_pages.insert(new_page_id);

    // Track in transaction if active
    if (in_transaction)
    {
        transaction_dirty_pages.push_back(new_page_id);
    }

    return &cache[new_page_id].page;
}

void BufferManager::markDirty(int page_id)
{
    lock_guard<mutex> lock(cache_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end())
    {
        // Capture before image if first time dirty in this transaction
        if (wal && in_transaction && !it->second.dirty)
        {
            captureBeforeImage(page_id, it->second.page);

            // Store before image for transaction
            char buffer[Page::HEADER_SIZE + Page::DATA_SIZE];
            it->second.page.serialize(buffer);
            transaction_before_images[page_id] =
                vector<char>(buffer, buffer + sizeof(buffer));
        }

        it->second.dirty = true;
        dirty_pages.insert(page_id);

        // Move to front of LRU since we're modifying it
        lru_list.erase(it->second.lru_it);
        lru_list.push_front(page_id);
        it->second.lru_it = lru_list.begin();

        // Track in transaction
        if (in_transaction)
        {
            transaction_dirty_pages.push_back(page_id);
        }
    }
}

void BufferManager::unpinPage(int page_id)
{
    lock_guard<mutex> lock(cache_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end() && it->second.pin_count > 0)
    {
        it->second.pin_count--;
    }
}

void BufferManager::flushPage(int page_id)
{
    lock_guard<mutex> lock(cache_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end() && it->second.dirty)
    {
        // Log to WAL before writing to disk
        if (wal)
        {
            // Get before image if available
            Page old_page;
            if (!it->second.before_image.empty())
            {
                old_page.deserialize(it->second.before_image.data());
            }
            else
            {
                // Load old version from disk
                char old_buffer[Page::HEADER_SIZE + Page::DATA_SIZE];
                if (file_manager->readPage(page_id, old_buffer))
                {
                    old_page.deserialize(old_buffer);
                }
                else
                {
                    old_page.clear();
                }
            }

            logPageUpdate(page_id, old_page, it->second.page);
        }

        savePageToDisk(page_id, it->second);
        it->second.dirty = false;
        dirty_pages.erase(page_id);

        // Clear before image after successful write
        it->second.before_image.clear();
    }
}

void BufferManager::flushAll()
{
    lock_guard<mutex> lock(cache_mutex);

    cout << "Flushing " << dirty_pages.size() << " dirty pages to disk...\n";

    // Make a copy since flushing might modify the set
    vector<int> pages_to_flush(dirty_pages.begin(), dirty_pages.end());

    for (int page_id : pages_to_flush)
    {
        auto it = cache.find(page_id);
        if (it != cache.end() && it->second.dirty)
        {
            // Log to WAL before writing
            if (wal)
            {
                Page old_page;
                if (!it->second.before_image.empty())
                {
                    old_page.deserialize(it->second.before_image.data());
                }
                else
                {
                    // Try to read from disk
                    char old_buffer[Page::HEADER_SIZE + Page::DATA_SIZE];
                    if (file_manager->readPage(page_id, old_buffer))
                    {
                        old_page.deserialize(old_buffer);
                    }
                }

                logPageUpdate(page_id, old_page, it->second.page);
            }

            savePageToDisk(page_id, it->second);
            it->second.dirty = false;
            dirty_pages.erase(page_id);
            it->second.before_image.clear();
        }
    }

    // Force file system sync
    file_manager->flush();

    cout << "All pages flushed.\n";

    // Commit any pending WAL records
    if (wal && in_transaction)
    {
        wal->commit(wal->getLastLSN());
        in_transaction = false;
    }
}

bool BufferManager::freePage(int page_id)
{
    lock_guard<mutex> lock(cache_mutex);

    // Log page free to WAL
    if (wal)
    {
        logPageFree(page_id);
    }

    // Flush if dirty
    auto it = cache.find(page_id);
    if (it != cache.end())
    {
        if (it->second.dirty)
        {
            savePageToDisk(page_id, it->second);
        }

        // Remove from cache
        lru_list.erase(it->second.lru_it);
        cache.erase(it);
        dirty_pages.erase(page_id);
    }

    // Tell file manager to add to free list
    return file_manager->freePage(page_id);
}

void BufferManager::loadPageFromDisk(int page_id, CacheEntry &entry)
{
    char buffer[PAGE_SIZE];

    if (file_manager->readPage(page_id, buffer))
    {
        entry.page.deserialize(buffer);
    }
    else
    {
        // Page doesn't exist on disk, create empty
        entry.page.clear();
        entry.page.setPageId(page_id);
    }

    entry.dirty = false;
    entry.pin_count = 0;
}

void BufferManager::savePageToDisk(int page_id, CacheEntry &entry)
{
    char buffer[PAGE_SIZE];
    entry.page.serialize(buffer);

    if (!file_manager->writePage(page_id, buffer))
    {
        cerr << "ERROR: Failed to write page " << page_id << " to disk\n";
        return;
    }

    cout << "Saved page " << page_id << " to disk\n";
}

void BufferManager::evictLRU()
{
    // Find unpinned page from LRU end
    for (auto rit = lru_list.rbegin(); rit != lru_list.rend(); ++rit)
    {
        int page_id = *rit;
        auto it = cache.find(page_id);

        if (it != cache.end() && it->second.pin_count == 0)
        {
            // Found unpinned page to evict
            if (it->second.dirty)
            {
                if (wal)
                {
                    // Log before eviction
                    Page old_page;
                    if (!it->second.before_image.empty())
                    {
                        old_page.deserialize(it->second.before_image.data());
                    }
                    logPageUpdate(page_id, old_page, it->second.page);
                }

                savePageToDisk(page_id, it->second);
                dirty_pages.erase(page_id);
            }

            // Remove from cache
            lru_list.erase(it->second.lru_it);
            cache.erase(it);

            cout << "Evicted page " << page_id << " from cache\n";
            return;
        }
    }

    // If we get here, all pages are pinned
    cerr << "WARNING: Cache full but all pages are pinned! "
         << "Consider increasing cache size.\n";
}

void BufferManager::removeFromCache(int page_id)
{
    auto it = cache.find(page_id);
    if (it != cache.end())
    {
        lru_list.erase(it->second.lru_it);
        cache.erase(it);
        dirty_pages.erase(page_id);
    }
}

void BufferManager::printStats() const
{
    lock_guard<mutex> lock(cache_mutex);

    cout << "\n=== BufferManager Statistics ===\n";
    cout << "Cache size: " << cache.size() << " / " << capacity << "\n";
    cout << "Hit count: " << hit_count << "\n";
    cout << "Miss count: " << miss_count << "\n";

    if (hit_count + miss_count > 0)
    {
        double hit_rate = (double)hit_count / (hit_count + miss_count) * 100;
        cout << "Hit rate: " << fixed << setprecision(2) << hit_rate << "%\n";
    }

    cout << "Dirty pages: " << dirty_pages.size() << "\n";
    cout << "WAL enabled: " << (wal ? "YES" : "NO") << "\n";
    cout << "In transaction: " << (in_transaction ? "YES" : "NO") << "\n";

    // Count pinned pages
    int pinned_count = 0;
    for (const auto &pair : cache)
    {
        if (pair.second.pin_count > 0)
        {
            pinned_count++;
        }
    }
    cout << "Pinned pages: " << pinned_count << "\n";
    cout << "==============================\n";
}

void BufferManager::printCacheContents() const
{
    lock_guard<mutex> lock(cache_mutex);

    cout << "\n=== Cache Contents (" << cache.size() << " pages) ===\n";

    // Print in LRU order (most recent first)
    int count = 0;
    for (int page_id : lru_list)
    {
        auto it = cache.find(page_id);
        if (it != cache.end())
        {
            cout << "[" << count++ << "] Page " << page_id
                 << " (pinned: " << it->second.pin_count
                 << ", dirty: " << (it->second.dirty ? "Y" : "N")
                 << ", before_img: " << (it->second.before_image.empty() ? "N" : "Y") << ")\n";
        }

        if (count >= 20) // Limit output
        {
            cout << "... and " << (cache.size() - 20) << " more\n";
            break;
        }
    }

    cout << "=============================\n";
}