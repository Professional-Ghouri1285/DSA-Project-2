#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

using namespace std;

constexpr int PAGE_SIZE = 4096;
constexpr int MAGIC_NUMBER = 0xBEEFCAFE;

struct DatabaseMetadata
{
    int32_t magic;
    int32_t version;
    int32_t page_size;
    int32_t total_pages;
    int64_t last_lsn;
    int32_t root_btree_page;
    int32_t root_bplustree_page;
    int32_t hash_table_root;
    int32_t free_page_list;
    char reserved[64];

    DatabaseMetadata()
    {
        magic = MAGIC_NUMBER;
        version = 1;
        page_size = PAGE_SIZE;
        total_pages = 1;
        last_lsn = 0;
        root_btree_page = -1;
        root_bplustree_page = -1;
        hash_table_root = -1;
        free_page_list = -1;
        memset(reserved, 0, sizeof(reserved));
    }

    void serialize(char *buffer) const
    {
        memcpy(buffer, this, sizeof(DatabaseMetadata));
    }

    void deserialize(const char *buffer)
    {
        memcpy(this, buffer, sizeof(DatabaseMetadata));
    }
};

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
    int allocatePage();

    bool readMetadata(DatabaseMetadata &metadata);
    bool writeMetadata(const DatabaseMetadata &metadata);

    int getTotalPages() const { return total_pages; }
    string getFileName() const { return filename; }

    static bool fileExists(const string &filename);

private:
    string filename;
    fstream file;
    int total_pages;

    void initializeNewFile();
    bool openExistingFile();
    void seekToPage(int page_id);
};
