// trading_platform/storage/WAL.cpp - Fixed version
#include "WAL.h"
#include <iostream>
#include <sys/stat.h>
#include <chrono>

WriteAheadLog::WriteAheadLog(const string &filename)
    : filename(filename), last_lsn(0)
{
    openLog();
}

WriteAheadLog::~WriteAheadLog()
{
    if (log_file.is_open())
    {
        log_file.close();
    }
}

void WriteAheadLog::openLog()
{
    log_file.open(filename, ios::binary | ios::in | ios::out | ios::app);

    if (!log_file.is_open())
    {
        // Create new log file
        log_file.open(filename, ios::binary | ios::out);
        if (log_file.is_open())
        {
            log_file.close();
            log_file.open(filename, ios::binary | ios::in | ios::out | ios::app);
        }
    }

    if (!log_file.is_open())
    {
        throw runtime_error("Failed to open WAL file: " + filename);
    }

    // Find last LSN by scanning file
    log_file.seekg(0, ios::end);
    streampos file_size = log_file.tellg();
    log_file.seekg(0, ios::beg);

    last_lsn = 0;

    // Scan through existing records to find max LSN
    while (log_file.tellg() < file_size)
    {
        int32_t record_size = 0;
        log_file.read(reinterpret_cast<char *>(&record_size), sizeof(int32_t));

        if (log_file.eof() || log_file.gcount() != sizeof(int32_t))
            break;

        if (record_size <= 0 || record_size > 1000000)
            break;

        // Skip the record data
        log_file.seekg(record_size, ios::cur);
    }

    // Reset for appending
    log_file.clear();
    log_file.seekp(0, ios::end);

    cout << "WAL opened: " << filename << " (size: " << file_size << " bytes)\n";
}

int64_t WriteAheadLog::append(LogType type, int page_id, const char *data, size_t size)
{
    LogRecord record;
    record.lsn = ++last_lsn;
    record.type = type;
    record.page_id = page_id;
    record.timestamp = chrono::duration_cast<chrono::milliseconds>(
                           chrono::system_clock::now().time_since_epoch())
                           .count();

    if (data && size > 0)
    {
        record.data.assign(data, data + size);
    }

    vector<char> buffer;
    record.serialize(buffer);

    log_file.write(buffer.data(), buffer.size());
    log_file.flush();

    cout << "WAL append LSN=" << record.lsn
         << " type=" << record.type
         << " page=" << record.page_id
         << " size=" << buffer.size() << "\n";

    return record.lsn;
}

void WriteAheadLog::writeRecord(const LogRecord &record)
{
    vector<char> buffer;
    record.serialize(buffer);
    log_file.write(buffer.data(), buffer.size());
}

void WriteAheadLog::commit(int64_t lsn)
{
    LogRecord commit_record;
    commit_record.lsn = ++last_lsn;
    commit_record.type = LOG_COMMIT;
    commit_record.page_id = -1;
    commit_record.timestamp = chrono::duration_cast<chrono::milliseconds>(
                                  chrono::system_clock::now().time_since_epoch())
                                  .count();

    writeRecord(commit_record);
    log_file.flush();

    cout << "WAL commit LSN=" << commit_record.lsn << "\n";
}

void WriteAheadLog::replay()
{
    cout << "\n=== WAL Replay Start ===\n";

    // Close and reopen for clean reading
    if (log_file.is_open())
    {
        log_file.close();
    }

    log_file.open(filename, ios::binary | ios::in);
    if (!log_file.is_open())
    {
        cerr << "ERROR: Cannot open WAL file for replay: " << filename << "\n";
        return;
    }

    // Get file size
    log_file.seekg(0, ios::end);
    streampos file_size = log_file.tellg();
    log_file.seekg(0, ios::beg);

    cout << "WAL file size: " << file_size << " bytes\n";

    if (file_size == 0)
    {
        cout << "WAL is empty, nothing to replay\n";
        log_file.close();
        openLog();
        return;
    }

    int record_count = 0;
    int64_t max_lsn = 0;

    // Read all records
    while (log_file.tellg() < file_size)
    {
        // Read record size
        int32_t record_size = 0;
        log_file.read(reinterpret_cast<char *>(&record_size), sizeof(int32_t));

        if (log_file.eof() || log_file.gcount() != sizeof(int32_t))
        {
            cout << "End of file reached\n";
            break;
        }

        // Validate record size
        if (record_size <= 0 || record_size > 1000000)
        {
            cerr << "ERROR: Invalid record size: " << record_size << "\n";
            break;
        }

        // Read record data
        vector<char> buffer(record_size);
        log_file.read(buffer.data(), record_size);

        if (static_cast<size_t>(log_file.gcount()) != buffer.size())
        {
            cerr << "ERROR: Incomplete record read\n";
            break;
        }

        // Parse record
        LogRecord record;
        record.deserialize(buffer.data(), buffer.size());

        record_count++;
        max_lsn = max(max_lsn, record.lsn);

        // Apply the record (in a real system, you'd update pages)
        cout << "Record " << record_count << ": "
             << "LSN=" << record.lsn
             << " Type=" << record.type
             << " Page=" << record.page_id
             << " Time=" << record.timestamp
             << " DataSize=" << record.data.size() << "\n";
    }

    cout << "WAL Replay complete: " << record_count
         << " records processed, max LSN=" << max_lsn << "\n";

    // Update last_lsn
    last_lsn = max_lsn;

    log_file.close();
    openLog(); // Reopen for appending

    cout << "=== WAL Replay End ===\n";
}

void WriteAheadLog::checkpoint()
{
    LogRecord checkpoint_record;
    checkpoint_record.lsn = ++last_lsn;
    checkpoint_record.type = LOG_COMMIT;
    checkpoint_record.page_id = -2; // Special ID for checkpoint
    checkpoint_record.timestamp = chrono::duration_cast<chrono::milliseconds>(
                                      chrono::system_clock::now().time_since_epoch())
                                      .count();

    writeRecord(checkpoint_record);
    log_file.flush();

    cout << "WAL checkpoint LSN=" << checkpoint_record.lsn << "\n";
}

void LogRecord::serialize(vector<char> &buffer) const
{
    // Calculate sizes
    size_t data_size = sizeof(int64_t) * 2 + // lsn + timestamp
                       sizeof(int32_t) * 2 + // type + page_id
                       this->data.size();

    // Reserve space: record_size + record_data
    size_t old_size = buffer.size();
    buffer.resize(old_size + sizeof(int32_t) + data_size);

    char *buf_ptr = buffer.data() + old_size;

    // Write record size
    memcpy(buf_ptr, &data_size, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    // Write record data
    memcpy(buf_ptr, &lsn, sizeof(int64_t));
    buf_ptr += sizeof(int64_t);

    memcpy(buf_ptr, &type, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    memcpy(buf_ptr, &page_id, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    memcpy(buf_ptr, &timestamp, sizeof(int64_t));
    buf_ptr += sizeof(int64_t);

    if (!data.empty())
    {
        memcpy(buf_ptr, data.data(), data.size());
    }
}

void LogRecord::deserialize(const char *buffer, size_t size)
{
    const char *buf_ptr = buffer;

    // Read fixed fields
    memcpy(&lsn, buf_ptr, sizeof(int64_t));
    buf_ptr += sizeof(int64_t);

    memcpy(&type, buf_ptr, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    memcpy(&page_id, buf_ptr, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    memcpy(&timestamp, buf_ptr, sizeof(int64_t));
    buf_ptr += sizeof(int64_t);

    // Read variable data
    size_t data_size = size - (buf_ptr - buffer);
    if (data_size > 0)
    {
        data.assign(buf_ptr, buf_ptr + data_size);
    }
}