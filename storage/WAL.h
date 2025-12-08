#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

using namespace std;

enum LogType
{
    LOG_INSERT = 1,
    LOG_DELETE = 2,
    LOG_UPDATE = 3,
    LOG_COMMIT = 4
};

struct LogRecord
{
    int64_t lsn;
    int32_t type;
    int32_t page_id;
    int64_t timestamp;
    vector<char> data;

    void serialize(vector<char> &buffer) const;
    void deserialize(const char *buffer, size_t size);
};

class WriteAheadLog
{
public:
    WriteAheadLog(const string &filename);
    ~WriteAheadLog();

    int64_t append(LogType type, int page_id, const char *data, size_t size);
    void commit(int64_t lsn);
    void replay();
    void checkpoint();

    int64_t getLastLSN() const { return last_lsn; }

private:
    string filename;
    fstream log_file;
    int64_t last_lsn;

    void openLog();
    void writeRecord(const LogRecord &record);
};