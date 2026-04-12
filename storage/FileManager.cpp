// trading_platform/storage/FileManager.cpp
#include "FileManager.h"
#include <iostream>
#include <sys/stat.h>
#include <iomanip>

FileManager::FileManager(const string &filename)
    : filename(filename), total_pages(0), metadata_loaded(false)
{
    open();
}

FileManager::~FileManager()
{
    close();
}

bool FileManager::fileExists(const string &filename)
{
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

bool FileManager::open()
{
    lock_guard<recursive_mutex> lock(file_mutex);

    if (fileExists(filename))
    {
        return openExistingFile();
    }
    else
    {
        initializeNewFile();
        return openExistingFile();
    }
}

void FileManager::close()
{
    lock_guard<recursive_mutex> lock(file_mutex);

    if (file.is_open())
    {
        // Write back cached metadata if dirty
        if (metadata_loaded)
        {
            writeMetadata(current_metadata);
        }
        file.close();
    }
}

void FileManager::initializeNewFile()
{
    ofstream new_file(filename, ios::binary);
    if (!new_file)
    {
        throw runtime_error("Failed to create database file: " + filename);
    }

    // Write metadata page (page 0)
    DatabaseMetadata metadata;
    char page_buffer[PAGE_SIZE] = {0};
    metadata.serialize(page_buffer);
    new_file.write(page_buffer, PAGE_SIZE);
    new_file.close();

    cout << "Created new database file: " << filename << endl;
}

bool FileManager::openExistingFile()
{
    file.open(filename, ios::binary | ios::in | ios::out);
    if (!file.is_open())
    {
        cerr << "Failed to open database file: " << filename << endl;
        return false;
    }

    // Read file size
    file.seekg(0, ios::end);
    streamsize file_size = file.tellg();
    total_pages = static_cast<int>(file_size / PAGE_SIZE);

    if (file_size % PAGE_SIZE != 0)
    {
        cerr << "Warning: File size (" << file_size
             << ") is not multiple of page size (" << PAGE_SIZE << ")!" << endl;
    }

    // Read and cache metadata
    if (!readMetadata(current_metadata))
    {
        cerr << "Failed to read metadata from file" << endl;
        return false;
    }

    metadata_loaded = true;

    cout << "Opened database file: " << filename
         << " (" << total_pages << " pages)" << endl;

    return true;
}

void FileManager::seekToPage(int page_id)
{
    if (page_id < 0)
    {
        throw out_of_range("Page ID out of range: " + to_string(page_id));
    }

    streampos position = static_cast<streampos>(page_id) * PAGE_SIZE;
    file.seekg(position, ios::beg);
    file.seekp(position, ios::beg);
}

bool FileManager::readPage(int page_id, char *buffer)
{
    lock_guard<recursive_mutex> lock(file_mutex);

    if (!file.is_open())
    {
        cerr << "File not open for reading page " << page_id << endl;
        return false;
    }

    if (page_id < 0)
    {
        cerr << "Invalid page ID: " << page_id << endl;
        return false;
    }

    // If page doesn't exist yet, return zeroed buffer
    if (page_id >= total_pages)
    {
        memset(buffer, 0, PAGE_SIZE);
        return true;
    }

    seekToPage(page_id);
    file.read(buffer, PAGE_SIZE);

    if (!file)
    {
        cerr << "Failed to read page " << page_id << endl;
        return false;
    }

    return true;
}

bool FileManager::writePage(int page_id, const char *buffer)
{
    lock_guard<recursive_mutex> lock(file_mutex);

    if (!file.is_open())
    {
        cerr << "File not open for writing page " << page_id << endl;
        return false;
    }

    if (page_id < 0)
    {
        cerr << "Invalid page ID: " << page_id << endl;
        return false;
    }

    // Extend file if needed
    if (page_id >= total_pages)
    {
        file.seekp(0, ios::end);
        char empty_page[PAGE_SIZE] = {0};

        for (int i = total_pages; i <= page_id; ++i)
        {
            file.write(empty_page, PAGE_SIZE);
            if (!file)
            {
                cerr << "Failed to extend file for page " << page_id << endl;
                return false;
            }
        }
        total_pages = page_id + 1;

        // Update metadata
        if (metadata_loaded)
        {
            current_metadata.total_pages = total_pages;
        }
    }

    seekToPage(page_id);
    file.write(buffer, PAGE_SIZE);
    file.flush();

    if (!file)
    {
        cerr << "Failed to write page " << page_id << endl;
        return false;
    }

    return true;
}

bool FileManager::readMetadata(DatabaseMetadata &metadata)
{
    char buffer[PAGE_SIZE];
    if (!readPage(0, buffer))
    {
        cerr << "ERROR: Failed to read page 0 for metadata!" << endl;
        return false;
    }

    // First, deserialize normally
    metadata.deserialize(buffer);

    // DEBUG: Show what we read
    cout << "\n=== DEBUG FileManager::readMetadata ===" << endl;
    cout << "Magic: 0x" << hex << metadata.magic << dec << " (expected: 0x" << hex << MAGIC_NUMBER << dec << ")" << endl;
    cout << "user_tree_root: " << metadata.user_tree_root << endl;
    cout << "portfolio_tree_root: " << metadata.portfolio_tree_root << endl;

    // Check if this is a NEW file (magic doesn't match)
    if (metadata.magic != MAGIC_NUMBER)
    {
        cout << "New database file detected or corrupted magic." << endl;
        cout << "Initializing fresh metadata..." << endl;

        // Create fresh metadata
        metadata = DatabaseMetadata();

        // Write it immediately
        char new_buffer[PAGE_SIZE];
        metadata.serialize(new_buffer);
        writePage(0, new_buffer);

        cout << "Fresh metadata initialized." << endl;
    }

    cout << "Final values:" << endl;
    cout << "  user_tree_root: " << metadata.user_tree_root << endl;
    cout << "  portfolio_tree_root: " << metadata.portfolio_tree_root << endl;
    cout << "=============================\n"
         << endl;

    return true;
}

bool FileManager::writeMetadata(const DatabaseMetadata &metadata)
{
    cout << "\n=== DEBUG FileManager::writeMetadata ===" << endl;
    cout << "Saving CRITICAL roots:" << endl;
    cout << "  user_tree_root: " << metadata.user_tree_root << " (at offset "
         << offsetof(DatabaseMetadata, user_tree_root) << ")" << endl;
    cout << "  portfolio_tree_root: " << metadata.portfolio_tree_root << " (at offset "
         << offsetof(DatabaseMetadata, portfolio_tree_root) << ")" << endl;
    cout << "==============================\n"
         << endl;

    char buffer[PAGE_SIZE];
    metadata.serialize(buffer);

    // DOUBLE-CHECK what we're writing
    DatabaseMetadata verify;
    verify.deserialize(buffer);

    if (verify.user_tree_root != metadata.user_tree_root ||
        verify.portfolio_tree_root != metadata.portfolio_tree_root)
    {
        cerr << "ERROR: Serialization mismatch!" << endl;
        return false;
    }

    if (!writePage(0, buffer))
    {
        cerr << "ERROR: Failed to write metadata page!" << endl;
        return false;
    }

    current_metadata = metadata;
    metadata_loaded = true;

    cout << "✓ Metadata saved successfully" << endl;

    return true;
}

bool FileManager::updateMetadataField(int64_t offset, const void *data, size_t size)
{
    if (offset + size > sizeof(DatabaseMetadata))
    {
        return false;
    }

    // Read current metadata
    DatabaseMetadata metadata;
    if (!readMetadata(metadata))
    {
        return false;
    }

    // Update field
    char *metadata_bytes = reinterpret_cast<char *>(&metadata);
    memcpy(metadata_bytes + offset, data, size);

    // Write back
    return writeMetadata(metadata);
}

bool FileManager::initializeFreeList()
{
    if (current_metadata.free_page_list_head != -1)
    {
        // Free list already initialized
        return true;
    }

    // Allocate first free list page
    int free_list_page = allocatePage();
    if (free_list_page == -1)
    {
        return false;
    }

    // Initialize free list page
    Page free_page(free_list_page, PAGE_TYPE_FREE_LIST);
    free_page.setNextPage(-1);

    char buffer[PAGE_SIZE];
    free_page.serialize(buffer);

    if (!writePage(free_list_page, buffer))
    {
        return false;
    }

    // Update metadata
    current_metadata.free_page_list_head = free_list_page;
    return writeMetadata(current_metadata);
}

int FileManager::popFreePage()
{
    if (current_metadata.free_page_list_head == -1)
    {
        return -1; // No free pages
    }

    // Read the head free list page
    char buffer[PAGE_SIZE];
    if (!readPage(current_metadata.free_page_list_head, buffer))
    {
        return -1;
    }

    Page free_page;
    free_page.deserialize(buffer);

    if (free_page.getPageType() != PAGE_TYPE_FREE_LIST)
    {
        cerr << "ERROR: Page " << current_metadata.free_page_list_head
             << " is not a free list page!" << endl;
        return -1;
    }

    // Try to pop a free page ID
    int32_t free_page_id = free_page.popFreeEntry();

    if (free_page_id == -1)
    {
        // This free list page is empty, try next one
        int next_page = free_page.getNextPage();
        if (next_page != -1)
        {
            // Update metadata to point to next free list page
            current_metadata.free_page_list_head = next_page;
            writeMetadata(current_metadata);

            // Free the empty free list page (it becomes available for reuse)
            free_page.setPageType(PAGE_TYPE_FREE);
            free_page.serialize(buffer);
            writePage(free_page.getPageId(), buffer);

            // Recursively try next page
            return popFreePage();
        }
        // No more free list pages and no free pages
        return -1;
    }

    // Save the updated free list page
    free_page.serialize(buffer);
    writePage(free_page.getPageId(), buffer);

    return free_page_id;
}

bool FileManager::pushFreePage(int page_id)
{
    if (page_id <= 0 || page_id >= total_pages)
    {
        cerr << "ERROR: Invalid page ID to free: " << page_id << endl;
        return false;
    }

    // Initialize free list if needed
    if (current_metadata.free_page_list_head == -1)
    {
        if (!initializeFreeList())
        {
            return false;
        }
    }

    // Read current free list head
    char buffer[PAGE_SIZE];
    if (!readPage(current_metadata.free_page_list_head, buffer))
    {
        return false;
    }

    Page free_page;
    free_page.deserialize(buffer);

    // Check if current free list page has space
    if (free_page.getNumKeys() < free_page.getMaxFreeListEntries())
    {
        // Add to current page
        free_page.addFreeEntry(page_id);
        free_page.serialize(buffer);
        return writePage(free_page.getPageId(), buffer);
    }
    else
    {
        // Current page is full, allocate new free list page
        int new_free_page_id = allocatePage();
        if (new_free_page_id == -1)
        {
            return false;
        }

        // Create new free list page
        Page new_free_page(new_free_page_id, PAGE_TYPE_FREE_LIST);
        new_free_page.setNextPage(current_metadata.free_page_list_head);
        new_free_page.addFreeEntry(page_id);

        // Update metadata to point to new head
        current_metadata.free_page_list_head = new_free_page_id;
        if (!writeMetadata(current_metadata))
        {
            return false;
        }

        // Write both pages
        new_free_page.serialize(buffer);
        if (!writePage(new_free_page_id, buffer))
        {
            return false;
        }

        // Update old free list page's next pointer
        free_page.setNextPage(new_free_page_id);
        free_page.serialize(buffer);
        return writePage(free_page.getPageId(), buffer);
    }
}

int FileManager::allocatePage()
{
    lock_guard<recursive_mutex> lock(file_mutex);

    // First try to reuse a free page
    int free_page_id = popFreePage();
    if (free_page_id != -1)
    {
        cout << "Reusing free page: " << free_page_id << endl;

        // Clear the page
        char empty_page[PAGE_SIZE] = {0};
        if (!writePage(free_page_id, empty_page))
        {
            return -1;
        }

        return free_page_id;
    }

    // No free pages, allocate new page at end of file
    int new_page_id = total_pages;

    // Extend file by one page
    char empty_page[PAGE_SIZE] = {0};
    if (!writePage(new_page_id, empty_page))
    {
        return -1;
    }

    // Update metadata
    current_metadata.total_pages = total_pages;
    writeMetadata(current_metadata);

    cout << "Allocated new page: " << new_page_id
         << " (total: " << total_pages << ")" << endl;

    return new_page_id;
}

bool FileManager::freePage(int page_id)
{
    if (page_id <= 0 || page_id >= total_pages)
    {
        cerr << "ERROR: Cannot free page " << page_id
             << " (valid range: 1-" << (total_pages - 1) << ")" << endl;
        return false;
    }

    cout << "Freeing page: " << page_id << endl;
    return pushFreePage(page_id);
}

bool FileManager::getFreeListPage(int page_id, char *buffer)
{
    return readPage(page_id, buffer);
}

bool FileManager::updateFreeListPage(int page_id, const char *buffer)
{
    return writePage(page_id, buffer);
}

void FileManager::printFreeList() const
{
    if (!metadata_loaded)
    {
        cout << "Metadata not loaded" << endl;
        return;
    }

    cout << "\n=== Free List ===\n";
    cout << "Head: " << current_metadata.free_page_list_head << endl;

    int current = current_metadata.free_page_list_head;
    int page_count = 0;

    while (current != -1 && page_count < 100) // Limit to 100 pages to avoid infinite loops
    {
        char buffer[PAGE_SIZE];
        if (!const_cast<FileManager *>(this)->readPage(current, buffer))
        {
            cout << "Failed to read free list page " << current << endl;
            break;
        }

        Page page;
        page.deserialize(buffer);

        cout << "\nFree List Page " << current << ":\n";
        cout << "  Type: " << (page.getPageType() == PAGE_TYPE_FREE_LIST ? "FREE_LIST" : "ERROR") << endl;
        cout << "  Next: " << page.getNextPage() << endl;
        cout << "  Free entries: " << page.getNumKeys() << endl;

        if (page.getNumKeys() > 0)
        {
            cout << "  Pages: ";
            for (int i = 0; i < min(page.getNumKeys(), 10); i++)
            {
                cout << page.getFreeEntry(i) << " ";
            }
            if (page.getNumKeys() > 10)
            {
                cout << "... (+" << (page.getNumKeys() - 10) << " more)";
            }
            cout << endl;
        }

        current = page.getNextPage();
        page_count++;
    }

    if (page_count >= 100)
    {
        cout << "\nWarning: Stopped after 100 free list pages (possible loop)" << endl;
    }

    cout << "=================\n";
}

void FileManager::printMetadata() const
{
    if (!metadata_loaded)
    {
        cout << "Metadata not loaded" << endl;
        return;
    }

    cout << "\n=== Database Metadata ===\n";
    cout << "Magic: 0x" << hex << current_metadata.magic << dec << endl;
    cout << "Version: " << current_metadata.version << endl;
    cout << "Page Size: " << current_metadata.page_size << endl;
    cout << "Total Pages: " << current_metadata.total_pages << endl;
    cout << "Last LSN: " << current_metadata.last_lsn << endl;
    cout << "B-Tree Root: " << current_metadata.root_btree_page << endl;
    cout << "B+Tree Root: " << current_metadata.root_bplustree_page << endl;
    cout << "Hash Table Root: " << current_metadata.hash_table_root << endl;
    cout << "Free List Head: " << current_metadata.free_page_list_head << endl;
    cout << "==========================\n";
}

void FileManager::flush()
{
    lock_guard<recursive_mutex> lock(file_mutex);

    if (file.is_open())
    {
        file.flush(); // Flush C++ buffers only
        // WARNING: Data may still be in OS cache, not on disk
    }
}

// In FileManager.cpp - Implement the new methods
bool FileManager::setUserTreeRoot(int root_page)
{
    if (!metadata_loaded)
        return false;

    current_metadata.user_tree_root = root_page;
    return writeMetadata(current_metadata);
}

bool FileManager::setPortfolioTreeRoot(int root_page)
{
    if (!metadata_loaded)
        return false;

    current_metadata.portfolio_tree_root = root_page;
    return writeMetadata(current_metadata);
}

bool FileManager::setOrderBookRoot(int root_page)
{
    if (!metadata_loaded)
        return false;

    current_metadata.order_book_root = root_page;
    return writeMetadata(current_metadata);
}

bool FileManager::setTradeHistoryRoot(int root_page)
{
    if (!metadata_loaded)
        return false;

    current_metadata.trade_history_root = root_page;
    return writeMetadata(current_metadata);
}

int FileManager::getUserTreeRoot() const
{
    return metadata_loaded ? current_metadata.user_tree_root : -1;
}

int FileManager::getPortfolioTreeRoot() const
{
    return metadata_loaded ? current_metadata.portfolio_tree_root : -1;
}

int FileManager::getOrderBookRoot() const
{
    return metadata_loaded ? current_metadata.order_book_root : -1;
}

int FileManager::getTradeHistoryRoot() const
{
    return metadata_loaded ? current_metadata.trade_history_root : -1;
}

bool FileManager::saveAllRoots(int user_root, int portfolio_root,
                               int order_book_root, int trade_root)
{
    if (!metadata_loaded)
    {
        cerr << "ERROR in saveAllRoots: metadata not loaded!" << endl;
        return false;
    }

    cout << "\n=== DEBUG FileManager::saveAllRoots CALLED ===" << endl;
    cout << "Caller is saving these roots:" << endl;
    cout << "  User root: " << user_root << endl;
    cout << "  Portfolio root: " << portfolio_root << endl;
    cout << "  Order book root: " << order_book_root << endl;
    cout << "  Trade history root: " << trade_root << endl;

    // Show current values before update
    cout << "\nCurrent metadata values:" << endl;
    cout << "  Current user root: " << current_metadata.user_tree_root << endl;
    cout << "  Current portfolio root: " << current_metadata.portfolio_tree_root << endl;
    cout << "==============================\n"
         << endl;

    current_metadata.user_tree_root = user_root;
    current_metadata.portfolio_tree_root = portfolio_root;
    current_metadata.order_book_root = order_book_root;
    current_metadata.trade_history_root = trade_root;

    return writeMetadata(current_metadata);
}
bool FileManager::loadAllRoots(int &user_root, int &portfolio_root,
                               int &order_book_root, int &trade_root)
{
    if (!metadata_loaded)
        return false;

    user_root = current_metadata.user_tree_root;
    portfolio_root = current_metadata.portfolio_tree_root;
    order_book_root = current_metadata.order_book_root;
    trade_root = current_metadata.trade_history_root;

    return true;
}