# 📈 Trading Platform

A full-stack stock trading platform built entirely from scratch in C++17, featuring a custom database engine, persistent B+Tree indexing, a price-time priority matching engine, and a browser-based trading dashboard. Developed as a DSA semester project (BSCS-24075).

---

## Overview

This project implements a working stock trading platform without relying on any external database library. Everything from page-level disk storage to HTTP request routing is written by hand. The goal was to demonstrate deep integration of data structures and systems concepts: custom B+Trees stored on disk, LRU buffer management, write-ahead logging for crash recovery, a FIFO order matching engine, and a RESTful JSON API served over raw sockets.

Users can register, log in, place buy/sell orders, view live order books, track their portfolio, and compete on a leaderboard — all through a web browser.

---

## Architecture

The system is organized into five layers that build on each other:

```
Frontend (HTML/CSS/JS)
        ↓
HTTP Server (raw socket, thread pool)
        ↓
Backend API (JSON request/response handlers)
        ↓
Core Trading Engine (OrderBook, MatchingEngine, UserManager)
        ↓
Storage Engine (BufferManager → FileManager + WAL)
                    ↑
           B+Tree / HashIndex (built on top of storage)
```

### Directory Structure

```
.
├── main.cpp                  # Entry point — wires all components together
├── CMakeLists.txt            # Build configuration
├── json.hpp                  # nlohmann/json (header-only, bundled)
│
├── storage/                  # Custom database storage engine
│   ├── Page.{h,cpp}          # 4KB page abstraction
│   ├── FileManager.{h,cpp}   # Page-level disk I/O, metadata, free list
│   ├── BufferManager.{h,cpp} # LRU buffer pool with pin/unpin
│   └── WAL.{h,cpp}           # Write-Ahead Log for crash recovery
│
├── btree/                    # Persistent index structures
│   ├── BTree.{h,cpp}         # Standard B-Tree
│   └── BPlusTree.{h,cpp}     # B+Tree with linked leaf chain
│
├── index/                    # Higher-level index utilities
│   ├── HashIndex.{h,cpp}     # Persistent hash index (overflow chaining)
│   └── SymbolTable.h         # Stock/user symbol lookup tables
│
├── core/                     # Trading domain logic
│   ├── Order.h               # Order and Trade structs with serialization
│   ├── OrderBook.{h,cpp}     # Per-symbol order book (B+Tree backed)
│   ├── MatchingEngine.{h,cpp}# Price-time priority matching engine
│   └── UserManager.{h,cpp}   # User accounts, balances, portfolios
│
├── api/                      # HTTP API layer
│   └── BackendAPI.{h,cpp}    # JSON handlers for all endpoints
│
├── server/                   # HTTP server
│   └── HttpServer.{h,cpp}    # Raw socket server with thread pool + routing
│
├── frontend/                 # Web dashboard
│   ├── index.html            # Single-page trading interface
│   ├── app.js                # API calls, chart rendering, UI logic
│   └── style.css             # Bootstrap-based styling
│
└── test/                     # Test suites (one per layer)
    ├── storage_test.cpp
    ├── btree_test.cpp
    ├── day3_test.cpp         # B+Tree + HashIndex
    ├── day4_test.cpp         # Matching engine + user management
    ├── day5_test.cpp         # HTTP server + full API
    └── integration_test.cpp
```

---

## Key Data Structures & Design Decisions

### Custom Storage Engine
- All data lives in a single binary database file (`trading_platform.db`) organized into fixed 4KB pages.
- `FileManager` handles raw page reads/writes, a free-page list, and a metadata header (magic number, root page pointers for all trees, LSN).
- `BufferManager` sits on top with an LRU cache and pin-count tracking, so hot pages stay in memory. Capacity defaults to 1024 pages.
- `WriteAheadLog` appends log records before any page is modified, enabling crash recovery and `ROLLBACK`.

### B+Tree (Persistent)
- Nodes are stored directly as pages in the database file, loaded/evicted through the buffer pool.
- Leaf nodes are linked for efficient range scans.
- Used for: order book bid/ask indices (price-time keys), order-ID lookup, user index, portfolio index, and engine metadata.

### Hash Index
- Fixed-bucket extendible hash index stored on disk.
- Overflow handled with chained pages.
- Used for fast symbol-to-stock-ID and username-to-user-ID lookups.

### Order Book
- Each trading symbol gets its own `PersistentOrderBook`.
- Buy orders are indexed in a `bidsTree` (B+Tree keyed by descending price then ascending timestamp — highest bid first).
- Sell orders are indexed in an `asksTree` (ascending price then ascending timestamp — lowest ask first).
- A separate `orderIdIndex` tree allows O(log n) lookup by order ID for cancellations.
- Price-time keys are encoded as 64-bit integers: `(price × 10⁶) | (time_ns mod 10⁹)`.

### Matching Engine
- On each new order, the engine fetches the best opposing quotes from the order book and matches greedily until the order is filled or the book is exhausted.
- Supports `MARKET`, `LIMIT`, and `STOP` order types.
- Partial fills update the order in-place in the B+Tree and cache.
- Trade records are persisted and broadcast to trade history.

### User Manager
- Users stored persistently via B+Tree (user_id → page of `UserData`).
- Portfolio holdings stored as a composite-keyed B+Tree (`(user_id << 32) | symbol_hash → PortfolioEntry`).
- In-memory LRU cache for hot users (up to 100 users, 500 portfolio entries).
- Balance and portfolio updates happen atomically through WAL-backed transactions.

---

## REST API

The HTTP server listens on port **8080** and serves the frontend as static files. All API responses are JSON.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/register` | Create a new user account |
| `POST` | `/api/login` | Authenticate and get user ID |
| `GET` | `/api/status` | System health and statistics |
| `POST` | `/api/order` | Place a buy or sell order |
| `DELETE` | `/api/order` | Cancel an open order |
| `GET` | `/api/orderbook?symbol=AAPL` | Bid/ask depth for a symbol |
| `GET` | `/api/trades?symbol=AAPL` | Recent trade history |
| `GET` | `/api/portfolio?user_id=...` | Holdings for a user |
| `GET` | `/api/balance?user_id=...` | Cash balance for a user |
| `GET` | `/api/symbols` | List all traded symbols |
| `GET` | `/api/search?q=...` | Symbol search |
| `GET` | `/api/leaderboard` | Top traders by portfolio value |

---

## Frontend

A single-page web app served directly by the C++ HTTP server:

- **Dashboard** — market overview with price tickers
- **Trading** — place limit/market/stop orders, view live order book depth, recent trade feed
- **Portfolio** — holdings table with average cost and current value
- **Leaderboard** — ranked users by total portfolio value

Built with Bootstrap 5 and Chart.js (CDN). No build step required.

---

## Building & Running

### Prerequisites

- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.10+
- `nlohmann/json` 3.9.1+ (`sudo apt install nlohmann-json3-dev`)
- `libcurl` (`sudo apt install libcurl4-openssl-dev`)
- POSIX sockets (Linux/macOS)

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run

```bash
# From the build directory:
./trading_platform_day5
```

The server starts on `http://localhost:8080`. Open the dashboard at:

```
http://localhost:8080/index.html
```

Press `Ctrl+C` to shut down gracefully.

### Run Tests

```bash
# From the build directory, run all test suites:
make test_all

# Or individually:
./storage_test
./btree_test
./day3_test    # HashIndex + B+Tree deletion
./day4_test    # Order matching + user management
./day5_test    # Full HTTP API
```

---

## Persistent Files

| File | Description |
|------|-------------|
| `trading_platform.db` | Main database (pages, B+Trees, user data, orders) |
| `trading_platform.log` | Write-Ahead Log for crash recovery |

Both files are created automatically on first run. Delete them to reset all state.

---

## Technologies

- **Language**: C++17
- **Build**: CMake
- **JSON**: nlohmann/json (bundled header-only)
- **HTTP**: Hand-rolled TCP socket server (no framework)
- **Frontend**: Bootstrap 5 + Chart.js (CDN)
- **Concurrency**: `std::thread`, `std::mutex`, `std::atomic`, thread pool
- **Storage**: Custom page-based binary file format

---

## Course Context

Built for a Data Structures & Algorithms course (BSCS-24075) to demonstrate practical application of:

- B-Trees and B+Trees for disk-based indexing
- LRU buffer pool management
- Write-Ahead Logging (WAL) and crash recovery
- Hash tables with overflow chaining
- Price-time priority queues for order matching
- Multithreaded server design with a connection queue
