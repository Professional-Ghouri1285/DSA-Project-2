#include "WAL.h"
#include <iostream>
#include <sys/stat.h>

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
    if (log_file.is_open())
    {
        log_file.close();
    }

    log_file.open(filename, ios::binary | ios::in | ios::out | ios::app);

    if (!log_file.is_open())
    {
        log_file.open(filename, ios::binary | ios::out);
        log_file.close();
        log_file.open(filename, ios::binary | ios::in | ios::out | ios::app);
    }

    if (!log_file.is_open())
    {
        throw runtime_error("Failed to open WAL file: " + filename);
    }

    last_lsn = 0;
}

int64_t WriteAheadLog::append(LogType type, int page_id, const char *data, size_t size)
{
    LogRecord record;
    record.lsn = ++last_lsn;
    record.type = type;
    record.page_id = page_id;
    record.timestamp = time(nullptr);
    record.data.assign(data, data + size);

    writeRecord(record);
    log_file.flush();

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
    commit_record.timestamp = time(nullptr);

    writeRecord(commit_record);
    log_file.flush();
}

void WriteAheadLog::replay()
{
    cout << "WAL Replay starting...\n";

    log_file.clear();
    log_file.seekg(0, ios::beg);

    log_file.seekg(0, ios::end);
    size_t file_size = log_file.tellg();
    log_file.seekg(0, ios::beg);
    cout << "Log file size: " << file_size << " bytes\n";

    int record_count = 0;

    while (log_file.good() && !log_file.eof())
    {
        int32_t record_data_size = 0;
        log_file.read(reinterpret_cast<char *>(&record_data_size), sizeof(int32_t));

        if (log_file.eof() || log_file.gcount() != sizeof(int32_t))
        {
            cout << "End of file reached or incomplete read. Records processed: "
                 << record_count << "\n";
            break;
        }

        cout << "Record " << record_count + 1 << " data size: " << record_data_size << "\n";

        if (record_data_size <= 0)
        {
            cout << "WARNING: Invalid record size: " << record_data_size << "\n";
            break;
        }

        if (record_data_size > 1000000)
        {
            cerr << "ERROR: Corrupted log record with size: " << record_data_size << "\n";
            cerr << "This indicates serialization issue!\n";
            break;
        }

        vector<char> buffer(record_data_size);
        log_file.read(buffer.data(), record_data_size);

        size_t bytes_read = log_file.gcount();
        if (bytes_read != record_data_size)
        {
            cerr << "ERROR: Failed to read complete log record. Expected: "
                 << record_data_size << ", Got: " << bytes_read << "\n";
            break;
        }

        LogRecord record;
        record.deserialize(buffer.data(), buffer.size());

        cout << "LSN (hex): 0x" << hex << record.lsn << dec << "\n";
        cout << "LSN (decimal): " << record.lsn << "\n";
        cout << "Page ID: " << record.page_id << "\n";
        cout << "Type: " << record.type << "\n";

        if (!record.data.empty())
        {
            string log_data(record.data.begin(), record.data.end());
            cout << "  Data (" << record.data.size() << " bytes): " << log_data << "\n";
        }

        cout << "---\n";
        record_count++;
    }

    cout << "WAL Replay complete. Processed " << record_count << " records.\n";
}

void WriteAheadLog::checkpoint()
{
    LogRecord checkpoint_record;
    checkpoint_record.lsn = ++last_lsn;
    checkpoint_record.type = LOG_COMMIT;
    checkpoint_record.page_id = -2;
    checkpoint_record.timestamp = time(nullptr);

    writeRecord(checkpoint_record);
    log_file.flush();
}

void LogRecord::serialize(vector<char> &buffer) const
{
    size_t pos = buffer.size();

    size_t data_size = sizeof(int64_t) * 2 +
                       sizeof(int32_t) * 2 +
                       data.size();

    size_t total_size = sizeof(int32_t) + data_size;

    buffer.resize(pos + total_size);

    char *buf_ptr = buffer.data() + pos;

    memcpy(buf_ptr, &data_size, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

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

    memcpy(&lsn, buf_ptr, sizeof(int64_t));
    buf_ptr += sizeof(int64_t);

    memcpy(&type, buf_ptr, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    memcpy(&page_id, buf_ptr, sizeof(int32_t));
    buf_ptr += sizeof(int32_t);

    memcpy(&timestamp, buf_ptr, sizeof(int64_t));
    buf_ptr += sizeof(int64_t);

    size_t data_size = size - (buf_ptr - buffer);
    if (data_size > 0 && data_size < 10000)
    {
        data.assign(buf_ptr, buf_ptr + data_size);
    }
}
