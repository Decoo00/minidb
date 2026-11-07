#include "lru.h"
#include <stdexcept>


LRU::LRU(size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::runtime_error("LRU::LRU: the cache capacity cannot be a zero.");
    }
}

std::optional<page_id_t> LRU::touch(page_id_t id) {
    auto found = pos_.find(id);
    if (found != pos_.end()) {
        order_.splice(order_.begin(), order_, found->second);
        return std::nullopt;
    }
    std::optional<page_id_t> evicted = std::nullopt;
    if (size() == capacity_) {
        evicted = order_.back();
        pos_.erase(order_.back());
        order_.pop_back();
    }
    order_.push_front(id);
    pos_[id] = order_.begin();
    return evicted;
}


bool LRU::contains(page_id_t id) const {
    return (pos_.find(id) != pos_.end());
}

size_t LRU::size() const {
    return order_.size();
}