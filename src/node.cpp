#include "node.h"

#include <cstring>
#include <stdexcept>

void serialize(const Node& node, char* buf) {
    memset(buf, 0, PAGE_SIZE);
    buf[0] = static_cast<char>(node.is_leaf);
    uint16_t num_keys = node.keys.size();
    uint32_t next = node.next;

    if (num_keys > MAX_KEYS) {
        throw std::runtime_error("Node::serialize: num_keys " + std::to_string(num_keys)
            + " exceeds MAX_KEYS " + std::to_string(MAX_KEYS));
    }

    memcpy(buf + 2, &num_keys, 2);
    
    if(node.is_leaf) {
        memcpy(buf + 4, &next, 4);
    }
    if (num_keys != 0) {
        memcpy(buf + 8, node.keys.data(), sizeof(Key) * num_keys);
    }
    if(node.is_leaf) {
        if (num_keys != 0) {
            memcpy(buf + 8 + sizeof(Key) * num_keys, node.values.data(), sizeof(Value) * num_keys);
        }
    } else {
        memcpy(buf + 8 + sizeof(Key) * num_keys, node.children.data(), sizeof(page_id_t) * (num_keys + 1));
    }
}

Node deserialize(const char* buf) {
    bool is_leaf = (buf[0] != 0);
    Node node = Node(is_leaf);
    uint16_t num_keys;
    uint32_t next;

    memcpy(&num_keys, buf + 2, 2);

    if (num_keys > MAX_KEYS) {
        throw std::runtime_error("Node::deserialize: num_keys " + std::to_string(num_keys)
            + " exceeds MAX_KEYS " + std::to_string(MAX_KEYS));
    }

    if(is_leaf) {
        memcpy(&next, buf + 4, 4);
        node.next = next;
    }

    if (num_keys != 0) {
        node.keys.resize(num_keys);
        memcpy(node.keys.data(), buf + 8, sizeof(Key) * num_keys);
    }
    if(is_leaf) {
        if (num_keys != 0) {
            node.values.resize(num_keys);
            memcpy(node.values.data(), buf + 8 + sizeof(Key) * num_keys, sizeof(Value) * num_keys);
        }
    } else {
        node.children.resize(num_keys + 1);
        memcpy(node.children.data(), buf + 8 + sizeof(Key) * num_keys, sizeof(page_id_t) * (num_keys + 1));
    }

    return node;
}