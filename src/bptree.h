#pragma once
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "node.h"
#include "page_store.h"

constexpr char META_MAGIC[] = "Hello DB";
constexpr size_t META_MAGIC_SIZE = sizeof(META_MAGIC) - 1;

class BPlusTree {
public:
    // 빈 파일이면 메타 page(0)와 빈 root leaf를 만들고, 아니면 메타를 읽어 이어서 사용.
    // magic이 다르면 예외. pager는 tree보다 오래 살아야 함.
    explicit BPlusTree(PageStore& page_store);

    // 이미 있는 key면 false(값은 덮어쓰지 않음), 삽입했으면 true.
    bool insert(Key key, Value value);
    std::optional<Value> find(Key key) const;
    // lo <= key <= hi 인 항목을 key 오름차순으로.
    std::vector<std::pair<Key, Value>> range(Key lo, Key hi) const;
    size_t size() const;

    // 테스트용. 트리 불변식이 깨졌으면 예외.
    void check() const;

private:
    struct Split {
        Key key;                      // 부모에 올릴 separator
        page_id_t right;  // 새로 생긴 오른쪽 노드
    };
    struct InsertResult {
        bool inserted;                // false면 중복 key, 아무것도 안 바뀜
        std::optional<Split> split;
    };

    Node load(page_id_t id) const;
    void store(page_id_t id, const Node& node);
     
    void load_meta();
    // [0..] : magic text
    // [64..67] : root_id_
    // [72..79] : size_ 
    void store_meta();    

    Node find_leaf(Key key) const;
    // 노드가 넘쳐서 쪼개졌으면 Split을 반환
    InsertResult insert_into(page_id_t id, Key key, Value value);

    PageStore& store_;
    page_id_t root_id_;
    size_t size_;
};