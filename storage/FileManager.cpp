#include "FileManager.h"
#include <iostream>
#include <sys/stat.h>

FileManager::FileManager(const string &filename)
    : filename(filename), total_pages(0)
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
    if (file.is_open())
    {
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

    file.seekg(0, ios::end);
    streamsize file_size = file.tellg();
    total_pages = static_cast<int>(file_size / PAGE_SIZE);

    if (file_size % PAGE_SIZE != 0)
    {
        cerr << "Warning: File size is not multiple of page size!" << endl;
    }

    cout << "Opened database file: " << filename
         << " (" << total_pages << " pages)" << endl;
    return true;
}

void FileManager::seekToPage(int page_id)
{
    if (page_id < 0 || page_id >= total_pages)
    {
        throw out_of_range("Page ID out of range: " + to_string(page_id));
    }
    file.seekg(page_id * PAGE_SIZE, ios::beg);
    file.seekp(page_id * PAGE_SIZE, ios::beg);
}

bool FileManager::readPage(int page_id, char *buffer)
{
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

int FileManager::allocatePage()
{
    int new_page_id = total_pages;
    char empty_page[PAGE_SIZE] = {0};

    if (!writePage(new_page_id, empty_page))
    {
        return -1;
    }

    return new_page_id;
}

bool FileManager::readMetadata(DatabaseMetadata &metadata)
{
    char buffer[PAGE_SIZE];
    if (!readPage(0, buffer))
    {
        return false;
    }

    metadata.deserialize(buffer);

    if (metadata.magic != MAGIC_NUMBER)
    {
        cerr << "Invalid database file (bad magic number)" << endl;
        return false;
    }

    return true;
}

bool FileManager::writeMetadata(const DatabaseMetadata &metadata)
{
    char buffer[PAGE_SIZE] = {0};
    metadata.serialize(buffer);
    return writePage(0, buffer);
}
