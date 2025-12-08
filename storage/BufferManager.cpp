#include "BufferManager.h"
#include <iostream>
#include <algorithm>

BufferManager::BufferManager(FileManager *file_manager, size_t capacity)
    : file_manager(file_manager), capacity(capacity),
      hit_count(0), miss_count(0)
{
    if (this->capacity < 2)
    {
        this->capacity = 2;
        cout << "BufferManager: Adjusted capacity to minimum 2 pages\n";
    }

    cout << "BufferManager initialized with capacity: "
         << this->capacity << " pages\n";
}

BufferManager::~BufferManager()
{
    flushAll();
}

Page *BufferManager::getPage(int page_id)
{
    lock_guard<mutex> lock(stats_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end())
    {
        hit_count++;
        lru_list.erase(it->second.lru_it);
        lru_list.push_front(page_id);
        it->second.lru_it = lru_list.begin();
        it->second.pin_count++;
        return &it->second.page;
    }

    miss_count++;

    if (cache.size() >= capacity)
    {
        bool evicted = false;
        for (auto rit = lru_list.rbegin(); rit != lru_list.rend(); ++rit)
        {
            int evict_id = *rit;
            auto evict_it = cache.find(evict_id);
            if (evict_it != cache.end() && evict_it->second.pin_count == 0)
            {
                if (evict_it->second.dirty)
                {
                    savePageToDisk(evict_id, evict_it->second);
                }

                lru_list.erase(next(rit).base());
                cache.erase(evict_it);
                dirty_pages.erase(evict_id);
                evicted = true;
                break;
            }
        }

        if (!evicted)
        {
            cerr << "ERROR: Cache full but all pages are pinned! Cannot evict.\n";
        }
    }

    CacheEntry entry;
    loadPageFromDisk(page_id, entry);

    lru_list.push_front(page_id);
    entry.lru_it = lru_list.begin();
    entry.pin_count = 1;

    cache[page_id] = move(entry);
    return &cache[page_id].page;
}

Page *BufferManager::allocatePage()
{
    int new_page_id = file_manager->allocatePage();
    cout << "Allocated new page ID: " << new_page_id << "\n";

    Page *page = getPage(new_page_id);
    page->clear();
    page->setPageId(new_page_id);

    markDirty(new_page_id);

    return page;
}

void BufferManager::markDirty(int page_id)
{
    lock_guard<mutex> lock(stats_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end())
    {
        it->second.dirty = true;
        dirty_pages.insert(page_id);
    }
}

void BufferManager::unpinPage(int page_id)
{
    lock_guard<mutex> lock(stats_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end() && it->second.pin_count > 0)
    {
        it->second.pin_count--;
    }
}

void BufferManager::flushPage(int page_id)
{
    lock_guard<mutex> lock(stats_mutex);

    auto it = cache.find(page_id);
    if (it != cache.end() && it->second.dirty)
    {
        savePageToDisk(page_id, it->second);
        it->second.dirty = false;
        dirty_pages.erase(page_id);
    }
}

void BufferManager::flushAll()
{
    lock_guard<mutex> lock(stats_mutex);

    cout << "Flushing " << dirty_pages.size() << " dirty pages to disk...\n";

    auto dirty_copy = dirty_pages;
    for (int page_id : dirty_copy)
    {
        auto it = cache.find(page_id);
        if (it != cache.end() && it->second.dirty)
        {
            savePageToDisk(page_id, it->second);
            it->second.dirty = false;
            dirty_pages.erase(page_id);
        }
    }

    cout << "All pages flushed.\n";
}

void BufferManager::flushMetadata()
{
    flushPage(0);
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
    }
}

void BufferManager::printStats() const
{
    lock_guard<mutex> lock(stats_mutex);

    cout << "\n=== BufferManager Statistics ===\n";
    cout << "Cache size: " << cache.size() << " / " << capacity << "\n";
    cout << "Hit count: " << hit_count << "\n";
    cout << "Miss count: " << miss_count << "\n";

    if (hit_count + miss_count > 0)
    {
        double hit_rate = (double)hit_count / (hit_count + miss_count) * 100;
        cout << "Hit rate: " << hit_rate << "%\n";
    }

    cout << "Dirty pages: " << dirty_pages.size() << "\n";
    cout << "Pinned pages: ";
    int pinned_count = 0;
    for (const auto &pair : cache)
    {
        if (pair.second.pin_count > 0)
        {
            cout << pair.first << " ";
            pinned_count++;
        }
    }
    cout << "(" << pinned_count << " total)\n";
    cout << "==============================\n";
}
