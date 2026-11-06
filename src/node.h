#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "pager.h"

using Key = int64_t;
using Value = int64_t;

constexpr size_t MAX_KEYS = 255;
constexpr page_id_t NO_PAGE = 0;   // page 0은 메타 page라 노드가 될 수 없음 → "없음" 표시로 사용

struct Node {
    bool is_leaf;
    std::vector<Key> keys;
    std::vector<Value> values;         // leaf 전용
    std::vector<page_id_t> children;   // internal 전용: keys.size() + 1개
    page_id_t next = NO_PAGE;          // leaf 전용
    explicit Node(bool leaf) : is_leaf(leaf) {}
};

// page 레이아웃 (host byte order)
//   [0]    uint8   is_leaf
//   [1]    uint8   padding
//   [2..3] uint16  num_keys
//   [4..7] uint32  next          (leaf만 사용)
//   [8..]  int64   keys[num_keys]
//          leaf:     int64  values[num_keys]
//          internal: uint32 children[num_keys + 1]
constexpr size_t NODE_HEADER_SIZE = 8;
static_assert(NODE_HEADER_SIZE + MAX_KEYS * (sizeof(Key) + sizeof(Value)) <= PAGE_SIZE,
              "MAX_KEYS too large for a leaf page");
static_assert(NODE_HEADER_SIZE + MAX_KEYS * sizeof(Key) + (MAX_KEYS + 1) * sizeof(page_id_t) <= PAGE_SIZE,
              "MAX_KEYS too large for an internal page");

// keys.size() <= MAX_KEYS 여야 함. buf는 PAGE_SIZE 바이트, 안 쓴 부분은 0으로 채움.
void serialize(const Node& node, char* buf);
// 내용이 레이아웃과 안 맞으면(num_keys > MAX_KEYS 등) 예외.
Node deserialize(const char* buf);