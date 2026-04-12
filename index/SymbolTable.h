#pragma once

#include "HashIndex.h"
#include "BufferManager.h"
#include <unordered_map>
#include <string>
#include <vector>

class SymbolTable
{
public:
    SymbolTable(BufferManager *bm);

    // Stock symbols
    bool addStock(const std::string &symbol, const std::string &name,
                  double initial_price);
    int getStockId(const std::string &symbol);
    std::string getStockSymbol(int stock_id);
    std::string getStockName(int stock_id);
    double getStockPrice(int stock_id);
    bool updateStockPrice(int stock_id, double new_price);

    // User accounts
    bool addUser(const std::string &username, const std::string &password_hash,
                 double initial_balance);
    int getUserId(const std::string &username);
    bool validateUser(const std::string &username, const std::string &password_hash);
    double getUserBalance(int user_id);
    bool updateUserBalance(int user_id, double delta);

    // Portfolio tracking
    void updatePortfolio(int user_id, int stock_id, int quantity_change);
    int getPortfolioQuantity(int user_id, int stock_id);
    std::vector<std::pair<int, int>> getUserPortfolio(int user_id); // stock_id, quantity

    // Statistics
    int getTotalStocks() const { return next_stock_id; }
    int getTotalUsers() const { return next_user_id; }
    void printStatistics() const;

    // Persistence
    bool saveToDisk();
    bool loadFromDisk();

private:
    BufferManager *buffer_manager;
    HashIndex symbol_index;    // symbol -> stock_id
    HashIndex user_index;      // username -> user_id
    HashIndex portfolio_index; // (user_id << 32) | stock_id -> quantity

    // In-memory caches for fast lookup
    struct StockInfo
    {
        int id;
        std::string symbol;
        std::string name;
        double current_price;
        int volume_traded;
    };

    struct UserInfo
    {
        int id;
        std::string username;
        std::string password_hash;
        double cash_balance;
    };

    // Caches
    std::unordered_map<std::string, StockInfo> symbol_to_stock;
    std::unordered_map<int, StockInfo> id_to_stock;
    std::unordered_map<std::string, UserInfo> username_to_user;
    std::unordered_map<int, UserInfo> id_to_user;
    std::unordered_map<int64_t, int> portfolio_quantities; // key = (user_id << 32) | stock_id

    int next_stock_id;
    int next_user_id;

    // Constants for page storage
    static const int STOCK_DATA_PAGE_START = 1000;
    static const int USER_DATA_PAGE_START = 2000;
    static const int PORTFOLIO_DATA_PAGE_START = 3000;

    // Private methods
    void serializeStock(const StockInfo &stock, char *buffer);
    StockInfo deserializeStock(const char *buffer);
    void saveStockToPage(int stock_id, const StockInfo &stock);
    StockInfo loadStockFromPage(int stock_id);

    void serializeUser(const UserInfo &user, char *buffer);
    UserInfo deserializeUser(const char *buffer);
    void saveUserToPage(int user_id, const UserInfo &user);
    UserInfo loadUserFromPage(int user_id);

    int64_t makePortfolioKey(int user_id, int stock_id);
    void parsePortfolioKey(int64_t key, int &user_id, int &stock_id);
};