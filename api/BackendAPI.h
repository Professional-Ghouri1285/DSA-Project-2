// api/BackendAPI.h
#pragma once

#include "../core/MatchingEngine.h"
#include "../core/UserManager.h"
#include "../index/HashIndex.h"
#include <string>
#include <unordered_map>
#include "../json.hpp"

using namespace std;

using json = nlohmann::json;

class BackendAPI
{
public:
    BackendAPI(FileManager *fm, BufferManager *bm);
    ~BackendAPI();

    // User endpoints
    json handleRegister(const json &request);
    json handleLogin(const json &request);
    json handleGetUserProfile(int user_id);

    // Trading endpoints
    json handlePlaceOrder(const json &request);
    json handleCancelOrder(const json &request);
    json handleGetOrderBook(const string &symbol, int depth = 10);
    json handleGetTrades(const string &symbol, int limit = 50);

    // Portfolio endpoints
    json handleGetPortfolio(int user_id);
    json handleGetBalance(int user_id);

    // Market data endpoints
    json handleGetSymbols();
    json handleGetMarketData();
    json handleSearchSymbols(const string &query);

    // Leaderboard endpoints
    json handleGetLeaderboard(int limit = 10);

    // System endpoints
    json handleGetSystemStatus();

private:
    FileManager *file_manager;
    BufferManager *buffer_manager;
    unique_ptr<UserManager> user_manager;
    unique_ptr<MatchingEngine> matching_engine;
    unique_ptr<HashIndex> symbol_index;

    // Market data cache
    unordered_map<string, double> market_prices;

    void initializeMarketData();
    void updateMarketPrice(const string &symbol, double price);
    void loadSymbols();

    // Helper methods
    bool validateOrderRequest(const json &request, string &error);
    json orderToJson(const Order *order);
    json tradeToJson(const Trade &trade);
    json userToJson(const User *user);
    string getCompanyName(const string &symbol);
};