#include "UserManager.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <functional>
#include <list>

using namespace std;

// In UserManager.cpp - Update constructor
// Update UserManager constructor:
UserManager::UserManager(BufferManager *bm, WriteAheadLog *w,
                         int user_root, int portfolio_root)
    : buffer_manager(bm), wal(w),
      user_index(bm, user_root),
      portfolio_index(bm, portfolio_root)
{
    cout << "DEBUG: UserManager constructor called\n";
    cout << "DEBUG: User root: " << user_root << endl;
    cout << "DEBUG: Portfolio root: " << portfolio_root << endl;

    // Try to find max user ID if trees exist
    if (user_root != -1)
    {
        // Get all user IDs to find max
        auto userIds = getAllUserIds();
        if (!userIds.empty())
        {
            next_user_id = *std::max_element(userIds.begin(), userIds.end()) + 1;
            cout << "DEBUG: Set next_user_id to " << next_user_id
                 << " (max found: " << (next_user_id - 1) << ")\n";
        }
        else
        {
            next_user_id = 1000;
            cout << "DEBUG: No users found, set next_user_id to 1000\n";
        }
    }
    else
    {
        next_user_id = 1000;
        cout << "DEBUG: New tree, set next_user_id to 1000\n";
    }
}
// FIX: Add UserData constructor - REMOVE THIS since it's not in header
// UserData::UserData(int uid, const std::string &uname, double balance) ...
// Instead, just use the default constructor

// Helper: Create composite key for portfolio (user_id in upper 32 bits, symbol hash in lower)
int64_t UserManager::createPortfolioKey(int user_id, const std::string &symbol) const
{
    // DEBUG: Show what's being calculated
    std::cout << "DEBUG createPortfolioKey: user_id=" << user_id
              << ", symbol='" << symbol << "'" << std::endl;

    std::hash<std::string> hasher;
    size_t symbol_hash = hasher(symbol);

    int64_t user_part = static_cast<int64_t>(user_id) << 32;
    int64_t symbol_part = static_cast<int64_t>(symbol_hash & 0xFFFFFFFF);
    int64_t key = user_part | symbol_part;

    std::cout << "DEBUG: user_part=0x" << std::hex << user_part
              << ", symbol_hash=0x" << symbol_hash
              << ", symbol_part=0x" << symbol_part
              << ", final_key=0x" << key << std::dec << std::endl;

    return key;
}

// Helper: Store data to a new page - FIXED
// Helper: Store data to a new page - FIXED with timeout check
int UserManager::storeDataPage(const char *data, size_t size, PageType type)
{
    // Try to allocate page with timeout
    Page *page = nullptr;

    // Simple attempt - if allocatePage hangs, we need to check why
    try
    {
        page = buffer_manager->allocatePage();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error allocating page: " << e.what() << std::endl;
        return -1;
    }

    if (!page)
    {
        std::cerr << "Failed to allocate page" << std::endl;
        return -1;
    }

    int page_id = page->getPageId();
    if (page_id < 0)
    {
        std::cerr << "Invalid page ID: " << page_id << std::endl;
        return -1;
    }

    // Copy data to page body (data area)
    char *page_data = page->getData();
    if (!page_data)
    {
        std::cerr << "Failed to get page data" << std::endl;
        return -1;
    }

    // Copy data to page (truncate if too large)
    size_t copy_size = std::min(size, static_cast<size_t>(Page::DATA_SIZE));
    memcpy(page_data, data, copy_size);

    // Update header
    PageHeader header = page->getHeader();
    header.page_type = static_cast<int>(type);
    header.page_id = page_id;
    page->setHeader(header);

    buffer_manager->markDirty(page_id);
    buffer_manager->unpinPage(page_id);

    std::cout << "DEBUG: storeDataPage allocated page " << page_id
              << " for type " << type << std::endl;

    return page_id;
}

// Helper: Load data from page - FIXED
bool UserManager::loadDataPage(int page_id, char *buffer, size_t size)
{
    Page *page = buffer_manager->getPage(page_id);
    if (!page)
        return false;

    char *page_data = page->getData();
    if (!page_data)
        return false;

    size_t copy_size = std::min(size, static_cast<size_t>(Page::DATA_SIZE));
    memcpy(buffer, page_data, copy_size);

    buffer_manager->unpinPage(page_id);
    return true;
}

// Store UserData to disk
int UserManager::storeUserData(const UserData &user)
{
    char buffer[UserData::SERIALIZED_SIZE];
    user.serialize(buffer);

    int page_id = storeDataPage(buffer, UserData::SERIALIZED_SIZE, PAGE_TYPE_DATA);
    if (page_id == -1)
        return -1;

    // Store page_id in user_index
    user_index.insert(static_cast<int64_t>(user.user_id), page_id);

    return page_id;
}

// Load UserData from disk
bool UserManager::loadUserData(int user_id, UserData &user)
{
    auto result = user_index.search(static_cast<int64_t>(user_id));
    if (!result.first)
        return false;

    int page_id = static_cast<int>(result.second);
    char buffer[UserData::SERIALIZED_SIZE];

    if (!loadDataPage(page_id, buffer, UserData::SERIALIZED_SIZE))
        return false;

    user = UserData::deserialize(buffer);
    return true;
}

// Store PortfolioEntry to disk
int UserManager::storePortfolioEntry(int user_id, const std::string &symbol, const PortfolioEntry &entry)
{
    if (entry.quantity == 0)
    {
        // Remove if quantity zero
        int64_t key = createPortfolioKey(user_id, symbol);
        portfolio_index.remove(key);
        return -1;
    }

    char buffer[PortfolioEntry::SERIALIZED_SIZE];
    entry.serialize(buffer);

    int page_id = storeDataPage(buffer, PortfolioEntry::SERIALIZED_SIZE, PAGE_TYPE_DATA);
    if (page_id == -1)
        return -1;

    int64_t key = createPortfolioKey(user_id, symbol);
    portfolio_index.insert(key, page_id);

    return page_id;
}

// Load PortfolioEntry from disk
bool UserManager::loadPortfolioEntry(int user_id, const std::string &symbol, PortfolioEntry &entry)
{
    int64_t key = createPortfolioKey(user_id, symbol);
    auto result = portfolio_index.search(key);
    if (!result.first)
        return false;

    int page_id = static_cast<int>(result.second);
    char buffer[PortfolioEntry::SERIALIZED_SIZE];

    if (!loadDataPage(page_id, buffer, PortfolioEntry::SERIALIZED_SIZE))
        return false;

    entry = PortfolioEntry::deserialize(buffer);
    return true;
}

// Load all portfolios for a user - FIXED VERSION
void UserManager::loadAllPortfolios(int user_id)
{
    cout << "DEBUG: loadAllPortfolios for user " << user_id << endl;

    // Clear cache for this user
    portfolio_cache[user_id].clear();

    try
    {
        // Create proper range for this user
        int64_t start_key = static_cast<int64_t>(user_id) << 32;
        int64_t end_key = (static_cast<int64_t>(user_id + 1) << 32) - 1; // Correct end range

        cout << "DEBUG: Searching portfolio range " << std::hex << start_key
             << " to " << end_key << std::dec << endl;
        cout << "DEBUG: Start key decimal: " << start_key
             << ", End key decimal: " << end_key << endl;

        // Direct range query without preliminary tests
        auto all_results = portfolio_index.rangeQuery(start_key, end_key);
        cout << "DEBUG: Full range query returned " << all_results.size() << " results" << endl;

        // Process results
        int loaded = 0;
        for (const auto &[key, page_id] : all_results)
        {
            char buffer[PortfolioEntry::SERIALIZED_SIZE];
            if (loadDataPage(page_id, buffer, PortfolioEntry::SERIALIZED_SIZE))
            {
                PortfolioEntry entry = PortfolioEntry::deserialize(buffer);
                portfolio_cache[user_id][entry.symbol] = entry;
                loaded++;

                cout << "DEBUG: Loaded portfolio entry - Key: " << std::hex << key
                     << std::dec << ", Symbol: " << entry.symbol
                     << ", Qty: " << entry.quantity << ", Price: $" << entry.avg_price << endl;
            }
        }

        cout << "DEBUG: Successfully loaded " << loaded
             << " portfolio entries for user " << user_id << endl;
    }
    catch (const std::exception &e)
    {
        cerr << "ERROR in loadAllPortfolios: " << e.what() << endl;
        // Continue with empty portfolio
    }
}

// Cache eviction
void UserManager::evictOldestUser()
{
    if (user_cache.size() <= MAX_USER_CACHE)
        return;

    // Simple LRU: remove first entry (oldest)
    auto it = user_cache.begin();
    if (it != user_cache.end())
    {
        // Save to disk before evicting
        storeUserData(it->second.data);
        user_cache.erase(it);
    }
}

void UserManager::evictOldestPortfolio(int user_id)
{
    auto it = portfolio_cache.find(user_id);
    if (it == portfolio_cache.end())
        return;

    if (it->second.size() <= MAX_PORTFOLIO_CACHE)
        return;

    // Remove first entry
    auto port_it = it->second.begin();
    if (port_it != it->second.end())
    {
        // Save to disk before evicting
        storePortfolioEntry(user_id, port_it->first, port_it->second);
        it->second.erase(port_it);
    }
}

// Constructor

// Destructor

// Create new user
int UserManager::createUser(const std::string &username, const std::string &password, double initial_balance)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // Check if username exists (linear scan cache, in production use secondary index)
    for (const auto &pair : user_cache)
    {
        if (strcmp(pair.second.data.username, username.c_str()) == 0)
        {
            std::cerr << "Username already exists: " << username << std::endl;
            return -1;
        }
    }

    int user_id = next_user_id.fetch_add(1);

    // Create user data
    UserData user_data;
    user_data.user_id = user_id;
    user_data.cash_balance = initial_balance;
    user_data.portfolio_root = -1;
    strncpy(user_data.username, username.c_str(), sizeof(user_data.username) - 1);
    user_data.username[sizeof(user_data.username) - 1] = '\0';

    // Hash password
    std::hash<std::string> hasher;
    std::string hash_str = std::to_string(hasher(password));
    strncpy(user_data.password_hash, hash_str.c_str(), sizeof(user_data.password_hash) - 1);
    user_data.password_hash[sizeof(user_data.password_hash) - 1] = '\0';

    // Store to disk
    int page_id = storeUserData(user_data);
    if (page_id == -1)
    {
        std::cerr << "Failed to store user to disk: " << username << std::endl;
        return -1;
    }

    // Update cache
    User user(user_data);
    user_cache[user_id] = user;

    // Log to WAL
    if (wal)
    {
        char buffer[256];
        int len = snprintf(buffer, sizeof(buffer), "CREATE_USER %d %s %.2f",
                           user_id, username.c_str(), initial_balance);
        wal->append(LOG_INSERT, user_id, buffer, len + 1);
    }

    evictOldestUser();

    std::cout << "Created user: " << username << " (ID: " << user_id
              << ") with balance: $" << std::fixed << std::setprecision(2)
              << initial_balance << std::endl;

    return user_id;
}

// Authenticate user
bool UserManager::authenticate(int user_id, const std::string &password)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    UserData user_data;
    if (!loadUserData(user_id, user_data))
    {
        return false;
    }

    std::hash<std::string> hasher;
    std::string input_hash = std::to_string(hasher(password));

    return strcmp(user_data.password_hash, input_hash.c_str()) == 0;
}

// Get user (loads from cache or disk)
// Get user (loads from cache or disk) - WITHOUT LOCK
User *UserManager::getUser(int user_id)
{
    // NO LOCK HERE - caller should already have lock

    std::cout << "DEBUG: getUser called for user " << user_id << std::endl;

    // Check cache first
    auto it = user_cache.find(user_id);
    if (it != user_cache.end())
    {
        std::cout << "DEBUG: Found user " << user_id << " in cache" << std::endl;
        return &it->second;
    }

    std::cout << "DEBUG: User " << user_id << " not in cache, loading from disk..." << std::endl;

    // Load from disk
    UserData user_data;
    if (!loadUserData(user_id, user_data))
    {
        std::cout << "DEBUG: User " << user_id << " not found in disk" << std::endl;
        return nullptr;
    }

    std::cout << "DEBUG: Loaded user data from disk" << std::endl;

    // Create user in cache
    User user(user_data);

    // CRITICAL FIX: Load portfolios from disk before checking cache
    loadAllPortfolios(user_id);

    // Now get portfolio from cache (should be populated by loadAllPortfolios)
    auto port_it = portfolio_cache.find(user_id);
    if (port_it != portfolio_cache.end())
    {
        user.portfolio = port_it->second;
        std::cout << "DEBUG: Loaded " << user.portfolio.size() << " portfolio entries from disk" << std::endl;
    }
    else
    {
        // Initialize empty portfolio
        portfolio_cache[user_id] = std::unordered_map<std::string, PortfolioEntry>();
        std::cout << "DEBUG: Created empty portfolio for user " << user_id << std::endl;
    }

    user_cache[user_id] = user;

    std::cout << "DEBUG: Added user " << user_id << " to cache with portfolio" << std::endl;

    return &user_cache[user_id];
}

// Update user balance
bool UserManager::updateBalance(int user_id, double amount)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    User *user = getUser(user_id);
    if (!user)
    {
        std::cerr << "User not found: " << user_id << std::endl;
        return false;
    }

    if (!user->updateBalance(amount))
    {
        std::cerr << "Insufficient funds for user " << user_id
                  << ": Attempted $" << std::fixed << std::setprecision(2) << amount
                  << ", Balance: $" << user->data.cash_balance << std::endl;
        return false;
    }

    // Update disk
    if (storeUserData(user->data) == -1)
    {
        std::cerr << "Failed to update user balance on disk: " << user_id << std::endl;
        return false;
    }

    // Log to WAL
    if (wal)
    {
        char buffer[128];
        int len = snprintf(buffer, sizeof(buffer), "UPDATE_BALANCE %d %.2f", user_id, amount);
        wal->append(LOG_UPDATE, user_id, buffer, len + 1);
    }

    std::cout << "Updated balance for user " << user_id
              << ": $" << std::fixed << std::setprecision(2) << amount
              << " (New balance: $" << user->data.cash_balance << ")" << std::endl;

    return true;
}

// Update portfolio
// Update portfolio - FIXED VERSION
// Update portfolio - FIXED VERSION (without cash handling)
// Update portfolio - FIXED VERSION (WITH cash handling for direct buys/sells)
bool UserManager::updatePortfolio(int user_id, const std::string &symbol,
                                  int quantity_change, double price)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    cout << "DEBUG: updatePortfolio called for user " << user_id
         << ", symbol " << symbol << ", qty " << quantity_change
         << ", price " << price << endl;

    if (quantity_change == 0)
        return true;

    // Get user
    User *user = nullptr;
    auto cache_it = user_cache.find(user_id);
    if (cache_it == user_cache.end())
    {
        // Load user data from disk
        UserData user_data;
        if (!loadUserData(user_id, user_data))
        {
            cerr << "User not found: " << user_id << endl;
            return false;
        }

        // Create new user with empty portfolio
        user = &user_cache[user_id];
        *user = User(user_data);
        portfolio_cache[user_id] = std::unordered_map<std::string, PortfolioEntry>();
    }
    else
    {
        user = &cache_it->second;
    }

    // Ensure portfolio cache exists for this user
    if (portfolio_cache.find(user_id) == portfolio_cache.end())
    {
        portfolio_cache[user_id] = std::unordered_map<std::string, PortfolioEntry>();
    }

    // Check if we're trying to sell more than we own
    if (quantity_change < 0)
    {
        // Try to load existing holding
        PortfolioEntry existing_entry;
        bool has_existing = false;

        // First check cache
        auto port_it = portfolio_cache[user_id].find(symbol);
        if (port_it != portfolio_cache[user_id].end())
        {
            existing_entry = port_it->second;
            has_existing = true;
        }
        else
        {
            // Try to load from disk if not in cache
            has_existing = loadPortfolioEntry(user_id, symbol, existing_entry);
        }

        if (!has_existing || existing_entry.quantity < -quantity_change)
        {
            cerr << "ERROR: Cannot sell " << -quantity_change
                 << " " << symbol << " - only have "
                 << (has_existing ? existing_entry.quantity : 0)
                 << " shares" << endl;
            return false;
        }
    }

    // Handle cash for buying/selling
    double cash_change = -quantity_change * price; // Negative for buying, positive for selling

    if (quantity_change > 0) // Buying
    {
        // Check if user has enough cash
        double amount_needed = -cash_change; // cash_change is negative
        cout << "DEBUG: Buying - checking if user can afford $" << amount_needed
             << ", current balance: $" << user->data.cash_balance << endl;

        if (!user->canAfford(amount_needed))
        {
            cerr << "ERROR: Insufficient funds to buy " << quantity_change
                 << " " << symbol << " @ $" << price
                 << " (need $" << amount_needed << ", have $"
                 << user->data.cash_balance << ")" << endl;
            return false;
        }
    }

    // Update cash balance FIRST (before updating portfolio)
    cout << "DEBUG: Before updateBalance: balance = $" << user->data.cash_balance
         << ", cash_change = $" << cash_change << endl;

    if (!user->updateBalance(cash_change))
    {
        cerr << "ERROR: updateBalance failed for user " << user_id
             << ", amount: $" << cash_change << endl;
        return false;
    }

    cout << "DEBUG: After updateBalance: balance = $" << user->data.cash_balance << endl;

    // Update portfolio shares
    if (!user->updatePortfolioSharesOnly(symbol, quantity_change, price))
    {
        cerr << "ERROR: Failed to update portfolio shares for user " << user_id << endl;
        // Rollback cash change
        user->updateBalance(-cash_change);
        return false;
    }

    // Get updated entry from user's portfolio
    auto it = user->portfolio.find(symbol);
    if (it == user->portfolio.end())
    {
        // Entry was removed (quantity became 0)
        cout << "DEBUG: Removing portfolio entry (quantity became 0)" << endl;
        portfolio_cache[user_id].erase(symbol);

        // Remove from disk
        int64_t key = createPortfolioKey(user_id, symbol);
        portfolio_index.remove(key);
        cout << "DEBUG: Removed from disk, key: " << key << endl;
    }
    else
    {
        // Update cache
        cout << "DEBUG: Updating cache with entry: " << it->second.quantity
             << " shares" << endl;
        portfolio_cache[user_id][symbol] = it->second;

        // Store to disk
        cout << "DEBUG: Storing to disk..." << endl;
        int result = storePortfolioEntry(user_id, symbol, it->second);
        if (result == -1)
        {
            cerr << "Failed to update portfolio on disk: " << user_id
                 << " " << symbol << endl;
            return false;
        }
        cout << "DEBUG: Successfully stored to disk, page: " << result << endl;
    }

    // Update user data on disk (cash balance changed)
    cout << "DEBUG: Storing updated user data to disk (new balance: $"
         << user->data.cash_balance << ")..." << endl;

    if (storeUserData(user->data) == -1)
    {
        cerr << "Failed to update user data on disk: " << user_id << endl;
        return false;
    }

    cout << "DEBUG: User data stored successfully!" << endl;

    // Log to WAL
    if (wal)
    {
        char buffer[256];
        int len = snprintf(buffer, sizeof(buffer), "UPDATE_PORTFOLIO %d %s %d %.2f",
                           user_id, symbol.c_str(), quantity_change, price);
        wal->append(LOG_UPDATE, user_id, buffer, len + 1);
    }

    cout << "DEBUG: updatePortfolio completed successfully" << endl;
    cout << "Updated cash balance: $" << cash_change
         << " (New balance: $" << user->data.cash_balance << ")" << endl;

    cout << "Updated portfolio for user " << user_id
         << ": " << symbol << " "
         << (quantity_change >= 0 ? "+" : "") << quantity_change
         << " @ $" << std::fixed << std::setprecision(2) << price << endl;

    return true;
}
// Delete user
bool UserManager::deleteUser(int user_id)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // Remove from user index
    user_index.remove(static_cast<int64_t>(user_id));

    // Remove all portfolio entries
    int64_t start_key = static_cast<int64_t>(user_id) << 32;
    int64_t end_key = start_key | 0xFFFFFFFF;

    auto entries = portfolio_index.rangeQuery(start_key, end_key);
    for (const auto &entry : entries)
    {
        portfolio_index.remove(entry.first);
    }

    // Remove from caches
    user_cache.erase(user_id);
    portfolio_cache.erase(user_id);

    // Log to WAL
    if (wal)
    {
        char buffer[128];
        int len = snprintf(buffer, sizeof(buffer), "DELETE_USER %d", user_id);
        wal->append(LOG_DELETE, user_id, buffer, len + 1);
    }

    std::cout << "Deleted user: " << user_id << std::endl;

    return true;
}

// Get balance
double UserManager::getBalance(int user_id)
{
    std::cout << "DEBUG: getBalance called for user " << user_id << std::endl;
    std::lock_guard<std::recursive_mutex> lock(user_mutex);
    std::cout << "DEBUG: getBalance acquired lock for user " << user_id << std::endl;

    User *user = getUser(user_id);
    std::cout << "DEBUG: getBalance got user pointer: " << (user ? "valid" : "null") << std::endl;

    double balance = user ? user->data.cash_balance : 0.0;
    std::cout << "DEBUG: getBalance returning: $" << balance << std::endl;

    return balance;
}

// Get portfolio
std::vector<PortfolioEntry> UserManager::getPortfolio(int user_id)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);
    std::vector<PortfolioEntry> result;

    User *user = getUser(user_id);
    if (!user)
        return result;

    for (const auto &pair : user->portfolio)
    {
        result.push_back(pair.second);
    }

    return result;
}

// Get specific holding
PortfolioEntry UserManager::getHolding(int user_id, const std::string &symbol)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // First ensure user is loaded
    User *user = getUser(user_id);
    if (!user)
    {
        return PortfolioEntry();
    }

    // Check cache first
    auto user_port_it = portfolio_cache.find(user_id);
    if (user_port_it != portfolio_cache.end())
    {
        auto entry_it = user_port_it->second.find(symbol);
        if (entry_it != user_port_it->second.end())
        {
            return entry_it->second;
        }
    }

    // Try to load from disk if not in cache
    PortfolioEntry entry;
    if (loadPortfolioEntry(user_id, symbol, entry))
    {
        // Cache it
        portfolio_cache[user_id][symbol] = entry;
        user->portfolio[symbol] = entry;
        return entry;
    }

    return PortfolioEntry(); // Not found
}

// Execute trade
// Execute trade - IMPROVED VERSION
bool UserManager::executeTrade(int buyer_id, int seller_id,
                               const std::string &symbol, int quantity, double price)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    cout << "DEBUG: executeTrade START - Buyer: " << buyer_id
         << ", Seller: " << seller_id << ", Symbol: " << symbol
         << ", Quantity: " << quantity << ", Price: " << price << endl;

    if (quantity <= 0 || price <= 0)
    {
        std::cerr << "Invalid trade parameters" << std::endl;
        return false;
    }

    // CRITICAL FIX: Load both users FIRST
    User *buyer = getUser(buyer_id);
    User *seller = getUser(seller_id);

    if (!buyer || !seller)
    {
        std::cerr << "Trade failed: Invalid user IDs" << std::endl;
        return false;
    }

    // CRITICAL FIX: Ensure portfolios are loaded
    if (portfolio_cache.find(buyer_id) == portfolio_cache.end())
    {
        loadAllPortfolios(buyer_id);
    }
    if (portfolio_cache.find(seller_id) == portfolio_cache.end())
    {
        loadAllPortfolios(seller_id);
    }

    // Now get holdings from cache (not from separate getHolding call)
    PortfolioEntry seller_holding;
    auto seller_port_it = portfolio_cache.find(seller_id);
    if (seller_port_it != portfolio_cache.end())
    {
        auto holding_it = seller_port_it->second.find(symbol);
        if (holding_it != seller_port_it->second.end())
        {
            seller_holding = holding_it->second;
        }
    }

    cout << "DEBUG: Seller holding from cache: " << seller_holding.quantity << " shares" << endl;

    double trade_value = quantity * price;
    cout << "DEBUG: Trade value: $" << trade_value << endl;
    cout << "DEBUG: Buyer current balance: $" << buyer->data.cash_balance << endl;
    cout << "DEBUG: Seller current balance: $" << seller->data.cash_balance << endl;

    // Check buyer funds
    if (!buyer->canAfford(trade_value))
    {
        std::cerr << "Trade failed: Buyer " << buyer_id
                  << " insufficient funds (needs $" << trade_value
                  << ", has $" << buyer->data.cash_balance << ")" << std::endl;
        return false;
    }

    // Check seller shares (using cached value)
    if (seller_holding.quantity < quantity)
    {
        std::cerr << "Trade failed: Seller " << seller_id
                  << " insufficient shares (needs " << quantity
                  << ", has " << seller_holding.quantity << ")" << std::endl;
        return false;
    }

    // Execute trade
    // 1. Transfer cash
    cout << "DEBUG: Transferring cash: Buyer -$" << trade_value
         << ", Seller +$" << trade_value << endl;

    buyer->data.cash_balance -= trade_value;
    seller->data.cash_balance += trade_value;

    cout << "DEBUG: After cash transfer - Buyer balance: $" << buyer->data.cash_balance
         << ", Seller balance: $" << seller->data.cash_balance << endl;

    // Update user data on disk
    storeUserData(buyer->data);
    storeUserData(seller->data);

    // 2. Transfer shares
    cout << "DEBUG: Transferring shares: Buyer +" << quantity
         << ", Seller -" << quantity << endl;

    // Use the special method that doesn't check cash (cash already handled)
    updatePortfolioSharesOnly(buyer_id, symbol, quantity, price);
    updatePortfolioSharesOnly(seller_id, symbol, -quantity, price);

    // Log to WAL
    if (wal)
    {
        char buffer[256];
        int len = snprintf(buffer, sizeof(buffer), "EXECUTE_TRADE %d %d %s %d %.2f",
                           buyer_id, seller_id, symbol.c_str(), quantity, price);
        wal->append(LOG_INSERT, buyer_id, buffer, len + 1);
    }

    std::cout << "Trade executed: " << quantity << " " << symbol
              << " @ $" << std::fixed << std::setprecision(2) << price
              << " (Value: $" << trade_value << ")" << std::endl;
    std::cout << "  Buyer " << buyer_id << ": -$" << trade_value
              << ", +" << quantity << " " << symbol
              << " (Balance: $" << buyer->data.cash_balance << ")" << std::endl;
    std::cout << "  Seller " << seller_id << ": +$" << trade_value
              << ", -" << quantity << " " << symbol
              << " (Balance: $" << seller->data.cash_balance << ")" << std::endl;

    cout << "DEBUG: executeTrade COMPLETE - Success!" << endl;
    return true;
}
// Print user info
void UserManager::printUserInfo(int user_id)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    User *user = getUser(user_id);
    if (!user)
    {
        std::cout << "User not found: " << user_id << std::endl;
        return;
    }

    std::cout << "\n=== User Information ===" << std::endl;
    std::cout << "User ID: " << user->data.user_id << std::endl;
    std::cout << "Username: " << user->data.username << std::endl;
    std::cout << "Cash Balance: $" << std::fixed << std::setprecision(2)
              << user->data.cash_balance << std::endl;

    if (!user->portfolio.empty())
    {
        std::cout << "\nPortfolio:" << std::endl;
        std::cout << "Symbol\t\tQuantity\tAvg Price\t\tTotal Value" << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;

        for (const auto &pair : user->portfolio)
        {
            const PortfolioEntry &p = pair.second;
            double current_value = p.quantity * p.avg_price;
            std::cout << p.symbol << "\t\t" << p.quantity << "\t\t$"
                      << std::fixed << std::setprecision(2) << p.avg_price
                      << "\t\t$" << current_value << std::endl;
        }
    }
    else
    {
        std::cout << "\nPortfolio: Empty" << std::endl;
    }
    std::cout << "=======================\n"
              << std::endl;
}

// Print all users
void UserManager::printAllUsers()
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // Load all users (this is expensive, for admin only)
    std::cout << "\n=== All Users ===" << std::endl;

    // Get all user IDs from index (simplified - in production, iterate through tree)
    // For now, just show cached users
    std::cout << "Cached Users: " << user_cache.size() << std::endl;
    std::cout << "ID\tUsername\t\tBalance\t\tHoldings" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;

    for (const auto &pair : user_cache)
    {
        const User &user = pair.second;
        std::cout << user.data.user_id << "\t" << user.data.username << "\t\t$"
                  << std::fixed << std::setprecision(2) << user.data.cash_balance
                  << "\t\t" << user.portfolio.size() << std::endl;
    }
    std::cout << "=================================\n"
              << std::endl;
}

// Flush all changes
/*void UserManager::flush()
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // Save all cached users
    for (auto &pair : user_cache)
    {
        storeUserData(pair.second.data);

        // Save all portfolio entries
        for (auto &port_pair : pair.second.portfolio)
        {
            storePortfolioEntry(pair.first, port_pair.first, port_pair.second);
        }
    }

    std::cout << "UserManager: Flushed " << user_cache.size()
              << " users to disk" << std::endl;
}*/

// Get user count
int UserManager::getUserCount()
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // In production, get count from B+Tree
    // For now, return cache size
    return user_cache.size();
}

// Get total holdings
int UserManager::getTotalHoldings()
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    int total = 0;
    for (const auto &user_pair : portfolio_cache)
    {
        total += user_pair.second.size();
    }
    return total;
}

// Update portfolio shares only (no cash handling) - for trade execution
bool UserManager::updatePortfolioSharesOnly(int user_id, const std::string &symbol,
                                            int quantity_change, double price)
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    cout << "DEBUG: updatePortfolioSharesOnly called for user " << user_id
         << ", symbol " << symbol << ", qty " << quantity_change
         << ", price " << price << endl;

    if (quantity_change == 0)
        return true;

    // Get user
    User *user = nullptr;
    auto cache_it = user_cache.find(user_id);
    if (cache_it == user_cache.end())
    {
        // Load user data from disk
        UserData user_data;
        if (!loadUserData(user_id, user_data))
        {
            cerr << "User not found: " << user_id << endl;
            return false;
        }

        // Create new user with empty portfolio
        user = &user_cache[user_id];
        *user = User(user_data);
        portfolio_cache[user_id] = std::unordered_map<std::string, PortfolioEntry>();
    }
    else
    {
        user = &cache_it->second;
    }

    // Ensure portfolio cache exists for this user
    if (portfolio_cache.find(user_id) == portfolio_cache.end())
    {
        portfolio_cache[user_id] = std::unordered_map<std::string, PortfolioEntry>();
    }

    // Check if we're trying to sell more than we own
    if (quantity_change < 0)
    {
        // Try to load existing holding
        PortfolioEntry existing_entry;
        bool has_existing = false;

        // First check cache
        auto port_it = portfolio_cache[user_id].find(symbol);
        if (port_it != portfolio_cache[user_id].end())
        {
            existing_entry = port_it->second;
            has_existing = true;
        }
        else
        {
            // Try to load from disk if not in cache
            has_existing = loadPortfolioEntry(user_id, symbol, existing_entry);
        }

        if (!has_existing || existing_entry.quantity < -quantity_change)
        {
            cerr << "ERROR: Cannot sell " << -quantity_change
                 << " " << symbol << " - only have "
                 << (has_existing ? existing_entry.quantity : 0)
                 << " shares" << endl;
            return false;
        }
    }

    // Update portfolio shares only (no cash handling)
    if (!user->updatePortfolioSharesOnly(symbol, quantity_change, price))
    {
        cerr << "ERROR: Failed to update portfolio shares for user " << user_id << endl;
        return false;
    }

    // Get updated entry from user's portfolio
    auto it = user->portfolio.find(symbol);
    if (it == user->portfolio.end())
    {
        // Entry was removed (quantity became 0)
        cout << "DEBUG: Removing portfolio entry (quantity became 0)" << endl;
        portfolio_cache[user_id].erase(symbol);

        // Remove from disk
        int64_t key = createPortfolioKey(user_id, symbol);
        portfolio_index.remove(key);
        cout << "DEBUG: Removed from disk, key: " << key << endl;
    }
    else
    {
        // Update cache
        cout << "DEBUG: Updating cache with entry: " << it->second.quantity
             << " shares" << endl;
        portfolio_cache[user_id][symbol] = it->second;

        // Store to disk
        cout << "DEBUG: Storing to disk..." << endl;
        int result = storePortfolioEntry(user_id, symbol, it->second);
        if (result == -1)
        {
            cerr << "Failed to update portfolio on disk: " << user_id
                 << " " << symbol << endl;
            return false;
        }
        cout << "DEBUG: Successfully stored to disk, page: " << result << endl;
    }

    // NO cash update here - that's handled by executeTrade()

    // Log to WAL
    if (wal)
    {
        char buffer[256];
        int len = snprintf(buffer, sizeof(buffer), "UPDATE_PORTFOLIO_SHARES_ONLY %d %s %d %.2f",
                           user_id, symbol.c_str(), quantity_change, price);
        wal->append(LOG_UPDATE, user_id, buffer, len + 1);
    }

    cout << "DEBUG: updatePortfolioSharesOnly completed successfully" << endl;

    cout << "Updated portfolio shares for user " << user_id
         << ": " << symbol << " "
         << (quantity_change >= 0 ? "+" : "") << quantity_change
         << " @ $" << std::fixed << std::setprecision(2) << price << endl;

    return true;
}

// In UserManager.cpp - Implement persistence
// In UserManager.cpp - Update the persistence methods
bool UserManager::saveRootPages()
{
    if (!buffer_manager)
    {
        cerr << "BufferManager is null in saveRootPages" << endl;
        return false;
    }

    // Get current root page IDs from the trees
    int user_root = user_index.getRootPageId();
    int portfolio_root = portfolio_index.getRootPageId();

    cout << "\n=== DEBUG UserManager::saveRootPages ===" << endl;
    cout << "Saving ACTUAL tree root pages:" << endl;
    cout << "  User B+Tree actual root page: " << user_root << endl;
    cout << "  Portfolio B+Tree actual root page: " << portfolio_root << endl;

    // Check if these are valid
    if (user_root <= 0 || portfolio_root <= 0)
    {
        cerr << "WARNING: Invalid root pages!" << endl;
    }

    cout << "==============================\n"
         << endl;

    // Save to FileManager metadata
    FileManager *fm = buffer_manager->getFileManager();
    if (fm)
    {
        bool success = fm->saveAllRoots(user_root, portfolio_root, -1, -1);
        if (success)
        {
            cout << "✓ Root pages saved successfully to metadata" << endl;
        }
        else
        {
            cerr << "✗ ERROR: Failed to save root pages to metadata" << endl;
        }
        return success;
    }
    else
    {
        cerr << "ERROR: Could not get FileManager from BufferManager" << endl;
        return false;
    }
}

bool UserManager::loadRootPages()
{
    if (!buffer_manager)
    {
        cerr << "BufferManager is null in loadRootPages" << endl;
        return false;
    }

    // Try to load saved root pages from FileManager
    FileManager *fm = buffer_manager->getFileManager();
    if (fm)
    {
        int user_root = fm->getUserTreeRoot();
        int portfolio_root = fm->getPortfolioTreeRoot();

        cout << "DEBUG: Loading root pages - User: " << user_root
             << ", Portfolio: " << portfolio_root << endl;

        if (user_root != -1 && portfolio_root != -1)
        {
            // IMPORTANT: Check if the trees already exist
            // If they're already initialized, we need to update them
            // For now, we'll just print a warning and use the existing trees
            cout << "DEBUG: Found saved root pages, but trees are already initialized." << endl;
            cout << "DEBUG: Current roots - User: " << user_index.getRootPageId()
                 << ", Portfolio: " << portfolio_index.getRootPageId() << endl;

            // In a proper implementation, you would reinitialize the trees
            // But for now, we'll just trust that they were already loaded correctly
            return true;
        }
        else
        {
            cout << "DEBUG: No saved root pages found, using default initialization" << endl;
            return false;
        }
    }
    else
    {
        cerr << "ERROR: Could not get FileManager from BufferManager" << endl;
        return false;
    }
}

// In UserManager.cpp - Update destructor
UserManager::~UserManager()
{
    // Save roots before destruction
    cout << "DEBUG: UserManager destructor called, saving root pages..." << endl;
    saveRootPages();

    // Clear caches
    user_cache.clear();
    portfolio_cache.clear();

    cout << "DEBUG: UserManager cleanup complete" << endl;
}

void UserManager::flush()
{
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    // Save root pages
    saveRootPages();

    // Flush buffer manager
    if (buffer_manager)
    {
        buffer_manager->flushAll();
    }

    cout << "DEBUG: UserManager flush complete" << endl;
}

std::vector<int> UserManager::getAllUserIds()
{
    std::vector<int> userIds;
    std::lock_guard<std::recursive_mutex> lock(user_mutex);

    cout << "Scanning B+Tree for all user IDs..." << endl;

    try
    {
        // Method 1: Use the B+Tree's getAllKeyValuePairs() method
        auto allPairs = user_index.getAllKeyValuePairs();

        cout << "Found " << allPairs.size() << " user entries in B+Tree" << endl;

        // Extract user IDs (keys)
        for (const auto &pair : allPairs)
        {
            int user_id = static_cast<int>(pair.first);
            userIds.push_back(user_id);
        }

        // Method 2: Also check cache for any users that might be dirty but not flushed
        for (const auto &pair : user_cache)
        {
            int user_id = pair.first;
            // Check if already in list
            if (std::find(userIds.begin(), userIds.end(), user_id) == userIds.end())
            {
                userIds.push_back(user_id);
            }
        }

        // Sort user IDs for consistency
        std::sort(userIds.begin(), userIds.end());

        cout << "Total unique users: " << userIds.size() << endl;
    }
    catch (const std::exception &e)
    {
        cerr << "Error in getAllUserIds: " << e.what() << endl;

        // Fallback: check cache only
        for (const auto &pair : user_cache)
        {
            userIds.push_back(pair.first);
        }
    }

    return userIds;
}
