#pragma once
#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include "pager.h"
#include "replacer.h"

class LRU : public Replacer {
public:
    // capacity가 0이면 예외.
    explicit LRU(size_t capacity);

    // id를 가장 최근에 쓴 것으로 표시하고, 없으면 새로 추가.
    // 추가 때문에 capacity를 넘으면 가장 오래 안 쓴 id를 빼고 그 id를 반환.
    std::optional<page_id_t> touch(page_id_t id) override;

    bool contains(page_id_t id) const;
    size_t size() const;

private:
    size_t capacity_;
    std::list<page_id_t> order_;   // front: 가장 최근, back: 가장 오래됨
    std::unordered_map<page_id_t, std::list<page_id_t>::iterator> pos_;
};