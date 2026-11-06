#include "bptree.h"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cstring>

constexpr size_t HALF_MAX_KEYS = MAX_KEYS / 2;

BPlusTree::BPlusTree(Pager& pager) : pager_(pager), root_id_(NO_PAGE), size_(0) {
    if (pager.page_count() == 0) {
        pager.allocate();
        
        root_id_ = pager.allocate();
        store(root_id_, Node(true));
        
        store_meta();
    } else {
        load_meta();
        
    }
}

Node BPlusTree::find_leaf(Key key) const {
    Node node = load(root_id_);
    while(!node.is_leaf){
        auto it = std::upper_bound(node.keys.begin(), node.keys.end(), key);
        node = load(node.children[it - node.keys.begin()]);
    }
    return node;
}

std::optional<Value> BPlusTree::find(Key key) const {
    Node leaf = find_leaf(key);
    auto it = std::lower_bound(leaf.keys.begin(), leaf.keys.end(), key);
    if(it == leaf.keys.end() || *it != key) {
        return std::nullopt;
    }
    return leaf.values[it - leaf.keys.begin()];
}



BPlusTree::InsertResult BPlusTree::insert_into(page_id_t id, Key key, Value value) {
    Node node = load(id);
    if(!node.is_leaf){
        auto it = std::upper_bound(node.keys.begin(), node.keys.end(), key);
        const auto child_index = it - node.keys.begin();
        page_id_t child_id = node.children[child_index];
        auto [inserted, split] = insert_into(child_id, key, value);
        if(split.has_value()){
            node.keys.insert(node.keys.begin() + child_index, split.value().key);
            node.children.insert(node.children.begin() + child_index + 1, split.value().right);
            
            if(node.keys.size() > MAX_KEYS) {
                // Split : right child
                Key sepKey = node.keys[HALF_MAX_KEYS];
                Node right = Node(false);
                page_id_t right_id = pager_.allocate();
            
                // Opt the middle one
                right.keys = std::vector<Key>(
                    node.keys.begin() + HALF_MAX_KEYS + 1, 
                    node.keys.end());
                right.children = std::vector<page_id_t>(
                    node.children.begin() + HALF_MAX_KEYS + 1, 
                    node.children.end());

                // Split : left child
                node.keys.resize(HALF_MAX_KEYS);
                node.children.resize(HALF_MAX_KEYS + 1);
                
                store(id, node);
                store(right_id, right);
                return {inserted, Split{sepKey, right_id}};
            } else {
                store(id, node);
                return {inserted, std::nullopt};
            }
        }
        return {inserted, std::nullopt};
    } else {
        auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key);
        if (it != node.keys.end() && *it == key) {
            // 중복 키
            return {false, std::nullopt};
        }
        int index = it - node.keys.begin();
        node.keys.insert(node.keys.begin() + index, key);
        node.values.insert(node.values.begin() + index, value);

        if(node.keys.size() > MAX_KEYS) {
            // Split : right child
            Key sepKey = node.keys[HALF_MAX_KEYS];
            Node right = Node(true);
            page_id_t right_id = pager_.allocate();

            right.keys = std::vector<Key>(node.keys.begin() + HALF_MAX_KEYS, node.keys.end());
            right.values = std::vector<Value>(node.values.begin() + HALF_MAX_KEYS, node.values.end());
            right.next = node.next;

            // Split : left child
            node.keys.resize(HALF_MAX_KEYS);
            node.values.resize(HALF_MAX_KEYS);
            node.next = right_id;

            store(id, node);
            store(right_id, right);
            return {true, Split{sepKey, right_id}};
        } else {
            store(id, node);
            return {true, std::nullopt};
        }
    }
}

bool BPlusTree::insert(Key key, Value value) {
    auto [inserted, split] = insert_into(root_id_, key, value);
    if (!inserted) return false;
    if (split.has_value()) {
        // Create new root
        Node new_root = Node(false);
        page_id_t new_root_id = pager_.allocate();

        new_root.keys = std::vector<Key>{split.value().key};
        new_root.children.push_back(root_id_);
        new_root.children.push_back(split.value().right);
        
        root_id_ = new_root_id;
        store(new_root_id, new_root);
    }
    size_++;
    store_meta();
    return true;
}

std::vector<std::pair<Key, Value>> BPlusTree::range(Key lo, Key hi) const {
    std::vector<std::pair<Key, Value>> kvrange;

    Node leaf = find_leaf(lo);
    size_t idx = static_cast<size_t>(std::lower_bound(
        leaf.keys.begin(), leaf.keys.end(), lo) - leaf.keys.begin());
    
    while(true) {
        if (idx == leaf.keys.size()) {
            if (leaf.next == NO_PAGE) break;
            leaf = load(leaf.next);
            idx = 0;
        }
        if (leaf.keys[idx] > hi) break;
        kvrange.emplace_back(leaf.keys[idx], leaf.values[idx]);
        idx++;
    }

    return kvrange;
}

Node BPlusTree::load(page_id_t id) const {
    if (id == NO_PAGE) throw std::runtime_error("BPlusTree::load: cannot load page 0 (meta page)");
    char rbuf[PAGE_SIZE];
    pager_.read(id, rbuf);
    return deserialize(rbuf);
}

void BPlusTree::store(page_id_t id, const Node& node) {
    if (id == NO_PAGE) throw std::runtime_error("BPlusTree::store: cannot store page 0 (meta page)");
    char wbuf[PAGE_SIZE];
    serialize(node, wbuf);
    pager_.write(id, wbuf);
}

void BPlusTree::store_meta() {
    char wbuf[PAGE_SIZE];
    uint64_t size = static_cast<uint64_t>(size_);
    memset(wbuf, 0, PAGE_SIZE);
    memcpy(wbuf, META_MAGIC, META_MAGIC_SIZE);
    memcpy(wbuf + 64, &root_id_, sizeof(page_id_t));
    memcpy(wbuf + 72, &size, sizeof(uint64_t));
    pager_.write(0, wbuf);
}

void BPlusTree::load_meta() {
    char rbuf[PAGE_SIZE];
    uint64_t size;
    pager_.read(0, rbuf);
    if (memcmp(rbuf, META_MAGIC, META_MAGIC_SIZE) != 0) {
        throw std::runtime_error(std::string("BPlusTree::load_meta: Invalid magic: ")
            + std::string(rbuf, META_MAGIC_SIZE));
    }
    
    memcpy(&root_id_, rbuf + 64, sizeof(page_id_t));
    memcpy(&size, rbuf + 72, sizeof(uint64_t));
    size_ = size;
}

size_t BPlusTree::size() const {
    return size_;
}

void BPlusTree::check() const {
    size_t total = 0;
    int leaf_depth = -1;
    if (root_id_ == NO_PAGE) {
        throw std::runtime_error("BPlusTree::check: root is null");
    }
    Node root = load(root_id_);

    if (root.keys.size() > MAX_KEYS) {
        throw std::runtime_error("BPlusTree::check: root node has "
            + std::to_string(root.keys.size()) + " keys (max: " 
            + std::to_string(MAX_KEYS) + ")");
    }
    if (!root.is_leaf && root.keys.size() == 0) {
        throw std::runtime_error("BPlusTree::check: internal root has no keys");
    }

    struct LeafChain {
        page_id_t curr;
        page_id_t next;
    };
    std::vector<LeafChain> leaves;

    std::function<void(page_id_t, 
        const std::optional<Key>, 
        const std::optional<Key>, 
        const int)> visit = 
            [&](page_id_t id, 
                const std::optional<Key> lo, 
                const std::optional<Key> hi, 
                const int depth) {

        Node node = load(id);
        // 2. Sorted Keys
        auto bad = std::adjacent_find(node.keys.begin(), node.keys.end(), std::greater_equal<Key>());
        if (bad != node.keys.end()) {
            throw std::runtime_error("BPlusTree::check: keys not strictly ascending ("
                + std::to_string(*bad) + " then " + std::to_string(*(bad + 1)) + ")");
        }
        // 3. #Keys
        if (id != root_id_ && (node.keys.size() < HALF_MAX_KEYS || node.keys.size() > MAX_KEYS)) {
            throw std::runtime_error("BPlusTree::check: node has " 
                + std::to_string(node.keys.size()) + " keys (allowed: " 
                + std::to_string(HALF_MAX_KEYS) + ".." 
                + std::to_string(MAX_KEYS) + ")");
        }
        // 4. Key range
        if ((lo.has_value() && node.keys.front() < lo) || (hi.has_value() && node.keys.back() >= hi)) {
            throw std::runtime_error("BPlusTree::check: key out of subtree range [" 
                + (lo.has_value() ? std::to_string(lo.value()) : std::string("-inf")) + ", " 
                + (hi.has_value() ? std::to_string(hi.value()) : std::string("inf")) + ")");
        }


        if(node.is_leaf) {
            leaves.push_back({id, node.next});
            total += node.keys.size();
            // Leaf
            // 1. Node shape
            if (node.keys.size() != node.values.size()) {
                throw std::runtime_error(
                    std::string("BPlusTree::check: leaf has ") 
                    + std::to_string(node.values.size()) + " values for " 
                    + std::to_string(node.keys.size()) + " keys");
            }
            if (!node.children.empty()) {
                throw std::runtime_error("BPlusTree::check: leaf has children");
            }

            // 5. Leaf depth
            if(leaf_depth == -1) {
                leaf_depth = depth;
            } else {
                if(leaf_depth != depth) {
                    throw std::runtime_error("BPlusTree::check: leaf at depth " 
                        + std::to_string(depth) + ", expected " 
                        + std::to_string(leaf_depth));
                }
            }


        } else {
            // Internal
            // 1. Node shape
            if (node.keys.size() + 1 != node.children.size()) {
                throw std::runtime_error(
                    std::string("BPlusTree::check: internal node has ") 
                    + std::to_string(node.children.size()) + " children for " 
                    + std::to_string(node.keys.size()) + " keys (expected keys + 1)");
            }
            if (!node.values.empty()) {
                throw std::runtime_error("BPlusTree::check: internal node has values");
            }
            if (node.next != NO_PAGE) {
                throw std::runtime_error("BPlusTree::check: internal node has next pointer");
            }



            // Recursion
            for(size_t i=0 ; i < node.children.size() ; ++i) {
                const page_id_t child = node.children[i]; 
                std::optional<Key> lo = std::nullopt, hi = std::nullopt;
                if (i>0) {
                    lo = node.keys[i-1];
                }
                if (i < node.children.size() - 1) {
                    hi = node.keys[i];
                }
                visit(child, lo, hi, depth + 1);
            }
        }
    };
    visit(root_id_, std::nullopt, std::nullopt, 0);

    // 6. leaf chain
    for (size_t i = 0; i + 1 < leaves.size(); ++i) {
        if (leaves[i].next != leaves[i + 1].curr) {
            throw std::runtime_error("BPlusTree::check: leaf #" + std::to_string(i)
                + ": next does not point to the following leaf");
        }
    }
    if (leaves.back().next != NO_PAGE) {
        throw std::runtime_error("BPlusTree::check: last leaf has non-null next");
    }

    // 7. size_
    if (total != size_) {
        throw std::runtime_error("BPlusTree::check: size_ is " + std::to_string(size_)
            + ", but leaves hold " + std::to_string(total) + " keys");
    }

    return;
}