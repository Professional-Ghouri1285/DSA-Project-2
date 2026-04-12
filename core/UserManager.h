#pragma once

#include "Order.h"
#include "../btree/BPlusTree.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h"
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstring>

// User data structure (persistent)
struct UserData
{
    int user_id;
    char username[32];
    char password_hash[64];
    double cash_balance;
    int portfolio_root;

    // Fix: Make buffer size match actual data size
    static constexpr size_t SERIALIZED_SIZE =
        sizeof(user_id) +
        sizeof(username) +
        sizeof(password_hash) +
        sizeof(cash_balance) +
        sizeof(portfolio_root);

    void serialize(char *buffer) const
    {
        char *ptr = buffer;

        // Write user_id
        std::memcpy(ptr, &user_id, sizeof(user_id));
        ptr += sizeof(user_id);

        // Write username (fixed size)
        std::memcpy(ptr, username, sizeof(username));
        ptr += sizeof(username);

        // Write password_hash (fixed size)
        std::memcpy(ptr, password_hash, sizeof(password_hash));
        ptr += sizeof(password_hash);

        // Write cash_balance
        std::memcpy(ptr, &cash_balance, sizeof(cash_balance));
        ptr += sizeof(cash_balance);

        // Write portfolio_root
        std::memcpy(ptr, &portfolio_root, sizeof(portfolio_root));
    }

    static UserData deserialize(const char *buffer)
    {
        UserData data;
        const char *ptr = buffer;

        // Read user_id
        std::memcpy(&data.user_id, ptr, sizeof(data.user_id));
        ptr += sizeof(data.user_id);

        // Read username
        std::memcpy(data.username, ptr, sizeof(data.username));
        ptr += sizeof(data.username);

        // Read password_hash
        std::memcpy(data.password_hash, ptr, sizeof(data.password_hash));
        ptr += sizeof(data.password_hash);

        // Read cash_balance
        std::memcpy(&data.cash_balance, ptr, sizeof(data.cash_balance));
        ptr += sizeof(data.cash_balance);

        // Read portfolio_root
        std::memcpy(&data.portfolio_root, ptr, sizeof(data.portfolio_root));

        return data;
    }
};

// Portfolio entry (persistent) - FIXED: Add missing fields
struct PortfolioEntry
{
    int user_id;
    char symbol[8]; // 8 characters max for stock symbol
    int quantity;
    double avg_price;
    double total_investment; // Add this missing field

    // Constructor
    PortfolioEntry() : user_id(0), quantity(0), avg_price(0.0), total_investment(0.0)
    {
        memset(symbol, 0, sizeof(symbol));
    }

    PortfolioEntry(const std::string &sym, int qty, double price)
        : user_id(0), quantity(qty), avg_price(price), total_investment(qty * price)
    {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }

    // Fix: Make buffer size match actual data size
    static constexpr size_t SERIALIZED_SIZE =
        sizeof(user_id) +
        sizeof(symbol) +
        sizeof(quantity) +
        sizeof(avg_price) +
        sizeof(total_investment); // Add this

    void serialize(char *buffer) const
    {
        char *ptr = buffer;

        // Write user_id
        std::memcpy(ptr, &user_id, sizeof(user_id));
        ptr += sizeof(user_id);

        // Write symbol (fixed size)
        std::memcpy(ptr, symbol, sizeof(symbol));
        ptr += sizeof(symbol);

        // Write quantity
        std::memcpy(ptr, &quantity, sizeof(quantity));
        ptr += sizeof(quantity);

        // Write avg_price
        std::memcpy(ptr, &avg_price, sizeof(avg_price));
        ptr += sizeof(avg_price);

        // Write total_investment
        std::memcpy(ptr, &total_investment, sizeof(total_investment));
    }

    static PortfolioEntry deserialize(const char *buffer)
    {
        PortfolioEntry entry;
        const char *ptr = buffer;

        // Read user_id
        std::memcpy(&entry.user_id, ptr, sizeof(entry.user_id));
        ptr += sizeof(entry.user_id);

        // Read symbol
        std::memcpy(entry.symbol, ptr, sizeof(entry.symbol));
        ptr += sizeof(entry.symbol);

        // Read quantity
        std::memcpy(&entry.quantity, ptr, sizeof(entry.quantity));
        ptr += sizeof(entry.quantity);

        // Read avg_price
        std::memcpy(&entry.avg_price, ptr, sizeof(entry.avg_price));
        ptr += sizeof(entry.avg_price);

        // Read total_investment
        std::memcpy(&entry.total_investment, ptr, sizeof(entry.total_investment));

        return entry;
    }
};

// In-memory User structure (extends persistent data)
struct User
{
    UserData data;
    std::unordered_map<std::string, PortfolioEntry> portfolio; // Cache

    User() = default;
    User(const UserData &d) : data(d) {}

    bool canAfford(double amount) const { return data.cash_balance >= amount; }

    bool updateBalance(double amount)
    {
        if (amount < 0 && !canAfford(-amount))
            return false;
        data.cash_balance += amount;
        return true;
    }
    bool updatePortfolioSharesOnly(const std::string &symbol, int quantity_change, double price)
    {
        if (quantity_change == 0)
            return true;

        auto it = portfolio.find(symbol);
        if (it != portfolio.end())
        {
            PortfolioEntry &entry = it->second;

            if (quantity_change > 0)
            {
                // Buying more: recalc average price
                double total_cost = (entry.quantity * entry.avg_price) + (quantity_change * price);
                entry.quantity += quantity_change;
                entry.avg_price = total_cost / entry.quantity;
                entry.total_investment = total_cost; // Update total investment
            }
            else
            {
                // Selling: just reduce quantity
                entry.quantity += quantity_change; // quantity_change is negative
                if (entry.quantity <= 0)
                {
                    portfolio.erase(it);
                }
                else
                {
                    // Update total investment for remaining shares
                    entry.total_investment = entry.quantity * entry.avg_price;
                }
            }
        }
        else
        {
            // New holding (must be buying)
            if (quantity_change > 0)
            {
                PortfolioEntry new_entry;
                // FIX: Use strncpy for char array assignment
                strncpy(new_entry.symbol, symbol.c_str(), sizeof(new_entry.symbol) - 1);
                new_entry.symbol[sizeof(new_entry.symbol) - 1] = '\0'; // Null terminate

                new_entry.quantity = quantity_change;
                new_entry.avg_price = price;
                new_entry.user_id = data.user_id; // IMPORTANT: Set user_id
                new_entry.total_investment = quantity_change * price;
                portfolio[symbol] = new_entry;
            }
            else
            {
                // Trying to sell something we don't own
                return false;
            }
        }
        return true;
    }

    // In UserManager.h - update the User::updatePortfolio method
    void updatePortfolio(const std::string &symbol, int qty_change, double price)
    {
        auto it = portfolio.find(symbol);
        if (it == portfolio.end())
        {
            if (qty_change > 0)
            {
                // Create new entry
                PortfolioEntry entry(symbol, qty_change, price);
                entry.user_id = data.user_id; // Set user_id
                portfolio[symbol] = entry;
            }
            // If qty_change <= 0 and symbol doesn't exist, do nothing
        }
        else
        {
            PortfolioEntry &entry = it->second;
            int new_quantity = entry.quantity + qty_change;

            if (new_quantity <= 0)
            {
                // Remove entry if quantity becomes 0 or negative
                portfolio.erase(it);
            }
            else
            {
                // Update existing entry
                double total_value = (entry.quantity * entry.avg_price) + (qty_change * price);
                entry.quantity = new_quantity;
                entry.avg_price = total_value / new_quantity;
                entry.total_investment = total_value;
            }
        }
    }
};

class UserManager
{
private:
    BufferManager *buffer_manager;
    WriteAheadLog *wal;

    // Persistent B+Tree indices
    BPlusTree user_index;      // user_id -> page_id of UserData
    BPlusTree portfolio_index; // composite_key(user_id, symbol) -> page_id of PortfolioEntry

    // In-memory cache with LRU eviction
    std::unordered_map<int, User> user_cache;
    std::unordered_map<int, std::unordered_map<std::string, PortfolioEntry>> portfolio_cache;

    std::recursive_mutex user_mutex;
    std::atomic<int> next_user_id{1000};

    // Cache management
    static constexpr int MAX_USER_CACHE = 100;
    static constexpr int MAX_PORTFOLIO_CACHE = 500;

    // Helper methods
    int storeUserData(const UserData &user);
    bool loadUserData(int user_id, UserData &user);
    int storePortfolioEntry(int user_id, const std::string &symbol, const PortfolioEntry &entry);
    bool loadPortfolioEntry(int user_id, const std::string &symbol, PortfolioEntry &entry);
    void loadAllPortfolios(int user_id);
    void evictOldestUser();
    void evictOldestPortfolio(int user_id);

    // Page serialization helpers
    int storeDataPage(const char *data, size_t size, PageType type = PAGE_TYPE_DATA);
    bool loadDataPage(int page_id, char *buffer, size_t size);

public:
    UserManager(BufferManager *bm, WriteAheadLog *w = nullptr,
                int user_root = -1, int portfolio_root = -1);
    ~UserManager();

    // No copy
    UserManager(const UserManager &) = delete;
    UserManager &operator=(const UserManager &) = delete;

    // User management
    int createUser(const std::string &username, const std::string &password,
                   double initial_balance = 10000.0);
    bool authenticate(int user_id, const std::string &password);
    User *getUser(int user_id);
    bool deleteUser(int user_id);

    // Portfolio management
    bool updateBalance(int user_id, double amount);
    bool updatePortfolio(int user_id, const std::string &symbol,
                         int quantity_change, double price);
    bool updatePortfolioSharesOnly(int user_id, const std::string &symbol,
                                   int quantity_change, double price);

    // Query methods
    double getBalance(int user_id);
    std::vector<PortfolioEntry> getPortfolio(int user_id);
    PortfolioEntry getHolding(int user_id, const std::string &symbol);

    // Trade execution
    bool executeTrade(int buyer_id, int seller_id,
                      const std::string &symbol, int quantity, double price);

    // Admin & debug
    void printUserInfo(int user_id);
    void printAllUsers();
    void flush();

    // Statistics
    int getUserCount();
    int getTotalHoldings();

    // Tree info
    int getUserTreeRoot() const { return user_index.getRootPageId(); }
    int getPortfolioTreeRoot() const { return portfolio_index.getRootPageId(); }

    bool saveRootPages(); // Call this before closing
    bool loadRootPages(); // Call this after opening

    // Get root page IDs (for testing/debugging)
    int getUserRootPage() const { return user_index.getRootPageId(); }
    int getPortfolioRootPage() const { return portfolio_index.getRootPageId(); }

    int64_t createPortfolioKey(int user_id, const std::string &symbol) const;
    // In UserManager.h, add this public method:
    std::vector<int> getAllUserIds();
};