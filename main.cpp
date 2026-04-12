#include "storage/FileManager.h"
#include "storage/BufferManager.h"
#include "api/BackendAPI.h"
#include "server/HttpServer.h"
#include <iostream>
#include <signal.h>

using namespace std;

atomic<bool> running(true);

void signalHandler(int signum)
{
    cout << "\nInterrupt signal (" << signum << ") received.\n";
    running = false;
}

int main()
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    cout << "=== Trading Platform - Complete System ===\n\n";
    cout << "Starting components...\n";

    FileManager fm("trading_platform.db");

    // FIX: Create WAL first, then pass it to BufferManager
    WriteAheadLog wal("trading_platform.log");

    // FIX: Pass WAL as second parameter, capacity as third
    BufferManager bm(&fm, &wal, 1024); // Correct order: (FileManager*, WAL*, capacity)

    BackendAPI api(&fm, &bm);

    HttpServer server(8080, &api, 8);

    server.start();

    cout << "\n✅ System started successfully!\n";
    cout << "🌐 Server URL: http://localhost:8080\n";
    cout << "📊 Dashboard:  http://localhost:8080/index.html\n";
    cout << "📚 API Docs:   http://localhost:8080/api/status\n";
    cout << "\nPress Ctrl+C to stop the server...\n\n";

    while (running)
    {
        this_thread::sleep_for(chrono::seconds(1));
    }

    cout << "\nShutting down server...\n";
    server.stop();

    cout << "Goodbye!\n";
    return 0;
}