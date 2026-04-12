#include "BPlusTree.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include <climits>

using namespace std;

// Initialize static constants using Page's values
const int BPlusTree::MAX_LEAF_KEYS = Page::MAX_LEAF_KEYS;
const int BPlusTree::MAX_INTERNAL_KEYS = Page::MAX_INTERNAL_KEYS;
const int BPlusTree::MIN_LEAF_KEYS = ORDER - 1;     // ceil(m/2) - 1
const int BPlusTree::MIN_INTERNAL_KEYS = ORDER - 1; // ceil(m/2) - 1
const int BPlusTree::ORDER = Page::BTREE_ORDER;     // This should be around 167

// Node I/O Methods
Page *BPlusTree::loadNode(int page_id) const
{
    if (page_id == -1)
        return nullptr;

    // Use BufferManager to get the page
    Page *page = buffer_manager->getPage(page_id);

    if (page && page->getPageType() != PAGE_TYPE_BP_LEAF &&
        page->getPageType() != PAGE_TYPE_BP_INTERNAL)
    {
        // Wrong page type - this shouldn't happen
        buffer_manager->unpinPage(page_id);
        return nullptr;
    }

    return page;
}

Page *BPlusTree::allocateNode(bool is_leaf)
{
    // Use BufferManager to allocate a new page
    Page *page = buffer_manager->allocatePage();
    if (!page)
    {
        cout << "ERROR: BufferManager::allocatePage() returned nullptr!" << endl;
        return nullptr;
    }

    // DEBUG: Check the page ID right after allocation
    int page_id = page->getPageId();
    cout << "DEBUG: allocateNode() got page ID: " << page_id << endl;

    if (page_id == -1)
    {
        cout << "ERROR: Page has invalid ID -1!" << endl;
        return nullptr;
    }

    // Clear the page
    page->clear();

    // Set basic properties
    page->setPageId(page_id); // Ensure page ID is set

    if (is_leaf)
    {
        page->setPageType(PAGE_TYPE_BP_LEAF);
        page->getHeader().is_leaf = 1;
        page->getHeader().next_page = -1; // Will be set when chained
    }
    else
    {
        page->setPageType(PAGE_TYPE_BP_INTERNAL);
        page->getHeader().is_leaf = 0;
    }

    page->getHeader().parent_page = -1;
    page->setNumKeys(0);

    return page;
}

void BPlusTree::markDirty(int page_id) const
{
    buffer_manager->markDirty(page_id);
}

void BPlusTree::unpinPage(int page_id) const
{
    buffer_manager->unpinPage(page_id);
}

// FIXED B+Tree Constructor - CORRECTED VERSION
BPlusTree::BPlusTree(BufferManager *buffer_manager, int root_page_id)
    : buffer_manager(buffer_manager), root_page_id(root_page_id),
      insert_count(0), search_count(0)
{
    if (!buffer_manager)
    {
        throw runtime_error("BufferManager cannot be null");
    }

    cout << "B+Tree Initializing...\n";
    cout << "  Max leaf keys: " << MAX_LEAF_KEYS << "\n";
    cout << "  Max internal keys: " << MAX_INTERNAL_KEYS << "\n";
    cout << "  Min leaf keys: " << MIN_LEAF_KEYS << "\n";
    cout << "  Min internal keys: " << MIN_INTERNAL_KEYS << "\n";

    if (root_page_id == -1)
    {
        // Create new B+Tree
        Page *root = allocateNode(true);
        if (!root)
        {
            throw runtime_error("Failed to allocate root page");
        }

        // CRITICAL FIX: Get the actual page ID BEFORE we do anything else
        this->root_page_id = root->getPageId();

        // DEBUG: Print the page ID immediately
        cout << "DEBUG: Allocated root page ID: " << this->root_page_id << endl;

        // Set up the root page properties
        root->setPageType(PAGE_TYPE_BP_LEAF);
        root->getHeader().is_leaf = 1;
        root->setNumKeys(0);
        root->getHeader().parent_page = -1;
        root->getHeader().next_page = -1;

        // Mark as dirty
        markDirty(this->root_page_id);

        // DEBUG: Verify the page ID after setting properties
        cout << "DEBUG: Root page ID after setup: " << root->getPageId() << endl;

        // Don't unpin yet - keep it in cache
        // We'll let the caller manage pinning

        cout << "Created new B+Tree with root page " << this->root_page_id << "\n";
    }
    else
    {
        // Use existing B+Tree
        Page *root = loadNode(root_page_id);
        if (!root)
        {
            throw runtime_error("Failed to load root page " + to_string(root_page_id));
        }

        cout << "Loaded existing B+Tree with root page " << root_page_id << "\n";
        cout << "  Page type: " << root->getPageType() << "\n";
        cout << "  Is leaf: " << root->isLeaf() << "\n";
        cout << "  Num keys: " << root->getNumKeys() << "\n";

        // Keep the root pinned - caller will manage
        // Don't call unpinPage here
    }
}
// Utility Methods
int BPlusTree::findKeyIndex(Page *page, int64_t key) const
{
    int left = 0;
    int right = page->getNumKeys() - 1;

    while (left <= right)
    {
        int mid = left + (right - left) / 2;
        int64_t mid_key = page->getKey(mid);

        if (mid_key == key)
        {
            return mid;
        }
        else if (mid_key < key)
        {
            left = mid + 1;
        }
        else
        {
            right = mid - 1;
        }
    }

    return left; // Position where key should be inserted
}

int BPlusTree::getLeftSibling(int parent_id, int child_id) const
{
    if (parent_id == -1)
        return -1;

    Page *parent = loadNode(parent_id);
    if (!parent)
        return -1;

    // Find child index
    for (int i = 0; i <= parent->getNumKeys(); ++i)
    {
        if (parent->getChild(i) == child_id)
        {
            if (i > 0)
            {
                int left_sibling = parent->getChild(i - 1);
                unpinPage(parent_id);
                return left_sibling;
            }
            break;
        }
    }

    unpinPage(parent_id);
    return -1;
}

int BPlusTree::getRightSibling(int parent_id, int child_id) const
{
    if (parent_id == -1)
        return -1;

    Page *parent = loadNode(parent_id);
    if (!parent)
        return -1;

    // Find child index
    for (int i = 0; i <= parent->getNumKeys(); ++i)
    {
        if (parent->getChild(i) == child_id)
        {
            if (i < parent->getNumKeys())
            {
                int right_sibling = parent->getChild(i + 1);
                unpinPage(parent_id);
                return right_sibling;
            }
            break;
        }
    }

    unpinPage(parent_id);
    return -1;
}

int BPlusTree::findChildIndex(Page *parent, int child_id) const
{
    for (int i = 0; i <= parent->getNumKeys(); ++i)
    {
        if (parent->getChild(i) == child_id)
        {
            return i;
        }
    }
    return -1;
}

// Search Methods
BPlusTree::SearchResult BPlusTree::findLeaf(int64_t key) const
{
    search_count++;
    SearchResult result = {-1, -1, false};

    if (root_page_id == -1)
    {
        return result;
    }

    int current_page_id = root_page_id;
    int visited_pages = 0;
    const int MAX_DEPTH = 1000; // Safety limit

    while (visited_pages < MAX_DEPTH)
    {
        visited_pages++;

        Page *page = loadNode(current_page_id);
        if (!page)
        {
            cerr << "ERROR: Failed to load page " << current_page_id << endl;
            return result;
        }

        if (page->isLeaf())
        {
            // Binary search in leaf
            int left = 0;
            int right = page->getNumKeys() - 1;
            int pos = 0;

            while (left <= right)
            {
                int mid = left + (right - left) / 2;
                int64_t mid_key = page->getKey(mid);

                if (mid_key == key)
                {
                    result.page_id = current_page_id;
                    result.index = mid;
                    result.found = true;
                    unpinPage(current_page_id);
                    return result;
                }
                else if (mid_key < key)
                {
                    left = mid + 1;
                    pos = left;
                }
                else
                {
                    right = mid - 1;
                    pos = mid;
                }
            }

            result.page_id = current_page_id;
            result.index = pos; // Position where key should be inserted
            result.found = false;
            unpinPage(current_page_id);
            return result;
        }

        // Internal node - binary search for child
        int num_keys = page->getNumKeys();
        if (num_keys == 0)
        {
            cerr << "ERROR: Internal node " << current_page_id
                 << " has 0 keys" << endl;
            unpinPage(current_page_id);
            return result;
        }

        int left = 0;
        int right = num_keys - 1;
        int child_idx = num_keys; // Default to last child

        while (left <= right)
        {
            int mid = left + (right - left) / 2;
            int64_t mid_key = page->getKey(mid);

            if (key < mid_key)
            {
                child_idx = mid;
                right = mid - 1;
            }
            else
            {
                left = mid + 1;
            }
        }

        int next_page_id = page->getChild(child_idx);
        unpinPage(current_page_id);

        if (next_page_id == -1)
        {
            cerr << "ERROR: Child pointer at index " << child_idx
                 << " is -1 in page " << current_page_id << endl;
            return result;
        }

        current_page_id = next_page_id;
    }

    cerr << "ERROR: findLeaf exceeded max depth (" << MAX_DEPTH
         << "), possible cycle in tree" << endl;
    return result;
}

pair<bool, int64_t> BPlusTree::search(int64_t key) const
{
    search_count++;

    if (root_page_id == -1)
    {
        return {false, 0};
    }

    SearchResult result = findLeaf(key);

    if (result.found && result.page_id != -1)
    {
        Page *leaf = loadNode(result.page_id);
        if (leaf)
        {
            int64_t value = leaf->getValue(result.index);
            unpinPage(result.page_id);
            return {true, value};
        }
    }

    return {false, 0};
}

vector<pair<int64_t, int64_t>> BPlusTree::rangeQuery(int64_t start_key, int64_t end_key) const
{
    cout << "=== DEBUG: ENTERING rangeQuery(" << start_key << ", " << end_key << ") ===" << endl;
    cout << "Time: " << std::chrono::system_clock::now().time_since_epoch().count() << endl;

    vector<pair<int64_t, int64_t>> results;

    if (root_page_id == -1 || start_key > end_key)
    {

        cout
            << "DEBUG: rangeQuery returning empty - invalid range or empty tree" << endl;
        cout << "root_page_id: " << root_page_id << ", start_key: " << start_key
             << ", end_key: " << end_key << endl;
        return results;
    }
    auto query_start = std::chrono::steady_clock::now();
    const auto QUERY_TIMEOUT = std::chrono::seconds(2);
    // Find starting leaf
    SearchResult start_result = findLeaf(start_key);
    if (start_result.page_id == -1)
    {
        cout << "DEBUG: rangeQuery - No leaf found for start key " << start_key << endl;
        return results;
    }

    int current_page_id = start_result.page_id;
    int visited_pages = 0;
    const int MAX_PAGES = 100; // Safety limit to prevent infinite loops

    // Track visited pages to detect cycles
    unordered_set<int> visited;

    // Debug: Show starting point
    cout << "DEBUG: rangeQuery starting at leaf " << current_page_id
         << " with start index " << start_result.index
         << " (found: " << (start_result.found ? "yes" : "no") << ")" << endl;

    while (current_page_id != -1 && visited_pages < MAX_PAGES)
    {
        // Check for cycles - critical fix
        if (visited.find(current_page_id) != visited.end())
        {
            cerr << "ERROR: rangeQuery - Cycle detected at page " << current_page_id
                 << "! Breaking infinite loop." << endl;
            cerr << "Visited pages: ";
            for (int pid : visited)
                cerr << pid << " ";
            cerr << endl;
            break;
        }
        visited.insert(current_page_id);
        visited_pages++;

        Page *leaf = loadNode(current_page_id);
        if (!leaf)
        {
            cerr << "ERROR: rangeQuery - Failed to load leaf page " << current_page_id << endl;
            break;
        }

        // Validate page type
        if (!leaf->isLeaf())
        {
            cerr << "ERROR: rangeQuery - Page " << current_page_id
                 << " is not a leaf! Page type: " << leaf->getPageType()
                 << " (expected leaf)" << endl;
            unpinPage(current_page_id);
            break;
        }

        int num_keys = leaf->getNumKeys();

        // Debug: Show leaf info
        cout << "DEBUG: Visiting leaf " << current_page_id
             << " with " << num_keys << " keys"
             << ", next page: " << leaf->getHeader().next_page
             << ", parent: " << leaf->getHeader().parent_page << endl;

        // Determine where to start in this leaf
        int start_index = 0;
        if (current_page_id == start_result.page_id)
        {
            // This is the first leaf we found
            if (start_result.found)
            {
                start_index = start_result.index; // Start at exact match
            }
            else
            {
                // Binary search within this leaf for start position
                start_index = 0;
                while (start_index < num_keys && leaf->getKey(start_index) < start_key)
                {
                    start_index++;
                }
            }
        }

        bool found_any_in_this_leaf = false;
        bool beyond_range = false;

        // Scan keys in this leaf
        for (int i = start_index; i < num_keys; i++)
        {
            int64_t key = leaf->getKey(i);

            if (key < start_key)
            {
                continue; // Still looking for start
            }

            if (key > end_key)
            {
                // We've passed the end of our range
                beyond_range = true;
                break;
            }

            // Key is within range
            int64_t value = leaf->getValue(i);
            results.push_back({key, value});
            found_any_in_this_leaf = true;

            // Debug: Show each key added
            cout << "  DEBUG: Added key " << key << " = " << value << endl;

            // Safety: limit number of results
            if (results.size() > 1000)
            {
                cerr << "WARNING: rangeQuery returning too many results (>1000)" << endl;
                beyond_range = true;
                break;
            }
        }

        // Get next page before unpinning
        int next_page_id = leaf->getHeader().next_page;
        unpinPage(current_page_id);

        // Check if we should stop
        if (beyond_range || next_page_id == -1)
        {
            cout << "DEBUG: rangeQuery stopping - "
                 << (beyond_range ? "beyond range" : "no next page") << endl;
            break;
        }

        // Safety check for invalid next page
        if (next_page_id == current_page_id)
        {
            cerr << "ERROR: Leaf " << current_page_id
                 << " points to itself as next page!" << endl;
            break;
        }

        // Check if next page exists (defensive programming)
        Page *test_page = loadNode(next_page_id);
        if (!test_page)
        {
            cerr << "ERROR: Next page " << next_page_id << " cannot be loaded!" << endl;
            break;
        }

        if (!test_page->isLeaf())
        {
            cerr << "ERROR: Next page " << next_page_id << " is not a leaf!" << endl;
            unpinPage(next_page_id);
            break;
        }
        unpinPage(next_page_id);

        // Continue to next leaf
        current_page_id = next_page_id;

        // Reset start index for subsequent leaves
        start_result.index = 0;
        start_result.found = false;
    }

    // Safety checks
    if (visited_pages >= MAX_PAGES)
    {
        cerr << "ERROR: rangeQuery visited " << visited_pages
             << " pages (max: " << MAX_PAGES << "), possible infinite loop!" << endl;
        cerr << "Path taken: ";
        for (int pid : visited)
            cerr << pid << " ";
        cerr << endl;
    }

    cout << "DEBUG: rangeQuery(" << start_key << ", " << end_key
         << ") found " << results.size() << " results"
         << " (visited " << visited_pages << " pages)" << endl;
    auto query_end = std::chrono::steady_clock::now();
    auto query_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(query_end - query_start);

    cout << "=== DEBUG: EXITING rangeQuery ===" << endl;
    cout << "Found " << results.size() << " results" << endl;
    cout << "Took " << query_elapsed.count() << " ms" << endl;

    if (query_elapsed >= QUERY_TIMEOUT)
    {
        cout << "WARNING: rangeQuery took " << query_elapsed.count()
             << "ms (timeout: " << QUERY_TIMEOUT.count() << "s)" << endl;
    }
    return results;
}
// Insertion Methods
bool BPlusTree::insert(int64_t key, int64_t value)
{
    insert_count++;

    if (root_page_id == -1)
    {
        cerr << "Error: B+Tree not properly initialized\n";
        return false;
    }

    // Find leaf where key should be inserted
    SearchResult result = findLeaf(key);

    if (result.found)
    {
        // Key already exists - update value
        Page *leaf = loadNode(result.page_id);
        if (!leaf)
        {
            cerr << "Error: Failed to load leaf page " << result.page_id << "\n";
            return false;
        }

        leaf->setValue(result.index, value);
        markDirty(result.page_id);
        unpinPage(result.page_id);
        return true;
    }

    // Insert into leaf
    Page *leaf = loadNode(result.page_id);
    if (!leaf)
    {
        cerr << "Error: Failed to load leaf page " << result.page_id << "\n";
        return false;
    }

    int insert_pos = result.index;
    int num_keys = leaf->getNumKeys();

    // Check if leaf is full BEFORE inserting
    if (num_keys >= MAX_LEAF_KEYS)
    {
        // Leaf is full, we need to split first
        unpinPage(result.page_id);
        cout << "Leaf " << result.page_id << " is full (" << num_keys
             << " = " << MAX_LEAF_KEYS << "), splitting before insert...\n";
        splitLeaf(result.page_id);

        // After split, find leaf again
        result = findLeaf(key);
        leaf = loadNode(result.page_id);
        if (!leaf)
            return false;

        insert_pos = result.index;
        num_keys = leaf->getNumKeys();
    }

    // Shift keys and values to make room
    for (int i = num_keys; i > insert_pos; --i)
    {
        leaf->setKey(i, leaf->getKey(i - 1));
        leaf->setValue(i, leaf->getValue(i - 1));
    }

    // Insert new key-value pair
    leaf->setKey(insert_pos, key);
    leaf->setValue(insert_pos, value);
    leaf->setNumKeys(num_keys + 1);

    markDirty(result.page_id);

    // Check if leaf needs to split (shouldn't happen if we split before)
    if (leaf->getNumKeys() > MAX_LEAF_KEYS)
    {
        cout << "WARNING: Leaf " << result.page_id << " overflow ("
             << leaf->getNumKeys() << " > " << MAX_LEAF_KEYS
             << "), splitting...\n";
        splitLeaf(result.page_id);
    }
    else
    {
        unpinPage(result.page_id);
    }

    return true;
}

void BPlusTree::splitLeaf(int leaf_page_id)
{
    Page *leaf = loadNode(leaf_page_id);
    if (!leaf)
        return;

    // Create new leaf
    Page *new_leaf = allocateNode(true);
    if (!new_leaf)
    {
        unpinPage(leaf_page_id);
        return;
    }

    // Set up new leaf
    new_leaf->getHeader().parent_page = leaf->getHeader().parent_page;
    new_leaf->getHeader().next_page = leaf->getHeader().next_page;

    // Split point: move half the keys to new leaf
    // IMPORTANT: MAX_LEAF_KEYS = 251, so when we have 252 keys (overflow),
    // we split into two leaves with ~126 keys each
    int split_point = (leaf->getNumKeys() + 1) / 2; // FIX: Use +1 to handle overflow properly
    int new_leaf_keys = leaf->getNumKeys() - split_point;

    // DEBUG: Print split info
    cout << "Splitting leaf " << leaf_page_id << " with " << leaf->getNumKeys()
         << " keys at split point " << split_point
         << ", new leaf will have " << new_leaf_keys << " keys\n";

    // Copy second half to new leaf
    for (int i = 0; i < new_leaf_keys; ++i)
    {
        int src_idx = split_point + i;
        if (src_idx >= leaf->getNumKeys())
        {
            cerr << "ERROR: Trying to copy key from invalid index " << src_idx
                 << " (leaf has " << leaf->getNumKeys() << " keys)\n";
            break;
        }

        new_leaf->setKey(i, leaf->getKey(src_idx));
        new_leaf->setValue(i, leaf->getValue(src_idx));
    }

    // Update counts
    new_leaf->setNumKeys(new_leaf_keys);
    leaf->setNumKeys(split_point); // Original leaf keeps first half

    // Update leaf chain
    leaf->getHeader().next_page = new_leaf->getPageId();

    // Get middle key (first key in new leaf)
    int64_t middle_key = new_leaf->getKey(0);

    // Mark both leaves as dirty
    markDirty(leaf_page_id);
    markDirty(new_leaf->getPageId());

    // Insert middle key into parent
    if (leaf->getHeader().parent_page == -1)
    {
        // Create new root
        createNewRoot(leaf_page_id, new_leaf->getPageId(), middle_key);
    }
    else
    {
        // Insert into existing parent
        insertIntoParent(leaf->getHeader().parent_page, middle_key, new_leaf->getPageId());
    }

    // Unpin pages
    unpinPage(leaf_page_id);
    unpinPage(new_leaf->getPageId());

    cout << "Split leaf " << leaf_page_id << " -> new leaf "
         << new_leaf->getPageId() << " with middle key "
         << middle_key << "\n";
}

void BPlusTree::createNewRoot(int left_child, int right_child, int64_t key)
{
    Page *new_root = allocateNode(false);
    if (!new_root)
        return;

    // Set up new root
    new_root->getHeader().parent_page = -1;
    new_root->setNumKeys(1);
    new_root->setKey(0, key);
    new_root->setChild(0, left_child);
    new_root->setChild(1, right_child);

    // Update children's parent pointers
    Page *left_page = loadNode(left_child);
    if (left_page)
    {
        left_page->getHeader().parent_page = new_root->getPageId();
        markDirty(left_child);
        unpinPage(left_child);
    }

    Page *right_page = loadNode(right_child);
    if (right_page)
    {
        right_page->getHeader().parent_page = new_root->getPageId();
        markDirty(right_child);
        unpinPage(right_child);
    }

    // Update root
    root_page_id = new_root->getPageId();
    markDirty(root_page_id);
    unpinPage(root_page_id);

    cout << "Created new root " << root_page_id << "\n";
}

void BPlusTree::insertIntoParent(int parent_id, int64_t key, int right_child)
{
    Page *parent = loadNode(parent_id);
    if (!parent)
        return;

    // Find insertion position
    int insert_pos = findKeyIndex(parent, key);

    // Shift keys and children to make room
    int num_keys = parent->getNumKeys();
    for (int i = num_keys; i > insert_pos; --i)
    {
        parent->setKey(i, parent->getKey(i - 1));
        parent->setChild(i + 1, parent->getChild(i));
    }

    // Insert new key and child
    parent->setKey(insert_pos, key);
    parent->setChild(insert_pos + 1, right_child);
    parent->setNumKeys(num_keys + 1);

    // Update child's parent pointer
    Page *child_page = loadNode(right_child);
    if (child_page)
    {
        child_page->getHeader().parent_page = parent_id;
        markDirty(right_child);
        unpinPage(right_child);
    }

    markDirty(parent_id);

    // Check if internal node needs to split
    if (parent->getNumKeys() > MAX_INTERNAL_KEYS)
    {
        cout << "Internal node " << parent_id << " overflow ("
             << parent->getNumKeys() << " > " << MAX_INTERNAL_KEYS
             << "), splitting...\n";
        splitInternal(parent_id);
    }
    else
    {
        unpinPage(parent_id);
    }
}

void BPlusTree::splitInternal(int internal_page_id)
{
    Page *internal = loadNode(internal_page_id);
    if (!internal)
        return;

    // Create new internal node
    Page *new_internal = allocateNode(false);
    if (!new_internal)
    {
        unpinPage(internal_page_id);
        return;
    }

    // Set up new internal node
    new_internal->getHeader().parent_page = internal->getHeader().parent_page;

    // Split point
    int split_point = internal->getNumKeys() / 2;
    int64_t middle_key = internal->getKey(split_point);

    // Number of keys in new internal node
    int new_keys_count = internal->getNumKeys() - split_point - 1;

    // Copy keys and children to new internal node
    for (int i = 0; i < new_keys_count; ++i)
    {
        new_internal->setKey(i, internal->getKey(split_point + 1 + i));
        new_internal->setChild(i, internal->getChild(split_point + 1 + i));
    }
    // Last child
    new_internal->setChild(new_keys_count, internal->getChild(internal->getNumKeys()));

    // Update counts
    new_internal->setNumKeys(new_keys_count);
    internal->setNumKeys(split_point);

    // Update children's parent pointers
    for (int i = 0; i <= new_keys_count; ++i)
    {
        int child_id = new_internal->getChild(i);
        Page *child = loadNode(child_id);
        if (child)
        {
            child->getHeader().parent_page = new_internal->getPageId();
            markDirty(child_id);
            unpinPage(child_id);
        }
    }

    // Mark nodes as dirty
    markDirty(internal_page_id);
    markDirty(new_internal->getPageId());

    // Insert middle key into parent
    if (internal->getHeader().parent_page == -1)
    {
        createNewRoot(internal_page_id, new_internal->getPageId(), middle_key);
    }
    else
    {
        insertIntoParent(internal->getHeader().parent_page, middle_key, new_internal->getPageId());
    }

    // Unpin
    unpinPage(internal_page_id);
    unpinPage(new_internal->getPageId());

    cout << "Split internal node " << internal_page_id
         << " -> new node " << new_internal->getPageId()
         << " with middle key " << middle_key << "\n";
}

// Deletion Methods
bool BPlusTree::remove(int64_t key)
{
    if (root_page_id == -1)
    {
        cout << "B+Tree is empty, nothing to remove\n";
        return false;
    }

    cout << "Removing key: " << key << "\n";

    // Find leaf containing the key
    SearchResult result = findLeaf(key);
    if (!result.found)
    {
        cout << "Key " << key << " not found in B+Tree\n";
        return false;
    }

    // Remove from leaf
    bool success = removeFromLeaf(result.page_id, key);
    if (!success)
    {
        return false;
    }

    // Check for underflow
    Page *leaf = loadNode(result.page_id);
    if (!leaf)
    {
        return false;
    }

    bool is_leaf_underflow = (leaf->getNumKeys() < MIN_LEAF_KEYS);
    unpinPage(result.page_id);

    if (is_leaf_underflow && result.page_id != root_page_id)
    {
        fixUnderflow(result.page_id);
    }

    // Special case: empty root
    adjustRoot();

    return true;
}

bool BPlusTree::removeFromLeaf(int leaf_page_id, int64_t key)
{
    Page *leaf = loadNode(leaf_page_id);
    if (!leaf)
        return false;

    // Find key index
    int key_index = -1;
    for (int i = 0; i < leaf->getNumKeys(); ++i)
    {
        if (leaf->getKey(i) == key)
        {
            key_index = i;
            break;
        }
    }

    if (key_index == -1)
    {
        unpinPage(leaf_page_id);
        return false;
    }

    cout << "Removing key at index " << key_index << " from leaf page " << leaf_page_id << "\n";

    // Shift keys left
    for (int i = key_index; i < leaf->getNumKeys() - 1; ++i)
    {
        leaf->setKey(i, leaf->getKey(i + 1));
        leaf->setValue(i, leaf->getValue(i + 1));
    }

    leaf->setNumKeys(leaf->getNumKeys() - 1);
    markDirty(leaf_page_id);
    unpinPage(leaf_page_id);

    return true;
}

void BPlusTree::fixUnderflow(int page_id)
{
    Page *page = loadNode(page_id);
    if (!page || page_id == root_page_id)
    {
        if (page)
            unpinPage(page_id);
        return;
    }

    int parent_id = page->getHeader().parent_page;
    if (parent_id == -1)
    {
        unpinPage(page_id);
        return;
    }

    Page *parent = loadNode(parent_id);
    if (!parent)
    {
        unpinPage(page_id);
        return;
    }

    // Find child index in parent
    int child_index = -1;
    for (int i = 0; i <= parent->getNumKeys(); ++i)
    {
        if (parent->getChild(i) == page_id)
        {
            child_index = i;
            break;
        }
    }

    if (child_index == -1)
    {
        unpinPage(parent_id);
        unpinPage(page_id);
        return;
    }

    // Try to borrow from left sibling
    if (child_index > 0)
    {
        int left_sibling_id = parent->getChild(child_index - 1);
        Page *left_sibling = loadNode(left_sibling_id);

        if (left_sibling)
        {
            int min_keys = page->isLeaf() ? MIN_LEAF_KEYS : MIN_INTERNAL_KEYS;
            if (left_sibling->getNumKeys() > min_keys)
            {
                cout << "Borrowing from left sibling (page " << left_sibling_id << ")\n";
                borrowFromLeftSibling(page_id, parent_id, child_index);
                unpinPage(left_sibling_id);
                unpinPage(parent_id);
                unpinPage(page_id);
                return;
            }
            unpinPage(left_sibling_id);
        }
    }

    // Try to borrow from right sibling
    if (child_index < parent->getNumKeys())
    {
        int right_sibling_id = parent->getChild(child_index + 1);
        Page *right_sibling = loadNode(right_sibling_id);

        if (right_sibling)
        {
            int min_keys = page->isLeaf() ? MIN_LEAF_KEYS : MIN_INTERNAL_KEYS;
            if (right_sibling->getNumKeys() > min_keys)
            {
                cout << "Borrowing from right sibling (page " << right_sibling_id << ")\n";
                borrowFromRightSibling(page_id, parent_id, child_index);
                unpinPage(right_sibling_id);
                unpinPage(parent_id);
                unpinPage(page_id);
                return;
            }
            unpinPage(right_sibling_id);
        }
    }

    // Must merge with sibling
    cout << "Merging node " << page_id << " with sibling\n";

    if (child_index > 0)
    {
        // Merge with left sibling
        int left_sibling_id = parent->getChild(child_index - 1);
        mergeWithSibling(page_id, left_sibling_id, parent_id, true);
    }
    else
    {
        // Merge with right sibling
        int right_sibling_id = parent->getChild(child_index + 1);
        mergeWithSibling(page_id, right_sibling_id, parent_id, false);
    }

    unpinPage(parent_id);
    unpinPage(page_id);

    // Parent might now be underflowing
    Page *parent_reload = loadNode(parent_id);
    if (parent_reload && parent_reload->getNumKeys() < MIN_INTERNAL_KEYS && parent_id != root_page_id)
    {
        cout << "Parent page " << parent_id << " underflow, fixing recursively\n";
        unpinPage(parent_id);
        fixUnderflow(parent_id);
    }
    else if (parent_reload)
    {
        unpinPage(parent_id);
    }
}

void BPlusTree::borrowFromLeftSibling(int page_id, int parent_id, int child_index)
{
    Page *node = loadNode(page_id);
    Page *parent = loadNode(parent_id);
    int left_sibling_id = parent->getChild(child_index - 1);
    Page *left_sibling = loadNode(left_sibling_id);

    if (!node || !parent || !left_sibling)
    {
        if (node)
            unpinPage(page_id);
        if (parent)
            unpinPage(parent_id);
        if (left_sibling)
            unpinPage(left_sibling_id);
        return;
    }

    if (node->isLeaf())
    {
        // Borrow from leaf
        int last_idx = left_sibling->getNumKeys() - 1;
        int64_t borrowed_key = left_sibling->getKey(last_idx);
        int64_t borrowed_value = left_sibling->getValue(last_idx);

        // Make room in node
        for (int i = node->getNumKeys() - 1; i >= 0; --i)
        {
            node->setKey(i + 1, node->getKey(i));
            node->setValue(i + 1, node->getValue(i));
        }

        // Insert borrowed key
        node->setKey(0, borrowed_key);
        node->setValue(0, borrowed_value);
        node->setNumKeys(node->getNumKeys() + 1);

        // Remove from left sibling
        left_sibling->setNumKeys(left_sibling->getNumKeys() - 1);

        // Update parent separator
        parent->setKey(child_index - 1, borrowed_key);
    }
    else
    {
        // Borrow from internal node
        int last_idx = left_sibling->getNumKeys() - 1;
        int64_t borrowed_key = left_sibling->getKey(last_idx);
        int borrowed_child = left_sibling->getChild(last_idx + 1);

        // Make room in node
        for (int i = node->getNumKeys() - 1; i >= 0; --i)
        {
            node->setKey(i + 1, node->getKey(i));
        }
        for (int i = node->getNumKeys(); i >= 0; --i)
        {
            node->setChild(i + 1, node->getChild(i));
        }

        // Insert separator from parent
        node->setKey(0, parent->getKey(child_index - 1));
        node->setNumKeys(node->getNumKeys() + 1);

        // Move child from left sibling
        node->setChild(0, borrowed_child);

        // Update parent separator
        parent->setKey(child_index - 1, borrowed_key);

        // Remove from left sibling
        left_sibling->setNumKeys(left_sibling->getNumKeys() - 1);

        // Update child's parent pointer
        Page *child_page = loadNode(borrowed_child);
        if (child_page)
        {
            child_page->getHeader().parent_page = page_id;
            markDirty(borrowed_child);
            unpinPage(borrowed_child);
        }
    }

    markDirty(page_id);
    markDirty(left_sibling_id);
    markDirty(parent_id);

    unpinPage(left_sibling_id);
    unpinPage(parent_id);
    unpinPage(page_id);
}

void BPlusTree::borrowFromRightSibling(int page_id, int parent_id, int child_index)
{
    Page *node = loadNode(page_id);
    Page *parent = loadNode(parent_id);
    int right_sibling_id = parent->getChild(child_index + 1);
    Page *right_sibling = loadNode(right_sibling_id);

    if (!node || !parent || !right_sibling)
    {
        if (node)
            unpinPage(page_id);
        if (parent)
            unpinPage(parent_id);
        if (right_sibling)
            unpinPage(right_sibling_id);
        return;
    }

    if (node->isLeaf())
    {
        // Borrow from leaf
        int64_t borrowed_key = right_sibling->getKey(0);
        int64_t borrowed_value = right_sibling->getValue(0);

        // Add to node
        node->setKey(node->getNumKeys(), borrowed_key);
        node->setValue(node->getNumKeys(), borrowed_value);
        node->setNumKeys(node->getNumKeys() + 1);

        // Remove from right sibling
        for (int i = 0; i < right_sibling->getNumKeys() - 1; ++i)
        {
            right_sibling->setKey(i, right_sibling->getKey(i + 1));
            right_sibling->setValue(i, right_sibling->getValue(i + 1));
        }
        right_sibling->setNumKeys(right_sibling->getNumKeys() - 1);

        // Update parent separator
        parent->setKey(child_index, right_sibling->getKey(0));
    }
    else
    {
        // Borrow from internal node
        int64_t borrowed_key = right_sibling->getKey(0);
        int borrowed_child = right_sibling->getChild(0);

        // Add separator from parent to node
        node->setKey(node->getNumKeys(), parent->getKey(child_index));
        node->setNumKeys(node->getNumKeys() + 1);

        // Move child from right sibling
        node->setChild(node->getNumKeys(), borrowed_child);

        // Update parent separator
        parent->setKey(child_index, borrowed_key);

        // Remove from right sibling
        for (int i = 0; i < right_sibling->getNumKeys() - 1; ++i)
        {
            right_sibling->setKey(i, right_sibling->getKey(i + 1));
            right_sibling->setChild(i, right_sibling->getChild(i + 1));
        }
        right_sibling->setChild(right_sibling->getNumKeys() - 1,
                                right_sibling->getChild(right_sibling->getNumKeys()));
        right_sibling->setNumKeys(right_sibling->getNumKeys() - 1);

        // Update child's parent pointer
        Page *child_page = loadNode(borrowed_child);
        if (child_page)
        {
            child_page->getHeader().parent_page = page_id;
            markDirty(borrowed_child);
            unpinPage(borrowed_child);
        }
    }

    markDirty(page_id);
    markDirty(right_sibling_id);
    markDirty(parent_id);

    unpinPage(right_sibling_id);
    unpinPage(parent_id);
    unpinPage(page_id);
}

void BPlusTree::mergeWithSibling(int page_id, int sibling_id, int parent_id, bool is_left_sibling)
{
    Page *node = loadNode(page_id);
    Page *sibling = loadNode(sibling_id);
    Page *parent = loadNode(parent_id);

    if (!node || !sibling || !parent)
    {
        if (node)
            unpinPage(page_id);
        if (sibling)
            unpinPage(sibling_id);
        if (parent)
            unpinPage(parent_id);
        return;
    }

    int child_index = -1;
    for (int i = 0; i <= parent->getNumKeys(); ++i)
    {
        if (parent->getChild(i) == page_id)
        {
            child_index = i;
            break;
        }
    }

    if (child_index == -1)
    {
        unpinPage(page_id);
        unpinPage(sibling_id);
        unpinPage(parent_id);
        return;
    }

    if (node->isLeaf())
    {
        // Merge leaves
        if (is_left_sibling)
        {
            // Merge node into left sibling
            int dest_idx = sibling->getNumKeys();
            for (int i = 0; i < node->getNumKeys(); ++i)
            {
                sibling->setKey(dest_idx + i, node->getKey(i));
                sibling->setValue(dest_idx + i, node->getValue(i));
            }
            sibling->setNumKeys(sibling->getNumKeys() + node->getNumKeys());

            // Update leaf chain
            sibling->getHeader().next_page = node->getHeader().next_page;
        }
        else
        {
            // Merge right sibling into node
            int dest_idx = node->getNumKeys();
            for (int i = 0; i < sibling->getNumKeys(); ++i)
            {
                node->setKey(dest_idx + i, sibling->getKey(i));
                node->setValue(dest_idx + i, sibling->getValue(i));
            }
            node->setNumKeys(node->getNumKeys() + sibling->getNumKeys());

            // Update leaf chain
            node->getHeader().next_page = sibling->getHeader().next_page;
        }

        // Remove key from parent
        int key_index = is_left_sibling ? child_index - 1 : child_index;
        for (int i = key_index; i < parent->getNumKeys() - 1; ++i)
        {
            parent->setKey(i, parent->getKey(i + 1));
        }
        for (int i = key_index + 1; i < parent->getNumKeys(); ++i)
        {
            parent->setChild(i, parent->getChild(i + 1));
        }
        parent->setNumKeys(parent->getNumKeys() - 1);
    }
    else
    {
        // Merge internal nodes
        int parent_key_index = is_left_sibling ? child_index - 1 : child_index;
        int64_t parent_key = parent->getKey(parent_key_index);

        if (is_left_sibling)
        {
            // Add parent key to sibling
            sibling->setKey(sibling->getNumKeys(), parent_key);
            sibling->setNumKeys(sibling->getNumKeys() + 1);

            // Copy node's keys and children
            int dest_idx = sibling->getNumKeys();
            for (int i = 0; i < node->getNumKeys(); ++i)
            {
                sibling->setKey(dest_idx + i, node->getKey(i));
                sibling->setChild(dest_idx + i, node->getChild(i));

                // Update child's parent pointer
                Page *child = loadNode(node->getChild(i));
                if (child)
                {
                    child->getHeader().parent_page = sibling_id;
                    markDirty(node->getChild(i));
                    unpinPage(node->getChild(i));
                }
            }
            sibling->setChild(dest_idx + node->getNumKeys(), node->getChild(node->getNumKeys()));

            // Update last child's parent pointer
            Page *last_child = loadNode(node->getChild(node->getNumKeys()));
            if (last_child)
            {
                last_child->getHeader().parent_page = sibling_id;
                markDirty(node->getChild(node->getNumKeys()));
                unpinPage(node->getChild(node->getNumKeys()));
            }

            sibling->setNumKeys(sibling->getNumKeys() + node->getNumKeys());
        }
        else
        {
            // Add parent key to node
            node->setKey(node->getNumKeys(), parent_key);
            node->setNumKeys(node->getNumKeys() + 1);

            // Copy sibling's keys and children
            int dest_idx = node->getNumKeys();
            for (int i = 0; i < sibling->getNumKeys(); ++i)
            {
                node->setKey(dest_idx + i, sibling->getKey(i));
                node->setChild(dest_idx + i, sibling->getChild(i));

                // Update child's parent pointer
                Page *child = loadNode(sibling->getChild(i));
                if (child)
                {
                    child->getHeader().parent_page = page_id;
                    markDirty(sibling->getChild(i));
                    unpinPage(sibling->getChild(i));
                }
            }
            node->setChild(dest_idx + sibling->getNumKeys(), sibling->getChild(sibling->getNumKeys()));

            // Update last child's parent pointer
            Page *last_child = loadNode(sibling->getChild(sibling->getNumKeys()));
            if (last_child)
            {
                last_child->getHeader().parent_page = page_id;
                markDirty(sibling->getChild(sibling->getNumKeys()));
                unpinPage(sibling->getChild(sibling->getNumKeys()));
            }

            node->setNumKeys(node->getNumKeys() + sibling->getNumKeys());
        }

        // Remove key and child from parent
        for (int i = parent_key_index; i < parent->getNumKeys() - 1; ++i)
        {
            parent->setKey(i, parent->getKey(i + 1));
        }
        for (int i = parent_key_index + 1; i < parent->getNumKeys(); ++i)
        {
            parent->setChild(i, parent->getChild(i + 1));
        }
        parent->setNumKeys(parent->getNumKeys() - 1);
    }

    // Mark as dirty
    markDirty(page_id);
    markDirty(sibling_id);
    markDirty(parent_id);

    // Unpin
    unpinPage(sibling_id);
    unpinPage(parent_id);
    unpinPage(page_id);
}

void BPlusTree::adjustRoot()
{
    if (root_page_id == -1)
        return;

    Page *root = loadNode(root_page_id);
    if (!root)
        return;

    if (root->getNumKeys() == 0)
    {
        if (root->isLeaf())
        {
            // Empty tree - root remains as empty leaf
        }
        else
        {
            // Root has only one child, make it the new root
            int child_id = root->getChild(0);
            Page *child = loadNode(child_id);
            if (child)
            {
                child->getHeader().parent_page = -1;
                markDirty(child_id);

                // Update root pointer
                root_page_id = child_id;
                cout << "Tree height decreased. New root: " << root_page_id << "\n";

                unpinPage(child_id);
            }
        }
    }

    unpinPage(root_page_id);
}

// Tree Information Methods
int BPlusTree::getHeight() const
{
    if (root_page_id == -1)
        return 0;

    int height = 0;
    int current_page_id = root_page_id;

    while (true)
    {
        height++;
        Page *page = loadNode(current_page_id);
        if (!page)
            break;

        if (page->isLeaf())
        {
            unpinPage(current_page_id);
            break;
        }

        // Go to first child
        int next_id = page->getChild(0);
        unpinPage(current_page_id);
        current_page_id = next_id;
    }

    return height;
}

int BPlusTree::countKeys(int page_id) const
{
    Page *page = loadNode(page_id);
    if (!page)
        return 0;

    int count = page->getNumKeys();

    if (!page->isLeaf())
    {
        // Count keys in children
        for (int i = 0; i <= page->getNumKeys(); ++i)
        {
            count += countKeys(page->getChild(i));
        }
    }

    unpinPage(page_id);
    return count;
}

int BPlusTree::getTotalKeys() const
{
    if (root_page_id == -1)
        return 0;
    return countKeys(root_page_id);
}

// Debug Methods
void BPlusTree::printTree() const
{
    if (root_page_id == -1)
    {
        cout << "B+Tree is empty\n";
        return;
    }

    cout << "\n=== B+Tree Structure ===\n";
    cout << "Root: " << root_page_id << "\n";
    cout << "Height: " << getHeight() << "\n";
    cout << "Total keys: " << getTotalKeys() << "\n";

    queue<pair<int, int>> q;
    q.push({root_page_id, 0});

    while (!q.empty())
    {
        auto [page_id, level] = q.front();
        q.pop();

        Page *page = loadNode(page_id);
        if (!page)
            continue;

        string indent(level * 2, ' ');
        cout << indent << "[" << page_id << "] ";

        if (page->isLeaf())
        {
            cout << "LEAF keys(" << page->getNumKeys() << "): ";
            for (int i = 0; i < page->getNumKeys(); ++i)
            {
                cout << page->getKey(i) << ":" << page->getValue(i) << " ";
            }
            if (page->getHeader().next_page != -1)
                cout << "-> next:" << page->getHeader().next_page;
            cout << "\n";
        }
        else
        {
            cout << "INTERNAL keys(" << page->getNumKeys() << "): ";
            for (int i = 0; i < page->getNumKeys(); ++i)
            {
                cout << page->getKey(i) << " ";
            }
            cout << "\n";

            // Add children to queue
            for (int i = 0; i <= page->getNumKeys(); ++i)
            {
                int child_id = page->getChild(i);
                if (child_id != -1)
                {
                    q.push({child_id, level + 1});
                }
            }
        }

        unpinPage(page_id);
    }

    cout << "========================\n";
}

void BPlusTree::printLeaves() const
{
    if (root_page_id == -1)
    {
        cout << "B+Tree is empty\n";
        return;
    }

    cout << "\n=== Leaf Chain ===\n";

    // Find leftmost leaf
    int current_page_id = root_page_id;
    Page *current_page = loadNode(current_page_id);

    while (current_page && !current_page->isLeaf())
    {
        int next_id = current_page->getChild(0);
        unpinPage(current_page_id);
        current_page_id = next_id;
        current_page = loadNode(current_page_id);
    }

    if (!current_page)
    {
        cout << "Error: Could not find leftmost leaf\n";
        return;
    }

    // Traverse leaf chain
    int leaf_count = 0;
    while (current_page_id != -1 && current_page)
    {
        cout << "Leaf " << current_page_id << " (parent: " << current_page->getHeader().parent_page
             << "): ";
        for (int i = 0; i < current_page->getNumKeys(); ++i)
        {
            cout << current_page->getKey(i) << " ";
        }
        cout << "\n";

        int next_page = current_page->getHeader().next_page;
        unpinPage(current_page_id);
        current_page_id = next_page;

        if (current_page_id != -1)
        {
            current_page = loadNode(current_page_id);
        }

        leaf_count++;
        if (leaf_count > 100)
        {
            cout << "Warning: Too many leaves, stopping...\n";
            break;
        }
    }

    cout << "=================\n";
}

void BPlusTree::validate() const
{
    if (root_page_id == -1)
    {
        cout << "B+Tree is empty (valid)\n";
        return;
    }

    cout << "Validating B+Tree...\n";
    cout << "Root page: " << root_page_id << "\n";
    cout << "Height: " << getHeight() << "\n";
    cout << "Total keys: " << getTotalKeys() << "\n";

    // Basic validation - check that all pages are accessible
    int total = countKeys(root_page_id);
    cout << "Validation: " << total << " keys counted\n";

    try
    {
        validateNode(root_page_id, true, INT64_MIN, INT64_MAX);
        cout << "B+Tree validation passed\n";
    }
    catch (const exception &e)
    {
        cerr << "B+Tree validation failed: " << e.what() << "\n";
    }
}

void BPlusTree::validateNode(int page_id, bool is_root, int64_t min_key, int64_t max_key) const
{
    Page *page = loadNode(page_id);
    if (!page)
    {
        throw runtime_error("Failed to load page " + to_string(page_id));
    }

    // Check parent pointers
    if (!is_root && page->getHeader().parent_page == -1)
    {
        unpinPage(page_id);
        throw runtime_error("Non-root node " + to_string(page_id) + " has no parent");
    }

    // Check key count
    if (page->getNumKeys() < 0 ||
        (page->isLeaf() && page->getNumKeys() > MAX_LEAF_KEYS) ||
        (!page->isLeaf() && page->getNumKeys() > MAX_INTERNAL_KEYS))
    {
        unpinPage(page_id);
        throw runtime_error("Invalid key count in node " + to_string(page_id));
    }

    // Check key order (for leaves and internal nodes)
    if (page->getNumKeys() > 0)
    {
        // First key must be >= min_key
        int64_t first_key = page->getKey(0);
        if (first_key < min_key)
        {
            unpinPage(page_id);
            throw runtime_error("First key " + to_string(first_key) + " in node " +
                                to_string(page_id) + " is less than min_key " + to_string(min_key));
        }

        // Last key must be <= max_key
        int64_t last_key = page->getKey(page->getNumKeys() - 1);
        if (last_key > max_key)
        {
            unpinPage(page_id);
            throw runtime_error("Last key " + to_string(last_key) + " in node " +
                                to_string(page_id) + " is greater than max_key " + to_string(max_key));
        }

        // Check keys are in sorted order
        for (int i = 1; i < page->getNumKeys(); ++i)
        {
            if (page->getKey(i) <= page->getKey(i - 1))
            {
                unpinPage(page_id);
                throw runtime_error("Keys not in sorted order in node " + to_string(page_id));
            }
        }
    }

    // Recursively validate children for internal nodes
    if (!page->isLeaf())
    {
        for (int i = 0; i <= page->getNumKeys(); ++i)
        {
            int child_id = page->getChild(i);
            if (child_id == -1 && i < page->getNumKeys())
            {
                unpinPage(page_id);
                throw runtime_error("Missing child pointer in internal node " + to_string(page_id));
            }

            if (child_id != -1)
            {
                int64_t child_min = (i == 0) ? min_key : page->getKey(i - 1);
                int64_t child_max = (i == page->getNumKeys()) ? max_key : page->getKey(i);
                validateNode(child_id, false, child_min, child_max);
            }
        }
    }

    // For leaves, check if next pointer is valid
    if (page->isLeaf() && page->getHeader().next_page != -1)
    {
        Page *next_leaf = loadNode(page->getHeader().next_page);
        if (!next_leaf)
        {
            unpinPage(page_id);
            throw runtime_error("Leaf " + to_string(page_id) +
                                " points to invalid next page " + to_string(page->getHeader().next_page));
        }

        if (!next_leaf->isLeaf())
        {
            unpinPage(page_id);
            unpinPage(page->getHeader().next_page);
            throw runtime_error("Leaf " + to_string(page_id) +
                                " points to non-leaf page as next");
        }
        unpinPage(page->getHeader().next_page);
    }

    unpinPage(page_id);
}

bool BPlusTree::isEmpty() const
{
    if (root_page_id == -1)
    {
        return true;
    }

    Page *root = loadNode(root_page_id);
    if (!root)
    {
        return true;
    }

    bool empty = (root->getNumKeys() == 0);
    unpinPage(root_page_id);
    return empty;
}

// In BPlusTree.cpp, add this NEW method:
vector<pair<int64_t, int64_t>> BPlusTree::rangeQuery(int64_t start_key, int64_t end_key, int limit) const
{
    vector<pair<int64_t, int64_t>> results;

    if (root_page_id == -1 || start_key > end_key || limit <= 0)
    {

        cout << "DEBUG: rangeQuery returning empty - invalid range or empty tree" << endl;
        return results;
    }
    cout << "DEBUG: rangeQuery(" << start_key << ", " << end_key << ") called" << endl;
    cout << "DEBUG: root_page_id = " << root_page_id << endl;
    // CRITICAL: Add immediate timeout
    auto start_time = std::chrono::steady_clock::now();
    const auto TIMEOUT = std::chrono::seconds(1); // 1 second timeout
    // Find starting leaf
    SearchResult start_result = findLeaf(start_key);
    if (start_result.page_id == -1)
    {
        return results;
    }

    int current_page_id = start_result.page_id;
    int visited_pages = 0;
    const int MAX_PAGES = 100;

    // Track visited pages to detect cycles
    unordered_set<int> visited;

    while (current_page_id != -1 && visited_pages < MAX_PAGES && results.size() < limit)
    {
        // Check for cycles
        if (visited.find(current_page_id) != visited.end())
        {
            cerr << "ERROR: Cycle detected in leaf chain at page " << current_page_id << endl;
            break;
        }
        visited.insert(current_page_id);
        visited_pages++;

        Page *leaf = loadNode(current_page_id);
        if (!leaf)
        {
            cerr << "ERROR: Failed to load leaf page " << current_page_id << endl;
            break;
        }

        if (!leaf->isLeaf())
        {
            cerr << "ERROR: Expected leaf page " << current_page_id
                 << " but got internal node" << endl;
            unpinPage(current_page_id);
            break;
        }

        int num_keys = leaf->getNumKeys();
        bool found_in_range = false;

        // Collect keys in range
        for (int i = 0; i < num_keys && results.size() < limit; ++i)
        {
            int64_t key = leaf->getKey(i);

            if (key < start_key)
            {
                continue; // Key too small
            }

            if (key > end_key)
            {
                found_in_range = false; // All subsequent keys will be > end_key
                break;
            }

            int64_t value = leaf->getValue(i);
            results.push_back({key, value});
            found_in_range = true;
        }

        // Check if we should continue to next leaf
        int next_page = leaf->getHeader().next_page;
        unpinPage(current_page_id);

        // Stop if:
        // 1. No next page
        // 2. We found keys beyond our range
        // 3. Next page points to itself (corruption)
        // 4. We reached our limit
        if (next_page == -1 || !found_in_range || next_page == current_page_id || results.size() >= limit)
        {
            break;
        }

        current_page_id = next_page;
    }

    if (visited_pages >= MAX_PAGES)
    {
        cerr << "WARNING: rangeQuery visited too many pages ("
             << visited_pages << "), possible infinite loop" << endl;
    }
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    if (elapsed >= TIMEOUT)
    {
        cout << "ERROR: rangeQuery timed out after " << elapsed.count() << "ms!" << endl;
        cout << "Returning empty results to avoid hang" << endl;
        return vector<pair<int64_t, int64_t>>(); // Return empty
    }

    return results;
}

void BPlusTree::debugLeafChain() const
{
    if (root_page_id == -1)
    {
        cout << "DEBUG: Tree is empty" << endl;
        return;
    }

    cout << "\n=== DEBUG: Leaf Chain Check ===" << endl;

    // Find leftmost leaf
    int current_id = root_page_id;
    Page *current = loadNode(current_id);

    while (current && !current->isLeaf())
    {
        int next_id = current->getChild(0);
        unpinPage(current_id);
        current_id = next_id;
        current = loadNode(current_id);
    }

    if (!current)
    {
        cout << "DEBUG: Cannot find leftmost leaf" << endl;
        return;
    }

    cout << "DEBUG: Leftmost leaf: " << current_id << endl;

    // Traverse leaf chain
    int count = 0;
    unordered_set<int> visited;

    while (current_id != -1 && count < 50) // Limit to 50 leaves
    {
        if (visited.find(current_id) != visited.end())
        {
            cout << "ERROR: CYCLE DETECTED at page " << current_id << endl;
            break;
        }
        visited.insert(current_id);
        count++;

        Page *leaf = loadNode(current_id);
        if (!leaf)
        {
            cout << "ERROR: Cannot load leaf " << current_id << endl;
            break;
        }

        cout << "Leaf " << current_id
             << ": keys=" << leaf->getNumKeys()
             << ", next=" << leaf->getHeader().next_page
             << ", parent=" << leaf->getHeader().parent_page << endl;

        int next_id = leaf->getHeader().next_page;
        unpinPage(current_id);

        if (next_id == current_id)
        {
            cout << "ERROR: Leaf points to itself!" << endl;
            break;
        }

        current_id = next_id;
    }

    if (count >= 50)
    {
        cout << "WARNING: Visited 50+ leaves, possible infinite chain" << endl;
    }

    cout << "Visited " << count << " leaves" << endl;
    cout << "==============================\n"
         << endl;
}

// In BPlusTree.cpp, add these implementations:
// In BPlusTree.cpp - Add these implementations at the end:

// ===================== CORRECT IMPLEMENTATIONS FOR YOUR B+Tree =====================

std::vector<int64_t> BPlusTree::getAllKeys()
{
    std::vector<int64_t> keys;

    if (root_page_id == -1)
    {
        return keys;
    }

    try
    {
        // Load root page
        Page *root = loadNode(root_page_id);
        if (!root)
        {
            cerr << "ERROR: Failed to load root page " << root_page_id << endl;
            return keys;
        }

        // Check if root is leaf
        bool is_leaf = root->isLeaf();
        unpinPage(root_page_id);

        // Collect all keys
        collectKeysFromNode(root_page_id, keys, is_leaf);
    }
    catch (const std::exception &e)
    {
        cerr << "Error in getAllKeys: " << e.what() << endl;
    }

    return keys;
}

void BPlusTree::collectKeysFromNode(int pageId, std::vector<int64_t> &keys, bool isLeaf)
{
    try
    {
        Page *node = loadNode(pageId);
        if (!node)
        {
            cerr << "ERROR: Failed to load page " << pageId << endl;
            return;
        }

        if (isLeaf)
        {
            // Add all keys from leaf node
            int num_keys = node->getNumKeys();
            for (int i = 0; i < num_keys; i++)
            {
                keys.push_back(node->getKey(i));
            }

            // Follow next pointer if exists
            int next_page = node->getHeader().next_page;
            unpinPage(pageId);

            if (next_page != -1)
            {
                collectKeysFromNode(next_page, keys, true);
            }
        }
        else
        {
            // Internal node - recursively traverse all children
            int num_keys = node->getNumKeys();

            // For each child
            for (int i = 0; i <= num_keys; i++)
            {
                int child_page = node->getChild(i);
                if (child_page != -1)
                {
                    // Load child to check if it's leaf
                    Page *child = loadNode(child_page);
                    bool child_is_leaf = child->isLeaf();
                    unpinPage(child_page);

                    collectKeysFromNode(child_page, keys, child_is_leaf);
                }
            }

            unpinPage(pageId);
        }
    }
    catch (const std::exception &e)
    {
        cerr << "Error in collectKeysFromNode: " << e.what() << endl;
    }
}

std::vector<std::pair<int64_t, int64_t>> BPlusTree::getAllKeyValuePairs()
{
    std::vector<std::pair<int64_t, int64_t>> pairs;

    if (root_page_id == -1)
    {
        return pairs;
    }

    try
    {
        // Load root page
        Page *root = loadNode(root_page_id);
        if (!root)
        {
            cerr << "ERROR: Failed to load root page " << root_page_id << endl;
            return pairs;
        }

        // Check if root is leaf
        bool is_leaf = root->isLeaf();
        unpinPage(root_page_id);

        // Collect all key-value pairs
        collectKeyValuePairsFromNode(root_page_id, pairs, is_leaf);
    }
    catch (const std::exception &e)
    {
        cerr << "Error in getAllKeyValuePairs: " << e.what() << endl;
    }

    return pairs;
}

void BPlusTree::collectKeyValuePairsFromNode(int pageId, std::vector<std::pair<int64_t, int64_t>> &pairs, bool isLeaf)
{
    try
    {
        Page *node = loadNode(pageId);
        if (!node)
        {
            cerr << "ERROR: Failed to load page " << pageId << endl;
            return;
        }

        if (isLeaf)
        {
            // Add all key-value pairs from leaf node
            int num_keys = node->getNumKeys();
            for (int i = 0; i < num_keys; i++)
            {
                pairs.push_back({node->getKey(i), node->getValue(i)});
            }

            // Follow next pointer if exists
            int next_page = node->getHeader().next_page;
            unpinPage(pageId);

            if (next_page != -1)
            {
                collectKeyValuePairsFromNode(next_page, pairs, true);
            }
        }
        else
        {
            // Internal node - recursively traverse all children
            int num_keys = node->getNumKeys();

            // For each child
            for (int i = 0; i <= num_keys; i++)
            {
                int child_page = node->getChild(i);
                if (child_page != -1)
                {
                    // Load child to check if it's leaf
                    Page *child = loadNode(child_page);
                    bool child_is_leaf = child->isLeaf();
                    unpinPage(child_page);

                    collectKeyValuePairsFromNode(child_page, pairs, child_is_leaf);
                }
            }

            unpinPage(pageId);
        }
    }
    catch (const std::exception &e)
    {
        cerr << "Error in collectKeyValuePairsFromNode: " << e.what() << endl;
    }
}