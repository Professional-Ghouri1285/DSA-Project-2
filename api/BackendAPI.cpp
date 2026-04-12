// api/BackendAPI.cpp - COMPLETE IMPLEMENTATION
#include "BackendAPI.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <random>
#include <tuple>

using namespace std;

// Thread-safe market data mutex
mutex market_data_mutex;

// ===================== CONSTRUCTOR & DESTRUCTOR =====================
// BackendAPI.cpp - FIXED CONSTRUCTOR
// BackendAPI.cpp - FIND THIS CONSTRUCTOR and UPDATE it:
BackendAPI::BackendAPI(FileManager *fm, BufferManager *bm)
    : file_manager(fm), buffer_manager(bm)
{
    cout << "BackendAPI initializing...\n";

    try
    {
        // ==============================================
        // CRITICAL FIX: GET SAVED ROOTS FROM FILEMANAGER
        // ==============================================
        int user_root = fm->getUserTreeRoot();
        int portfolio_root = fm->getPortfolioTreeRoot();

        cout << "DEBUG [BackendAPI]: Saved roots from FileManager:\n";
        cout << "  User root: " << user_root << "\n";
        cout << "  Portfolio root: " << portfolio_root << "\n";

        // ==============================================
        // Initialize UserManager WITH SAVED ROOTS
        // ==============================================
        cout << "Initializing UserManager...\n";
        user_manager = make_unique<UserManager>(bm, nullptr, user_root, portfolio_root);

        // Rest of your code...
        matching_engine = make_unique<MatchingEngine>(bm, nullptr, user_manager.get());
        symbol_index = make_unique<HashIndex>(bm, 101);
        initializeMarketData();

        cout << "BackendAPI initialized successfully.\n";
    }
    catch (const exception &e)
    {
        cerr << "BackendAPI initialization failed: " << e.what() << endl;
        throw;
    }
}

BackendAPI::~BackendAPI()
{
    cout << "BackendAPI shutdown\n";
    // Flush all data before shutdown
    try
    {
        matching_engine->flushAll();
        if (user_manager)
        {
            user_manager->flush();
        }
    }
    catch (const exception &e)
    {
        cerr << "Error during shutdown: " << e.what() << endl;
    }
}

// ===================== PRIVATE HELPER METHODS =====================
void BackendAPI::initializeMarketData()
{
    // Initialize with realistic market prices
    market_prices = {
        {"AAPL", 175.25},
        {"GOOGL", 142.50},
        {"TSLA", 245.75},
        {"MSFT", 330.20},
        {"AMZN", 178.90},
        {"NVDA", 950.00},
        {"META", 485.30},
        {"JPM", 195.45},
        {"V", 275.80},
        {"WMT", 170.25},
        {"PG", 162.30},
        {"JNJ", 151.75},
        {"UNH", 545.20},
        {"HD", 382.15},
        {"BAC", 35.80},
        {"MA", 456.90},
        {"DIS", 95.40},
        {"ADBE", 625.75},
        {"CRM", 298.30},
        {"CSCO", 53.25},
        {"PEP", 175.60},
        {"INTC", 44.85},
        {"CMCSA", 43.90},
        {"TMO", 550.25},
        {"ABT", 112.40},
        {"NKE", 96.75},
        {"ACN", 375.60},
        {"LIN", 455.80},
        {"DHR", 265.90}};

    // Add symbols to matching engine
    for (const auto &pair : market_prices)
    {
        try
        {
            matching_engine->addSymbol(pair.first);

            // Generate a unique key for the symbol index
            size_t hash_val = 0;
            for (char c : pair.first)
            {
                hash_val = hash_val * 31 + c;
            }
            int64_t symbol_key = static_cast<int64_t>(hash_val);

            // Store symbol key mapping
            symbol_index->insert(pair.first, symbol_key);
        }
        catch (const exception &e)
        {
            cerr << "Warning: Failed to add symbol " << pair.first
                 << ": " << e.what() << endl;
        }
    }
}

void BackendAPI::updateMarketPrice(const string &symbol, double price)
{
    lock_guard<mutex> lock(market_data_mutex);
    market_prices[symbol] = price;
}

void BackendAPI::loadSymbols()
{
    // Already loaded in initializeMarketData
    cout << "Market symbols loaded: " << market_prices.size() << endl;
}

bool BackendAPI::validateOrderRequest(const json &request, string &error)
{
    try
    {
        // Check all required fields exist
        vector<string> required_fields = {"user_id", "symbol", "side", "type", "price", "quantity"};
        for (const auto &field : required_fields)
        {
            if (!request.contains(field))
            {
                error = "Missing required field: " + field;
                return false;
            }
        }

        // Validate user_id
        if (!request["user_id"].is_number_integer() || request["user_id"] <= 0)
        {
            error = "Invalid user_id: must be positive integer";
            return false;
        }

        int user_id = request["user_id"];

        // Check if user exists
        User *user = user_manager->getUser(user_id);
        if (!user)
        {
            error = "User with ID " + to_string(user_id) + " not found";
            return false;
        }

        // Validate symbol
        string symbol = request["symbol"];
        if (symbol.empty() || symbol.length() > 7)
        {
            error = "Invalid symbol: must be 1-7 characters";
            return false;
        }

        // Check if symbol exists in market
        if (market_prices.find(symbol) == market_prices.end())
        {
            error = "Symbol '" + symbol + "' not found in market";
            return false;
        }

        // Validate side
        int side = request["side"];
        if (side != SIDE_BUY && side != SIDE_SELL)
        {
            error = "Invalid order side: must be " + to_string(SIDE_BUY) + " (BUY) or " + to_string(SIDE_SELL) + " (SELL)";
            return false;
        }

        // Validate type
        int type = request["type"];
        if (type < ORDER_MARKET || type > ORDER_STOP)
        {
            error = "Invalid order type: must be " + to_string(ORDER_MARKET) + " (MARKET), " + to_string(ORDER_LIMIT) + " (LIMIT), or " + to_string(ORDER_STOP) + " (STOP)";
            return false;
        }

        // Validate price
        double price = request["price"];
        if (price <= 0)
        {
            error = "Price must be positive";
            return false;
        }

        if (price > 1000000.0)
        {
            error = "Price exceeds maximum limit of $1,000,000";
            return false;
        }

        // Validate quantity
        int quantity = request["quantity"];
        if (quantity <= 0)
        {
            error = "Quantity must be positive";
            return false;
        }

        if (quantity > 1000000)
        {
            error = "Quantity exceeds maximum limit of 1,000,000 shares";
            return false;
        }

        // Additional validation for buy orders
        if (side == SIDE_BUY)
        {
            double required_amount = price * quantity;
            double available_balance = user->data.cash_balance;

            if (available_balance < required_amount)
            {
                error = "Insufficient balance. Available: $" + to_string(available_balance) + ", Required: $" + to_string(required_amount);
                return false;
            }
        }

        return true;
    }
    catch (const json::exception &e)
    {
        error = "JSON parsing error: " + string(e.what());
        return false;
    }
    catch (const exception &e)
    {
        error = "Validation error: " + string(e.what());
        return false;
    }
}

json BackendAPI::orderToJson(const Order *order)
{
    json j;
    if (!order)
        return j;

    try
    {
        j["order_id"] = order->order_id;
        j["user_id"] = order->user_id;
        j["symbol"] = string(order->symbol);
        j["side"] = order->side;
        j["type"] = order->type;
        j["status"] = order->status;
        j["price"] = order->price;
        j["quantity"] = order->quantity;
        j["filled_quantity"] = order->filled_quantity;
        j["filled_price"] = order->filled_price;
        j["timestamp"] = order->timestamp;
        j["remaining_quantity"] = order->quantity - order->filled_quantity;

        // Format timestamp for display
        auto tp = chrono::system_clock::time_point(chrono::nanoseconds(order->timestamp));
        auto time = chrono::system_clock::to_time_t(tp);
        stringstream ss;
        ss << put_time(localtime(&time), "%Y-%m-%d %H:%M:%S");
        j["time_str"] = ss.str();
    }
    catch (const exception &e)
    {
        cerr << "Error in orderToJson: " << e.what() << endl;
    }

    return j;
}

json BackendAPI::tradeToJson(const Trade &trade)
{
    json j;

    try
    {
        j["trade_id"] = trade.trade_id;
        j["timestamp"] = trade.timestamp;
        j["symbol"] = trade.symbol;
        j["price"] = trade.price;
        j["quantity"] = trade.quantity;
        j["buy_order_id"] = trade.buy_order_id;
        j["sell_order_id"] = trade.sell_order_id;
        j["buyer_id"] = trade.buyer_id;
        j["seller_id"] = trade.seller_id;
        j["total_value"] = trade.price * trade.quantity;

        // Format timestamp for display
        auto tp = chrono::system_clock::time_point(chrono::nanoseconds(trade.timestamp));
        auto time = chrono::system_clock::to_time_t(tp);
        stringstream ss;
        ss << put_time(localtime(&time), "%Y-%m-%d %H:%M:%S");
        j["time_str"] = ss.str();
    }
    catch (const exception &e)
    {
        cerr << "Error in tradeToJson: " << e.what() << endl;
    }

    return j;
}

json BackendAPI::userToJson(const User *user)
{
    json j;
    if (!user)
        return j;

    try
    {
        j["user_id"] = user->data.user_id;
        j["username"] = user->data.username;
        j["cash_balance"] = user->data.cash_balance;
        j["portfolio_root"] = user->data.portfolio_root;
    }
    catch (const exception &e)
    {
        cerr << "Error in userToJson: " << e.what() << endl;
    }

    return j;
}

// ===================== USER ENDPOINTS =====================
json BackendAPI::handleRegister(const json &request)
{
    json response;

    try
    {
        if (!request.contains("username") || !request.contains("password"))
        {
            response["success"] = false;
            response["error"] = "Missing username or password";
            return response;
        }

        string username = request["username"];
        string password = request["password"];
        double initial_balance = request.value("initial_balance", 10000.0);

        // Validate input
        if (username.empty() || username.length() > 31)
        {
            response["success"] = false;
            response["error"] = "Username must be 1-31 characters";
            return response;
        }

        if (password.empty() || password.length() < 6)
        {
            response["success"] = false;
            response["error"] = "Password must be at least 6 characters";
            return response;
        }

        if (initial_balance < 0 || initial_balance > 1000000.0)
        {
            response["success"] = false;
            response["error"] = "Initial balance must be between $0 and $1,000,000";
            return response;
        }

        // Create user using UserManager::createUser
        int user_id = user_manager->createUser(username, password, initial_balance);

        if (user_id > 0)
        {
            response["success"] = true;
            response["user_id"] = user_id;
            response["username"] = username;
            response["balance"] = initial_balance;
            response["message"] = "User registered successfully";

            cout << "User registered: ID=" << user_id << ", Username=" << username << endl;
        }
        else
        {
            response["success"] = false;
            response["error"] = "Username '" + username + "' already exists";
        }
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Registration failed: ") + e.what();
    }

    return response;
}

json BackendAPI::handleLogin(const json &request)
{
    json response;

    try
    {
        if (!request.contains("user_id") || !request.contains("password"))
        {
            response["success"] = false;
            response["error"] = "Missing user_id or password";
            return response;
        }

        int user_id = request["user_id"];
        string password = request["password"];

        // Authenticate user
        if (user_manager->authenticate(user_id, password))
        {
            User *user = user_manager->getUser(user_id);
            if (user)
            {
                response["success"] = true;
                response["user_id"] = user_id;
                response["username"] = user->data.username;
                response["balance"] = user->data.cash_balance;
                response["message"] = "Login successful";

                cout << "User login: ID=" << user_id << ", Username=" << user->data.username << endl;
            }
            else
            {
                response["success"] = false;
                response["error"] = "User data could not be loaded";
            }
        }
        else
        {
            response["success"] = false;
            response["error"] = "Invalid user_id or password";
        }
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Login failed: ") + e.what();
    }

    return response;
}

json BackendAPI::handleGetUserProfile(int user_id)
{
    json response;

    try
    {
        User *user = user_manager->getUser(user_id);
        if (user)
        {
            response = userToJson(user);

            // Add additional user info
            double portfolio_value = 0.0;
            auto portfolio = user_manager->getPortfolio(user_id);
            for (const auto &entry : portfolio)
            {
                auto it = market_prices.find(entry.symbol);
                if (it != market_prices.end())
                {
                    portfolio_value += entry.quantity * it->second;
                }
            }

            response["portfolio_value"] = portfolio_value;
            response["total_value"] = user->data.cash_balance + portfolio_value;
            response["portfolio_count"] = portfolio.size();
            response["success"] = true;
        }
        else
        {
            response["success"] = false;
            response["error"] = "User with ID " + to_string(user_id) + " not found";
        }
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get user profile: ") + e.what();
    }

    return response;
}

// ===================== TRADING ENDPOINTS =====================
json BackendAPI::handlePlaceOrder(const json &request)
{
    json response;

    try
    {
        string error;
        if (!validateOrderRequest(request, error))
        {
            response["success"] = false;
            response["error"] = error;
            return response;
        }

        // Create Order object
        Order order;
        order.order_id = 0;
        order.user_id = request["user_id"];

        string symbol_str = request["symbol"];
        strncpy(order.symbol, symbol_str.c_str(), sizeof(order.symbol) - 1);
        order.symbol[sizeof(order.symbol) - 1] = '\0';

        order.side = static_cast<OrderSide>(request["side"]);
        order.type = static_cast<OrderType>(request["type"]);
        order.price = request["price"];
        order.quantity = request["quantity"];
        order.status = STATUS_PENDING;
        order.timestamp = chrono::duration_cast<chrono::nanoseconds>(
                              chrono::system_clock::now().time_since_epoch())
                              .count();
        order.filled_quantity = 0;
        order.filled_price = 0.0;

        cout << "Placing order: User=" << order.user_id
             << ", Symbol=" << order.symbol
             << ", Side=" << (order.side == SIDE_BUY ? "BUY" : "SELL")
             << ", Type=" << order.type
             << ", Price=$" << order.price
             << ", Qty=" << order.quantity << endl;

        // Place order
        int64_t order_id = matching_engine->placeOrder(order);

        if (order_id > 0)
        {
            response["success"] = true;
            response["order_id"] = order_id;
            response["symbol"] = symbol_str;
            response["side"] = order.side;
            response["price"] = order.price;
            response["quantity"] = order.quantity;
            response["timestamp"] = order.timestamp;
            response["message"] = "Order placed successfully";

            cout << "Order placed successfully: ID=" << order_id << endl;
        }
        else
        {
            response["success"] = false;
            response["error"] = "Failed to place order in matching engine";
        }
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Order placement failed: ") + e.what();
        cerr << "Order placement error: " << e.what() << endl;
    }

    return response;
}

json BackendAPI::handleCancelOrder(const json &request)
{
    json response;

    try
    {
        if (!request.contains("symbol") || !request.contains("order_id"))
        {
            response["success"] = false;
            response["error"] = "Missing symbol or order_id";
            return response;
        }

        string symbol = request["symbol"];
        int64_t order_id = request["order_id"];

        if (market_prices.find(symbol) == market_prices.end())
        {
            response["success"] = false;
            response["error"] = "Symbol '" + symbol + "' not found";
            return response;
        }

        cout << "Cancelling order: ID=" << order_id << ", Symbol=" << symbol << endl;

        bool success = matching_engine->cancelOrder(symbol, order_id);

        if (success)
        {
            response["success"] = true;
            response["order_id"] = order_id;
            response["symbol"] = symbol;
            response["message"] = "Order cancelled successfully";

            cout << "Order cancelled successfully: ID=" << order_id << endl;
        }
        else
        {
            response["success"] = false;
            response["error"] = "Failed to cancel order. Order not found, already filled, or already cancelled";
        }
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Order cancellation failed: ") + e.what();
        cerr << "Order cancellation error: " << e.what() << endl;
    }

    return response;
}

json BackendAPI::handleGetOrderBook(const string &symbol, int depth)
{
    json response;

    try
    {
        if (symbol.empty())
        {
            response["success"] = false;
            response["error"] = "Symbol parameter is required";
            return response;
        }

        if (market_prices.find(symbol) == market_prices.end())
        {
            response["success"] = false;
            response["error"] = "Symbol '" + symbol + "' not found";
            return response;
        }

        PersistentOrderBook *book = matching_engine->getOrderBook(symbol);

        if (!book)
        {
            response["success"] = false;
            response["error"] = "No order book available for symbol '" + symbol + "'";
            return response;
        }

        double current_price = market_prices[symbol];

        response["symbol"] = symbol;
        response["current_price"] = current_price;
        response["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
                                    chrono::system_clock::now().time_since_epoch())
                                    .count();
        response["depth"] = depth;
        response["bids"] = json::array();
        response["asks"] = json::array();
        response["market_summary"] = {
            {"symbol", symbol},
            {"price", current_price},
            {"volume", 0},
            {"spread", 0.0}};
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get order book: ") + e.what();
    }

    return response;
}

json BackendAPI::handleGetTrades(const string &symbol, int limit)
{
    json response;

    try
    {
        if (symbol.empty())
        {
            response["success"] = false;
            response["error"] = "Symbol parameter is required";
            return response;
        }

        if (market_prices.find(symbol) == market_prices.end())
        {
            response["success"] = false;
            response["error"] = "Symbol '" + symbol + "' not found";
            return response;
        }

        if (limit <= 0 || limit > 1000)
        {
            limit = 50;
        }

        vector<Trade> trades = matching_engine->getTradeHistory(symbol, limit);

        json trades_json = json::array();
        double total_volume = 0.0;
        double total_value = 0.0;

        for (const auto &trade : trades)
        {
            json trade_json = tradeToJson(trade);
            trades_json.push_back(trade_json);

            total_volume += trade.quantity;
            total_value += trade.price * trade.quantity;
        }

        response["symbol"] = symbol;
        response["trades"] = trades_json;
        response["count"] = trades_json.size();
        response["total_volume"] = total_volume;
        response["total_value"] = total_value;
        response["limit"] = limit;
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get trades: ") + e.what();
    }

    return response;
}

// ===================== PORTFOLIO ENDPOINTS =====================
json BackendAPI::handleGetPortfolio(int user_id)
{
    json response;

    try
    {
        User *user = user_manager->getUser(user_id);
        if (!user)
        {
            response["success"] = false;
            response["error"] = "User with ID " + to_string(user_id) + " not found";
            return response;
        }

        vector<PortfolioEntry> portfolio = user_manager->getPortfolio(user_id);

        json portfolio_json = json::array();
        double total_investment = 0.0;
        double total_current_value = user->data.cash_balance;
        double total_unrealized_pnl = 0.0;

        for (const auto &entry : portfolio)
        {
            json holding;
            string symbol_str(entry.symbol);

            holding["symbol"] = symbol_str;
            holding["quantity"] = entry.quantity;
            holding["avg_price"] = entry.avg_price;

            // Calculate investment and current value
            double investment = entry.quantity * entry.avg_price;
            holding["total_investment"] = investment;
            total_investment += investment;

            double current_price = 0.0;
            auto it = market_prices.find(symbol_str);
            if (it != market_prices.end())
            {
                current_price = it->second;
            }
            else
            {
                current_price = entry.avg_price;
            }

            holding["current_price"] = current_price;

            double current_value = entry.quantity * current_price;
            holding["current_value"] = current_value;
            total_current_value += current_value;

            double unrealized_pnl = current_value - investment;
            holding["unrealized_pnl"] = unrealized_pnl;

            if (entry.avg_price > 0)
            {
                holding["unrealized_pnl_percent"] = (unrealized_pnl / investment) * 100;
            }
            else
            {
                holding["unrealized_pnl_percent"] = 0.0;
            }

            total_unrealized_pnl += unrealized_pnl;

            portfolio_json.push_back(holding);
        }

        double total_portfolio_value = user->data.cash_balance + (total_current_value - total_investment);

        response["user_id"] = user_id;
        response["username"] = user->data.username;
        response["portfolio"] = portfolio_json;
        response["cash_balance"] = user->data.cash_balance;
        response["total_investment"] = total_investment;
        response["total_current_value"] = total_current_value;
        response["total_unrealized_pnl"] = total_unrealized_pnl;
        response["total_portfolio_value"] = total_portfolio_value;
        response["holdings_count"] = portfolio.size();
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get portfolio: ") + e.what();
    }

    return response;
}

json BackendAPI::handleGetBalance(int user_id)
{
    json response;

    try
    {
        double balance = user_manager->getBalance(user_id);

        response["user_id"] = user_id;
        response["balance"] = balance;
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get balance: ") + e.what();
    }

    return response;
}

// ===================== MARKET DATA ENDPOINTS =====================
json BackendAPI::handleGetSymbols()
{
    json response;

    try
    {
        json symbols_json = json::array();

        for (const auto &pair : market_prices)
        {
            json symbol_info;
            symbol_info["symbol"] = pair.first;
            symbol_info["price"] = pair.second;
            symbol_info["change"] = 0.0;
            symbol_info["volume"] = 0;

            symbols_json.push_back(symbol_info);
        }

        response["symbols"] = symbols_json;
        response["count"] = symbols_json.size();
        response["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
                                    chrono::system_clock::now().time_since_epoch())
                                    .count();
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get symbols: ") + e.what();
    }

    return response;
}

json BackendAPI::handleGetMarketData()
{
    json response;

    try
    {
        json market_data = json::object();

        for (const auto &pair : market_prices)
        {
            json symbol_data;
            symbol_data["price"] = pair.second;
            symbol_data["volume"] = 0;
            symbol_data["change"] = 0.0;
            symbol_data["high"] = pair.second * 1.02;
            symbol_data["low"] = pair.second * 0.98;
            symbol_data["open"] = pair.second;

            market_data[pair.first] = symbol_data;
        }

        response["market_data"] = market_data;
        response["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
                                    chrono::system_clock::now().time_since_epoch())
                                    .count();
        response["symbol_count"] = market_prices.size();
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get market data: ") + e.what();
    }

    return response;
}

json BackendAPI::handleSearchSymbols(const string &query)
{
    json response;

    try
    {
        if (query.empty())
        {
            response["success"] = false;
            response["error"] = "Search query cannot be empty";
            return response;
        }

        json results = json::array();

        string query_lower = query;
        transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

        for (const auto &pair : market_prices)
        {
            string symbol_lower = pair.first;
            transform(symbol_lower.begin(), symbol_lower.end(), symbol_lower.begin(), ::tolower);

            if (symbol_lower.find(query_lower) == 0)
            {
                json symbol_info;
                symbol_info["symbol"] = pair.first;
                symbol_info["price"] = pair.second;

                results.push_back(symbol_info);
            }
        }

        response["query"] = query;
        response["results"] = results;
        response["count"] = results.size();
        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to search symbols: ") + e.what();
    }

    return response;
}

// ===================== REAL LEADERBOARD IMPLEMENTATION =====================
json BackendAPI::handleGetLeaderboard(int limit)
{
    json response;

    try
    {
        if (limit <= 0 || limit > 100)
        {
            limit = 10;
        }

        cout << "Generating REAL leaderboard with limit: " << limit << endl;

        // Step 1: Get ALL ACTUAL user IDs from UserManager
        vector<int> allUserIds = user_manager->getAllUserIds();

        if (allUserIds.empty())
        {
            // No users found - return empty but successful response
            response["leaderboard"] = json::array();
            response["message"] = "No users found in the system";
            response["count"] = 0;
            response["success"] = true;
            return response;
        }

        cout << "Processing " << allUserIds.size() << " real users for leaderboard..." << endl;

        // Step 2: Calculate total value for each user
        vector<tuple<int, string, double, double>> user_data; // user_id, username, cash, total_value

        for (int user_id : allUserIds)
        {
            try
            {
                User *user = user_manager->getUser(user_id);
                if (!user)
                {
                    continue; // Skip if user can't be loaded
                }

                double total_value = user->data.cash_balance;
                string username = user->data.username;

                // Calculate portfolio value
                vector<PortfolioEntry> portfolio = user_manager->getPortfolio(user_id);
                for (const auto &entry : portfolio)
                {
                    string symbol_str(entry.symbol);
                    auto it = market_prices.find(symbol_str);

                    if (it != market_prices.end())
                    {
                        total_value += entry.quantity * it->second;
                    }
                    else
                    {
                        total_value += entry.quantity * entry.avg_price;
                    }
                }

                user_data.push_back({user_id, username, user->data.cash_balance, total_value});
            }
            catch (const exception &e)
            {
                cerr << "Error processing user " << user_id << ": " << e.what() << endl;
            }
        }

        // Step 3: Sort by total value (descending)
        sort(user_data.begin(), user_data.end(),
             [](const tuple<int, string, double, double> &a,
                const tuple<int, string, double, double> &b)
             {
                 return get<3>(a) > get<3>(b);
             });

        // Step 4: Build leaderboard JSON
        json leaderboard = json::array();
        int rank = 1;

        for (size_t i = 0; i < min(user_data.size(), static_cast<size_t>(limit)); i++)
        {
            auto &[user_id, username, cash_balance, total_value] = user_data[i];

            json entry;
            entry["rank"] = rank++;
            entry["user_id"] = user_id;
            entry["username"] = username;
            entry["total_value"] = total_value;
            entry["cash_balance"] = cash_balance;
            entry["portfolio_value"] = total_value - cash_balance;

            // Get portfolio holdings count
            try
            {
                vector<PortfolioEntry> portfolio = user_manager->getPortfolio(user_id);
                entry["holdings_count"] = portfolio.size();

                // Add top holdings
                json top_holdings = json::array();
                for (size_t j = 0; j < min(portfolio.size(), static_cast<size_t>(3)); j++)
                {
                    json holding;
                    holding["symbol"] = portfolio[j].symbol;
                    holding["quantity"] = portfolio[j].quantity;
                    holding["avg_price"] = portfolio[j].avg_price;

                    // Get current price
                    string symbol_str(portfolio[j].symbol);
                    auto it = market_prices.find(symbol_str);
                    if (it != market_prices.end())
                    {
                        holding["current_price"] = it->second;
                        holding["current_value"] = portfolio[j].quantity * it->second;
                    }

                    top_holdings.push_back(holding);
                }
                entry["top_holdings"] = top_holdings;
            }
            catch (const exception &e)
            {
                entry["holdings_count"] = 0;
                entry["top_holdings"] = json::array();
            }

            leaderboard.push_back(entry);
        }

        // Step 5: Build final response
        response["leaderboard"] = leaderboard;
        response["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
                                    chrono::system_clock::now().time_since_epoch())
                                    .count();
        response["total_users"] = allUserIds.size();
        response["leaderboard_size"] = leaderboard.size();
        response["limit"] = limit;
        response["success"] = true;

        cout << "Leaderboard generated with " << leaderboard.size()
             << " entries from " << allUserIds.size() << " total users" << endl;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to generate leaderboard: ") + e.what();
        cerr << "Leaderboard generation error: " << e.what() << endl;
    }

    return response;
}

// ===================== SYSTEM ENDPOINTS =====================
json BackendAPI::handleGetSystemStatus()
{
    json response;

    try
    {
        // Gather system statistics
        time_t now = time(nullptr);

        response["status"] = "online";
        response["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
                                    chrono::system_clock::now().time_since_epoch())
                                    .count();

        // Convert time to string
        char time_str[100];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
        response["time_string"] = time_str;

        response["market"] = {
            {"total_symbols", market_prices.size()},
            {"total_trades", 0},  // You would track this in MatchingEngine
            {"total_volume", 0.0} // You would track this in MatchingEngine
        };

        response["users"] = {
            {"total_users", 0}, // You could add this count
            {"active_users", 0}};

        response["performance"] = {
            {"uptime", 0}, // You could track this
            {"memory_usage", 0}};

        response["success"] = true;
    }
    catch (const exception &e)
    {
        response["success"] = false;
        response["error"] = string("Failed to get system status: ") + e.what();
    }

    return response;
}

// ===================== UTILITY METHOD (for company names) =====================
string BackendAPI::getCompanyName(const string &symbol)
{
    // Simple mapping of symbols to company names
    static const unordered_map<string, string> company_names = {
        {"AAPL", "Apple Inc."},
        {"GOOGL", "Alphabet Inc."},
        {"TSLA", "Tesla Inc."},
        {"MSFT", "Microsoft Corporation"},
        {"AMZN", "Amazon.com Inc."},
        {"NVDA", "NVIDIA Corporation"},
        {"META", "Meta Platforms Inc."},
        {"JPM", "JPMorgan Chase & Co."},
        {"V", "Visa Inc."},
        {"WMT", "Walmart Inc."},
        {"PG", "Procter & Gamble Co."},
        {"JNJ", "Johnson & Johnson"},
        {"UNH", "UnitedHealth Group Inc."},
        {"HD", "Home Depot Inc."},
        {"BAC", "Bank of America Corp."},
        {"MA", "Mastercard Inc."},
        {"DIS", "Walt Disney Co."},
        {"ADBE", "Adobe Inc."},
        {"CRM", "Salesforce Inc."},
        {"CSCO", "Cisco Systems Inc."},
        {"PEP", "PepsiCo Inc."},
        {"INTC", "Intel Corporation"},
        {"CMCSA", "Comcast Corporation"},
        {"TMO", "Thermo Fisher Scientific Inc."},
        {"ABT", "Abbott Laboratories"},
        {"NKE", "Nike Inc."},
        {"ACN", "Accenture plc"},
        {"LIN", "Linde plc"},
        {"DHR", "Danaher Corporation"}};

    auto it = company_names.find(symbol);
    if (it != company_names.end())
    {
        return it->second;
    }
    return symbol + " Corporation";
}