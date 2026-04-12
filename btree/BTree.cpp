// trading_platform/btree/BTree.cpp
#include "BTree.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <queue>
#include <cassert>

using namespace std;

// Initialize static constants from Page
const int BTree::T = Page::BTREE_ORDER;
// We need different limits for leaves and internal nodes
const int BTree::MAX_KEYS = Page::MAX_LEAF_KEYS; // Conservative: use leaf limit

BTree::BTree(BufferManager *buffer_manager, int initial_root_page_id)
    : buffer_manager(buffer_manager)
{
    if (!buffer_manager)
    {
        throw runtime_error("BufferManager cannot be null");
    }

    cout << "B-Tree Initializing...\n";
    cout << "  Minimum degree (t): " << T << "\n";
    cout << "  Max leaf keys: " << Page::MAX_LEAF_KEYS << "\n";
    cout << "  Max internal keys: " << Page::MAX_INTERNAL_KEYS << "\n";
    cout << "  Min leaf keys: " << MIN_LEAF_KEYS << "\n";
    cout << "  Min internal keys: " << MIN_INTERNAL_KEYS << "\n";

    if (initial_root_page_id == -1)
    {
        // Create new B-Tree
        PageHandle root = PageHandle::allocate(buffer_manager);
        if (!root.valid())
        {
            throw runtime_error("Failed to allocate root page");
        }

        root->clear();
        root->setPageId(root.getPageId());
        root->setPageType(PAGE_TYPE_BT_LEAF);
        root->getHeader().is_leaf = 1;
        root->setNumKeys(0);
        root->getHeader().parent_page = -1;
        root->getHeader().next_page = -1;

        root_page_id = root.getPageId();
        buffer_manager->markDirty(root_page_id);

        cout << "Created new B-Tree with root page " << root_page_id << "\n";
    }
    else
    {
        // Use existing B-Tree
        root_page_id = initial_root_page_id;
        PageHandle root(buffer_manager, root_page_id);
        if (!root.valid())
        {
            throw runtime_error("Failed to load root page " + to_string(root_page_id));
        }

        cout << "Loaded existing B-Tree with root page " << root_page_id << "\n";
        cout << "  Page type: " << root->getPageType() << "\n";
        cout << "  Is leaf: " << root->isLeaf() << "\n";
        cout << "  Num keys: " << root->getNumKeys() << "\n";
    }
}

bool BTree::insert(int64_t key, int64_t value)
{
    if (root_page_id == -1)
    {
        cerr << "Error: B-Tree not properly initialized\n";
        return false;
    }

    PageHandle root(buffer_manager, root_page_id);
    if (!root.valid())
    {
        cerr << "Error: Failed to load root page\n";
        return false;
    }

    // Check if root needs to split
    int max_keys_for_node = root->isLeaf() ? Page::MAX_LEAF_KEYS : Page::MAX_INTERNAL_KEYS;

    if (root->getNumKeys() >= max_keys_for_node)
    {
        cout << "Root is full (keys=" << root->getNumKeys()
             << ", max=" << max_keys_for_node << "), splitting...\n";

        // Create new root (always internal when splitting)
        PageHandle new_root = PageHandle::allocate(buffer_manager);
        if (!new_root.valid())
        {
            cerr << "Error: Failed to allocate new root page\n";
            return false;
        }

        new_root->clear();
        new_root->setPageId(new_root.getPageId());
        new_root->setPageType(PAGE_TYPE_BT_INTERNAL);
        new_root->getHeader().is_leaf = 0;
        new_root->setNumKeys(0);
        new_root->getHeader().parent_page = -1;
        new_root->getHeader().next_page = -1;
        new_root->setChild(0, root_page_id);

        // Update old root's parent
        root->getHeader().parent_page = new_root.getPageId();

        // Save old root ID
        int old_root_id = root_page_id;

        // Set new root BEFORE calling splitChild
        root_page_id = new_root.getPageId();

        // Mark old root as dirty (parent changed)
        buffer_manager->markDirty(old_root_id);

        // Split the old root (now child 0 of new root)
        splitChild(root_page_id, 0);

        cout << "Root split complete. New root: " << root_page_id
             << " (internal), Old root: " << old_root_id << "\n";

        // Now insert into the tree with new root
        // We need to reload root reference
        PageHandle new_root_ref(buffer_manager, root_page_id);
        if (!new_root_ref.valid())
        {
            cerr << "Error: Failed to reload new root\n";
            return false;
        }
    }

    // Insert into non-full tree
    insertNonFull(root_page_id, key, value);
    return true;
}

void BTree::splitChild(int parent_id, int child_index)
{
    PageHandle parent(buffer_manager, parent_id);
    if (!parent.valid())
        return;

    int child_id = parent->getChild(child_index);
    PageHandle child(buffer_manager, child_id);
    if (!child.valid())
        return;

    // Determine split point based on node type
    int split_point;
    if (child->isLeaf())
    {
        // For leaves, split in the middle
        split_point = Page::MAX_LEAF_KEYS / 2;
    }
    else
    {
        // For internal nodes, split at t-1 (where t = T)
        split_point = T - 1;
    }

    // Create new sibling node
    PageHandle new_child = PageHandle::allocate(buffer_manager);
    if (!new_child.valid())
        return;

    new_child->clear();
    new_child->setPageId(new_child.getPageId());
    new_child->setPageType(child->getPageType());
    new_child->getHeader().is_leaf = child->getHeader().is_leaf;
    new_child->getHeader().parent_page = parent_id;
    new_child->getHeader().next_page = child->getNextPage();

    int64_t middle_key;

    if (child->isLeaf())
    {
        // For leaf nodes
        middle_key = child->getKey(split_point);

        // Copy second half of keys and values to new child
        int keys_to_copy = child->getNumKeys() - split_point;
        new_child->setNumKeys(keys_to_copy);

        for (int i = 0; i < keys_to_copy; ++i)
        {
            new_child->setKey(i, child->getKey(split_point + i));
            new_child->setValue(i, child->getValue(split_point + i));
        }

        // Update child's key count (keeping the first half)
        child->setNumKeys(split_point);

        // Update leaf chain pointers
        int old_next = child->getNextPage();
        new_child->setNextPage(old_next);
        child->setNextPage(new_child.getPageId());
    }
    else
    {
        // For internal nodes
        middle_key = child->getKey(split_point); // This key moves up to parent

        // Copy second half of keys and ALL children to new child
        int keys_to_copy = child->getNumKeys() - split_point - 1;
        new_child->setNumKeys(keys_to_copy);

        // Copy keys (skip the middle key that moves to parent)
        for (int i = 0; i < keys_to_copy; ++i)
        {
            new_child->setKey(i, child->getKey(split_point + 1 + i));
        }

        // Copy children
        for (int i = 0; i <= keys_to_copy; ++i)
        {
            int grandchild_id = child->getChild(split_point + 1 + i);
            new_child->setChild(i, grandchild_id);

            // Update grandchild's parent pointer
            PageHandle grandchild(buffer_manager, grandchild_id);
            if (grandchild.valid())
            {
                grandchild->getHeader().parent_page = new_child.getPageId();
                buffer_manager->markDirty(grandchild_id);
            }
        }

        // Update child's key count
        child->setNumKeys(split_point);
    }

    // Make room in parent for new child and middle key
    for (int i = parent->getNumKeys(); i > child_index; --i)
    {
        parent->setKey(i, parent->getKey(i - 1));
        parent->setChild(i + 1, parent->getChild(i));
    }

    // Insert middle key into parent
    parent->setKey(child_index, middle_key);

    // Insert new child into parent
    parent->setChild(child_index + 1, new_child.getPageId());
    parent->setNumKeys(parent->getNumKeys() + 1);

    // Mark all modified pages as dirty
    buffer_manager->markDirty(parent_id);
    buffer_manager->markDirty(child_id);
    buffer_manager->markDirty(new_child.getPageId());

    cout << "Split: Child " << child_id << " -> New child " << new_child.getPageId()
         << ", Middle key: " << middle_key << ", Split point: " << split_point << "\n";
}

void BTree::insertNonFull(int page_id, int64_t key, int64_t value)
{
    PageHandle node(buffer_manager, page_id);
    if (!node.valid())
        return;

    if (node->isLeaf())
    {
        // Check if leaf is full (should have been split already)
        if (node->getNumKeys() >= Page::MAX_LEAF_KEYS)
        {
            cerr << "ERROR: Leaf is full but not split! Page " << page_id
                 << " has " << node->getNumKeys() << " keys\n";
            return;
        }

        // Find insertion position
        int i = node->getNumKeys() - 1;
        while (i >= 0 && key < node->getKey(i))
        {
            i--;
        }

        // Insert at i+1
        for (int j = node->getNumKeys() - 1; j > i; j--)
        {
            node->setKey(j + 1, node->getKey(j));
            node->setValue(j + 1, node->getValue(j));
        }

        node->setKey(i + 1, key);
        node->setValue(i + 1, value);
        node->setNumKeys(node->getNumKeys() + 1);

        buffer_manager->markDirty(page_id);

        // Debug
        if (node->getNumKeys() >= Page::MAX_LEAF_KEYS - 5)
        {
            cout << "Leaf page " << page_id << " now has " << node->getNumKeys()
                 << " keys (max: " << Page::MAX_LEAF_KEYS << ")\n";
        }
    }
    else
    {
        // Internal node: find child to descend into
        int i = node->getNumKeys() - 1;
        while (i >= 0 && key < node->getKey(i))
        {
            i--;
        }
        i++;

        int child_id = node->getChild(i);
        PageHandle child(buffer_manager, child_id);
        if (!child.valid())
            return;

        // Check if child needs to split
        int max_keys_for_child = child->isLeaf() ? Page::MAX_LEAF_KEYS : Page::MAX_INTERNAL_KEYS;

        if (child->getNumKeys() >= max_keys_for_child)
        {
            cout << "Child " << child_id << " is full (keys=" << child->getNumKeys()
                 << ", max=" << max_keys_for_child << "), splitting...\n";

            splitChild(page_id, i);

            // Reload node because split might have changed keys
            PageHandle node_reload(buffer_manager, page_id);
            if (!node_reload.valid())
                return;

            // Decide which child to go into
            if (key > node_reload->getKey(i))
            {
                i++;
            }

            child_id = node_reload->getChild(i);
        }

        insertNonFull(child_id, key, value);
    }
}

pair<bool, int64_t> BTree::search(int64_t key)
{
    if (root_page_id == -1)
    {
        return {false, 0};
    }

    SearchResult result = searchInternal(root_page_id, key);

    if (result.found)
    {
        PageHandle page(buffer_manager, result.page_id);
        if (page.valid() && page->isLeaf())
        {
            int64_t value = page->getValue(result.index);
            return {true, value};
        }
    }

    return {false, 0};
}

BTree::SearchResult BTree::searchInternal(int page_id, int64_t key)
{
    PageHandle page(buffer_manager, page_id);
    if (!page.valid())
    {
        return {-1, -1, false};
    }

    int i = 0;
    while (i < page->getNumKeys() && key > page->getKey(i))
    {
        i++;
    }

    if (i < page->getNumKeys() && page->getKey(i) == key)
    {
        // Key found in this node
        return {page_id, i, true};
    }

    if (page->isLeaf())
    {
        // Key not found
        return {-1, -1, false};
    }

    // Recurse to child
    int child_id = page->getChild(i);
    return searchInternal(child_id, key);
}

int BTree::findKeyIndex(Page *page, int64_t key)
{
    int i = 0;
    while (i < page->getNumKeys() && key > page->getKey(i))
    {
        i++;
    }
    return i;
}

int BTree::getHeight()
{
    if (root_page_id == -1)
        return 0;

    int height = 0;
    int current_page_id = root_page_id;

    while (true)
    {
        height++;
        PageHandle page(buffer_manager, current_page_id);
        if (!page.valid())
            break;

        if (page->isLeaf())
        {
            break;
        }

        // Go to first child
        current_page_id = page->getChild(0);
    }

    return height;
}

int BTree::countKeys(int page_id)
{
    PageHandle page(buffer_manager, page_id);
    if (!page.valid())
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

    return count;
}

int BTree::getTotalKeys()
{
    if (root_page_id == -1)
        return 0;
    return countKeys(root_page_id);
}

void BTree::printTree()
{
    if (root_page_id == -1)
    {
        cout << "B-Tree is empty\n";
        return;
    }

    cout << "\n=== B-Tree Structure ===\n";
    cout << "Root: " << root_page_id << "\n";
    cout << "Height: " << getHeight() << "\n";
    cout << "Total keys: " << getTotalKeys() << "\n";

    // Print tree starting from root
    queue<pair<int, int>> q;
    q.push({root_page_id, 0});

    while (!q.empty())
    {
        auto [page_id, level] = q.front();
        q.pop();

        PageHandle page(buffer_manager, page_id);
        if (!page.valid())
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
            if (page->getNextPage() != -1)
                cout << "-> next:" << page->getNextPage();
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
    }

    cout << "========================\n";
}

void BTree::validate()
{
    if (root_page_id == -1)
    {
        cout << "B-Tree is empty (valid)\n";
        return;
    }

    cout << "Validating B-Tree...\n";
    cout << "Root page: " << root_page_id << "\n";
    cout << "Height: " << getHeight() << "\n";
    cout << "Total keys: " << getTotalKeys() << "\n";

    // Basic validation - check that all pages are accessible
    int total = countKeys(root_page_id);
    cout << "Validation: " << total << " keys counted\n";

    cout << "Validation complete.\n";
}

BTree::~BTree()
{
    // BufferManager handles flushing in its destructor
}

// ======================
// REMOVE IMPLEMENTATION
// ======================

bool BTree::remove(int64_t key)
{
    if (root_page_id == -1)
    {
        cout << "B-Tree is empty, nothing to remove\n";
        return false;
    }

    cout << "Removing key: " << key << "\n";

    // Search for the key
    SearchResult result = searchInternal(root_page_id, key);
    if (!result.found)
    {
        cout << "Key " << key << " not found in B-Tree\n";
        return false;
    }

    // Remove the key
    PageHandle page(buffer_manager, result.page_id);
    if (!page.valid())
    {
        cerr << "Error: Failed to load page " << result.page_id << "\n";
        return false;
    }

    bool success;
    if (page->isLeaf())
    {
        success = removeFromLeaf(result.page_id, result.index);
    }
    else
    {
        success = removeFromInternal(result.page_id, result.index);
    }

    if (!success)
    {
        cerr << "Error: Failed to remove key " << key << " from page " << result.page_id << "\n";
        return false;
    }

    // If root becomes empty after deletion
    if (root_page_id != -1)
    {
        PageHandle root(buffer_manager, root_page_id);
        if (root.valid() && root->getNumKeys() == 0 && !root->isLeaf())
        {
            cout << "Root is empty, shrinking tree height\n";
            int new_root_id = root->getChild(0);

            // Update new root's parent
            PageHandle new_root(buffer_manager, new_root_id);
            if (new_root.valid())
            {
                new_root->getHeader().parent_page = -1;
                buffer_manager->markDirty(new_root_id);
            }

            // Free old root page (in production, add to free list)
            root_page_id = new_root_id;
            cout << "New root page: " << root_page_id << "\n";
        }
    }

    return true;
}

bool BTree::removeFromLeaf(int page_id, int key_index)
{
    PageHandle leaf(buffer_manager, page_id);
    if (!leaf.valid())
        return false;

    cout << "Removing key at index " << key_index << " from leaf page " << page_id << "\n";

    // Shift keys and values left
    for (int i = key_index + 1; i < leaf->getNumKeys(); ++i)
    {
        leaf->setKey(i - 1, leaf->getKey(i));
        leaf->setValue(i - 1, leaf->getValue(i));
    }

    leaf->setNumKeys(leaf->getNumKeys() - 1);
    buffer_manager->markDirty(page_id);

    // Check if leaf is now underflowing (but root can have fewer keys)
    if (leaf->getNumKeys() < MIN_LEAF_KEYS && page_id != root_page_id)
    {
        cout << "Leaf page " << page_id << " underflow (" << leaf->getNumKeys()
             << " < " << MIN_LEAF_KEYS << "), fixing...\n";
        fixAfterDeletion(page_id);
    }

    return true;
}

bool BTree::removeFromInternal(int page_id, int key_index)
{
    PageHandle node(buffer_manager, page_id);
    if (!node.valid())
        return false;

    cout << "Removing key at index " << key_index << " from internal page " << page_id << "\n";

    int64_t key_to_remove = node->getKey(key_index);

    // Get left and right children
    int left_child_id = node->getChild(key_index);
    int right_child_id = node->getChild(key_index + 1);

    PageHandle left_child(buffer_manager, left_child_id);
    PageHandle right_child(buffer_manager, right_child_id);

    if (!left_child.valid() || !right_child.valid())
        return false;

    // Case 1: Left child has at least T keys
    if (left_child->getNumKeys() >= T)
    {
        cout << "Case 1: Using predecessor from left child\n";
        int64_t predecessor = getPredecessor(page_id, key_index);
        node->setKey(key_index, predecessor);
        buffer_manager->markDirty(page_id);
        removeFromLeaf(left_child_id, left_child->getNumKeys() - 1);
        return true;
    }

    // Case 2: Right child has at least T keys
    if (right_child->getNumKeys() >= T)
    {
        cout << "Case 2: Using successor from right child\n";
        int64_t successor = getSuccessor(page_id, key_index);
        node->setKey(key_index, successor);
        buffer_manager->markDirty(page_id);
        removeFromLeaf(right_child_id, 0);
        return true;
    }

    // Case 3: Both children have T-1 keys, merge them
    cout << "Case 3: Merging children\n";

    // Merge right child into left child
    mergeWithSibling(page_id, key_index, true); // merge with left

    // Now remove the key from the merged node
    return removeFromLeaf(left_child_id, T - 1);
}

int64_t BTree::getPredecessor(int page_id, int key_index)
{
    // Go to left child, then always go right
    PageHandle node(buffer_manager, page_id);
    if (!node.valid())
        return -1;

    int child_id = node->getChild(key_index);
    PageHandle child(buffer_manager, child_id);
    if (!child.valid())
        return -1;

    // Keep going to the rightmost child until we reach a leaf
    while (!child->isLeaf())
    {
        child_id = child->getChild(child->getNumKeys());
        child = PageHandle(buffer_manager, child_id);
        if (!child.valid())
            return -1;
    }

    // Return the rightmost key in the leaf
    return child->getKey(child->getNumKeys() - 1);
}

int64_t BTree::getSuccessor(int page_id, int key_index)
{
    // Go to right child, then always go left
    PageHandle node(buffer_manager, page_id);
    if (!node.valid())
        return -1;

    int child_id = node->getChild(key_index + 1);
    PageHandle child(buffer_manager, child_id);
    if (!child.valid())
        return -1;

    // Keep going to the leftmost child until we reach a leaf
    while (!child->isLeaf())
    {
        child_id = child->getChild(0);
        child = PageHandle(buffer_manager, child_id);
        if (!child.valid())
            return -1;
    }

    // Return the leftmost key in the leaf
    return child->getKey(0);
}

void BTree::fixAfterDeletion(int page_id)
{
    PageHandle node(buffer_manager, page_id);
    if (!node.valid() || page_id == root_page_id)
        return;

    // Get parent
    int parent_id = node->getHeader().parent_page;
    PageHandle parent(buffer_manager, parent_id);
    if (!parent.valid())
        return;

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
        cerr << "Error: Child not found in parent\n";
        return;
    }

    // Try to borrow from left sibling
    if (child_index > 0)
    {
        int left_sibling_id = parent->getChild(child_index - 1);
        PageHandle left_sibling(buffer_manager, left_sibling_id);

        if (left_sibling.valid())
        {
            int min_keys = node->isLeaf() ? MIN_LEAF_KEYS : MIN_INTERNAL_KEYS;
            if (left_sibling->getNumKeys() > min_keys)
            {
                cout << "Borrowing from left sibling (page " << left_sibling_id << ")\n";
                borrowFromLeftSibling(parent_id, child_index);
                return;
            }
        }
    }

    // Try to borrow from right sibling
    if (child_index < parent->getNumKeys())
    {
        int right_sibling_id = parent->getChild(child_index + 1);
        PageHandle right_sibling(buffer_manager, right_sibling_id);

        if (right_sibling.valid())
        {
            int min_keys = node->isLeaf() ? MIN_LEAF_KEYS : MIN_INTERNAL_KEYS;
            if (right_sibling->getNumKeys() > min_keys)
            {
                cout << "Borrowing from right sibling (page " << right_sibling_id << ")\n";
                borrowFromRightSibling(parent_id, child_index);
                return;
            }
        }
    }

    // Can't borrow, must merge
    cout << "Merging node " << page_id << " with sibling\n";

    // Merge with left sibling if exists, otherwise with right
    if (child_index > 0)
    {
        mergeWithSibling(parent_id, child_index - 1, false); // node merges into left sibling
    }
    else
    {
        mergeWithSibling(parent_id, child_index, true); // right sibling merges into node
    }

    // Parent might now be underflowing
    if (parent_id != root_page_id)
    {
        PageHandle parent_reload(buffer_manager, parent_id);
        if (parent_reload.valid() && parent_reload->getNumKeys() < MIN_INTERNAL_KEYS)
        {
            cout << "Parent page " << parent_id << " underflow, fixing recursively\n";
            fixAfterDeletion(parent_id);
        }
    }
}

void BTree::borrowFromLeftSibling(int parent_id, int child_index)
{
    PageHandle parent(buffer_manager, parent_id);
    if (!parent.valid())
        return;

    int child_id = parent->getChild(child_index);
    int left_sibling_id = parent->getChild(child_index - 1);

    PageHandle child(buffer_manager, child_id);
    PageHandle left_sibling(buffer_manager, left_sibling_id);

    if (!child.valid() || !left_sibling.valid())
        return;

    if (child->isLeaf())
    {
        // For leaf nodes
        // Move last key from left sibling to first position in child
        int last_idx = left_sibling->getNumKeys() - 1;
        int64_t borrowed_key = left_sibling->getKey(last_idx);
        int64_t borrowed_value = left_sibling->getValue(last_idx);

        // Make room in child
        for (int i = child->getNumKeys() - 1; i >= 0; --i)
        {
            child->setKey(i + 1, child->getKey(i));
            child->setValue(i + 1, child->getValue(i));
        }

        // Insert borrowed key
        child->setKey(0, borrowed_key);
        child->setValue(0, borrowed_value);
        child->setNumKeys(child->getNumKeys() + 1);

        // Remove from left sibling
        left_sibling->setNumKeys(left_sibling->getNumKeys() - 1);

        // Update parent separator
        parent->setKey(child_index - 1, borrowed_key);
    }
    else
    {
        // For internal nodes
        // Move separator key from parent to child
        int64_t separator_key = parent->getKey(child_index - 1);

        // Make room in child
        for (int i = child->getNumKeys() - 1; i >= 0; --i)
        {
            child->setKey(i + 1, child->getKey(i));
        }
        for (int i = child->getNumKeys(); i >= 0; --i)
        {
            child->setChild(i + 1, child->getChild(i));
        }

        // Insert separator at beginning
        child->setKey(0, separator_key);
        child->setNumKeys(child->getNumKeys() + 1);

        // Move last child from left sibling to child
        int last_child_idx = left_sibling->getNumKeys();
        child->setChild(0, left_sibling->getChild(last_child_idx));

        // Update child's parent pointer
        int moved_child_id = left_sibling->getChild(last_child_idx);
        PageHandle moved_child(buffer_manager, moved_child_id);
        if (moved_child.valid())
        {
            moved_child->getHeader().parent_page = child_id;
            buffer_manager->markDirty(moved_child_id);
        }

        // Update parent separator with last key from left sibling
        int64_t new_separator = left_sibling->getKey(last_child_idx - 1);
        parent->setKey(child_index - 1, new_separator);

        // Remove from left sibling
        left_sibling->setNumKeys(left_sibling->getNumKeys() - 1);
    }

    buffer_manager->markDirty(parent_id);
    buffer_manager->markDirty(child_id);
    buffer_manager->markDirty(left_sibling_id);
}

void BTree::borrowFromRightSibling(int parent_id, int child_index)
{
    PageHandle parent(buffer_manager, parent_id);
    if (!parent.valid())
        return;

    int child_id = parent->getChild(child_index);
    int right_sibling_id = parent->getChild(child_index + 1);

    PageHandle child(buffer_manager, child_id);
    PageHandle right_sibling(buffer_manager, right_sibling_id);

    if (!child.valid() || !right_sibling.valid())
        return;

    if (child->isLeaf())
    {
        // For leaf nodes
        // Move first key from right sibling to last position in child
        int64_t borrowed_key = right_sibling->getKey(0);
        int64_t borrowed_value = right_sibling->getValue(0);

        // Add to child
        child->setKey(child->getNumKeys(), borrowed_key);
        child->setValue(child->getNumKeys(), borrowed_value);
        child->setNumKeys(child->getNumKeys() + 1);

        // Remove from right sibling
        for (int i = 1; i < right_sibling->getNumKeys(); ++i)
        {
            right_sibling->setKey(i - 1, right_sibling->getKey(i));
            right_sibling->setValue(i - 1, right_sibling->getValue(i));
        }
        right_sibling->setNumKeys(right_sibling->getNumKeys() - 1);

        // Update parent separator
        parent->setKey(child_index, right_sibling->getKey(0));
    }
    else
    {
        // For internal nodes
        // Move separator key from parent to child
        int64_t separator_key = parent->getKey(child_index);

        // Add separator to end of child
        child->setKey(child->getNumKeys(), separator_key);
        child->setNumKeys(child->getNumKeys() + 1);

        // Move first child from right sibling to child
        child->setChild(child->getNumKeys(), right_sibling->getChild(0));

        // Update child's parent pointer
        int moved_child_id = right_sibling->getChild(0);
        PageHandle moved_child(buffer_manager, moved_child_id);
        if (moved_child.valid())
        {
            moved_child->getHeader().parent_page = child_id;
            buffer_manager->markDirty(moved_child_id);
        }

        // Update parent separator with first key from right sibling
        int64_t new_separator = right_sibling->getKey(0);
        parent->setKey(child_index, new_separator);

        // Remove from right sibling
        for (int i = 1; i < right_sibling->getNumKeys(); ++i)
        {
            right_sibling->setKey(i - 1, right_sibling->getKey(i));
        }
        for (int i = 1; i <= right_sibling->getNumKeys(); ++i)
        {
            right_sibling->setChild(i - 1, right_sibling->getChild(i));
        }
        right_sibling->setNumKeys(right_sibling->getNumKeys() - 1);
    }

    buffer_manager->markDirty(parent_id);
    buffer_manager->markDirty(child_id);
    buffer_manager->markDirty(right_sibling_id);
}

void BTree::mergeWithSibling(int parent_id, int child_index, bool merge_with_left)
{
    // child_index is the index of the left child in the pair to merge
    PageHandle parent(buffer_manager, parent_id);
    if (!parent.valid())
        return;

    int left_child_id, right_child_id;
    if (merge_with_left)
    {
        left_child_id = parent->getChild(child_index);
        right_child_id = parent->getChild(child_index + 1);
    }
    else
    {
        // The parameter is actually the index of the right child
        left_child_id = parent->getChild(child_index - 1);
        right_child_id = parent->getChild(child_index);
        child_index--; // Adjust to left child index
    }

    PageHandle left_child(buffer_manager, left_child_id);
    PageHandle right_child(buffer_manager, right_child_id);

    if (!left_child.valid() || !right_child.valid())
        return;

    cout << "Merging pages " << left_child_id << " and " << right_child_id << "\n";

    // Get the separator key from parent
    int64_t separator_key = parent->getKey(child_index);

    if (left_child->isLeaf())
    {
        // Merge leaf nodes
        // Add separator key? Actually for B-Tree leaves, we don't add parent's key
        // Just copy all keys from right to left

        // Check capacity
        int total_keys = left_child->getNumKeys() + right_child->getNumKeys();
        if (total_keys > Page::MAX_LEAF_KEYS)
        {
            cerr << "ERROR: Cannot merge - would exceed leaf capacity ("
                 << total_keys << " > " << Page::MAX_LEAF_KEYS << ")\n";
            return;
        }

        // Copy keys and values from right to left
        int dest_idx = left_child->getNumKeys();
        for (int i = 0; i < right_child->getNumKeys(); ++i)
        {
            left_child->setKey(dest_idx + i, right_child->getKey(i));
            left_child->setValue(dest_idx + i, right_child->getValue(i));
        }

        left_child->setNumKeys(total_keys);

        // Update leaf chain
        left_child->setNextPage(right_child->getNextPage());
    }
    else
    {
        // Merge internal nodes
        // Add separator key to left child
        int dest_idx = left_child->getNumKeys();
        left_child->setKey(dest_idx, separator_key);
        left_child->setNumKeys(left_child->getNumKeys() + 1);

        // Copy keys from right child
        dest_idx = left_child->getNumKeys();
        for (int i = 0; i < right_child->getNumKeys(); ++i)
        {
            left_child->setKey(dest_idx + i, right_child->getKey(i));
        }

        // Copy children from right child
        int child_dest_idx = left_child->getNumKeys();
        for (int i = 0; i <= right_child->getNumKeys(); ++i)
        {
            left_child->setChild(child_dest_idx + i, right_child->getChild(i));

            // Update parent pointers of moved children
            int grandchild_id = right_child->getChild(i);
            PageHandle grandchild(buffer_manager, grandchild_id);
            if (grandchild.valid())
            {
                grandchild->getHeader().parent_page = left_child_id;
                buffer_manager->markDirty(grandchild_id);
            }
        }

        left_child->setNumKeys(left_child->getNumKeys() + right_child->getNumKeys());
    }

    // Remove separator and right child from parent
    for (int i = child_index + 1; i < parent->getNumKeys(); ++i)
    {
        parent->setKey(i - 1, parent->getKey(i));
    }
    for (int i = child_index + 2; i <= parent->getNumKeys(); ++i)
    {
        parent->setChild(i - 1, parent->getChild(i));
    }
    parent->setNumKeys(parent->getNumKeys() - 1);

    // Mark pages as dirty
    buffer_manager->markDirty(parent_id);
    buffer_manager->markDirty(left_child_id);

    // In production, we would add right_child_id to free list
    cout << "Merged page " << right_child_id << " into " << left_child_id << "\n";
}
