// trading_platform/storage/FileManager.h
#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <mutex>
#include "Page.h"
#include <unistd.h>

using namespace std;

constexpr int PAGE_SIZE = 4096;
constexpr int MAGIC_NUMBER = 0xBEEFCAFE;
#pragma pack(push, 1)
struct DatabaseMetadata
{
    // Field order is CRITICAL - DO NOT CHANGE
    uint32_t magic;
    uint32_t version;
    uint32_t page_size;
    uint32_t total_pages;
    int64_t last_lsn; // 8 bytes at offset 16

    // B-Tree and general indices
    int32_t root_btree_page;     // offset 24
    int32_t root_bplustree_page; // offset 28
    int32_t hash_table_root;     // offset 32
    int32_t free_page_list_head; // offset 36

    // Application-specific roots - NEW FIELDS
    int32_t user_tree_root;      // offset 40 - USER DATA GOES HERE
    int32_t portfolio_tree_root; // offset 44 - PORTFOLIO DATA GOES HERE
    int32_t order_book_root;     // offset 48
    int32_t trade_history_root;  // offset 52

    // Pad to 128 bytes total
    char reserved[128 - 56]; // 72 bytes reserved

    DatabaseMetadata()
    {
        magic = MAGIC_NUMBER;
        version = 1;
        page_size = PAGE_SIZE;
        total_pages = 1;
        last_lsn = 0;

        // Initialize all roots to -1
        root_btree_page = -1;
        root_bplustree_page = -1;
        hash_table_root = -1;
        free_page_list_head = -1;

        user_tree_root = -1;
        portfolio_tree_root = -1;
        order_book_root = -1;
        trade_history_root = -1;

        memset(reserved, 0, sizeof(reserved));
    }

    void serialize(char *buffer) const
    {
        memset(buffer, 0, PAGE_SIZE);
        memcpy(buffer, this, sizeof(DatabaseMetadata));
    }

    void deserialize(const char *buffer)
    {
        memcpy(this, buffer, sizeof(DatabaseMetadata));
    }

    static void printOffsets()
    {
        cout << "\n=== DatabaseMetadata Offsets ===" << endl;
        cout << "Total size: " << sizeof(DatabaseMetadata) << " bytes" << endl;
        cout << "magic: " << offsetof(DatabaseMetadata, magic) << endl;
        cout << "version: " << offsetof(DatabaseMetadata, version) << endl;
        cout << "page_size: " << offsetof(DatabaseMetadata, page_size) << endl;
        cout << "total_pages: " << offsetof(DatabaseMetadata, total_pages) << endl;
        cout << "last_lsn: " << offsetof(DatabaseMetadata, last_lsn) << endl;
        cout << "root_btree_page: " << offsetof(DatabaseMetadata, root_btree_page) << endl;
        cout << "root_bplustree_page: " << offsetof(DatabaseMetadata, root_bplustree_page) << endl;
        cout << "hash_table_root: " << offsetof(DatabaseMetadata, hash_table_root) << endl;
        cout << "free_page_list_head: " << offsetof(DatabaseMetadata, free_page_list_head) << endl;
        cout << "user_tree_root: " << offsetof(DatabaseMetadata, user_tree_root) << " ← THIS IS WHERE USER DATA SHOULD BE" << endl;
        cout << "portfolio_tree_root: " << offsetof(DatabaseMetadata, portfolio_tree_root) << " ← THIS IS WHERE PORTFOLIO DATA SHOULD BE" << endl;
        cout << "order_book_root: " << offsetof(DatabaseMetadata, order_book_root) << endl;
        cout << "trade_history_root: " << offsetof(DatabaseMetadata, trade_history_root) << endl;
        cout << "================================\n"
             << endl;
    }
};
#pragma pack(pop)
class FileManager
{
public:
    explicit FileManager(const string &filename);
    ~FileManager();

    bool open();
    void close();
    bool isOpen() const { return file.is_open(); }

    bool readPage(int page_id, char *buffer);
    bool writePage(int page_id, const char *buffer);

    // Page management
    int allocatePage();
    bool freePage(int page_id);

    // Metadata operations
    bool readMetadata(DatabaseMetadata &metadata);
    bool writeMetadata(const DatabaseMetadata &metadata);
    bool updateMetadataField(int64_t offset, const void *data, size_t size);

    // Free list management
    bool initializeFreeList();
    int popFreePage();
    bool pushFreePage(int page_id);

    // File info
    int getTotalPages() const { return total_pages; }
    string getFileName() const { return filename; }

    // Debug
    void printFreeList() const;
    void printMetadata() const;

    static bool fileExists(const string &filename);
    void flush();
    // Root page management
    bool setUserTreeRoot(int root_page);
    bool setPortfolioTreeRoot(int root_page);
    bool setOrderBookRoot(int root_page);
    bool setTradeHistoryRoot(int root_page);

    int getUserTreeRoot() const;
    int getPortfolioTreeRoot() const;
    int getOrderBookRoot() const;
    int getTradeHistoryRoot() const;

    bool saveAllRoots(int user_root, int portfolio_root, int order_book_root, int trade_root);
    bool loadAllRoots(int &user_root, int &portfolio_root, int &order_book_root, int &trade_root);

private:
    string filename;
    fstream file;
    int total_pages;
    mutable recursive_mutex file_mutex;

    // Cached metadata
    DatabaseMetadata current_metadata;
    bool metadata_loaded;

    void initializeNewFile();
    bool openExistingFile();
    void seekToPage(int page_id);

    // Helper for free list traversal
    bool getFreeListPage(int page_id, char *buffer);
    bool updateFreeListPage(int page_id, const char *buffer);
};