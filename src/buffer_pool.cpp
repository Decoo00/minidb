#include "buffer_pool.h"
#include <cstring>
#include <string>
#include <stdexcept>
#include <iostream>


BufferPool::BufferPool(Pager& pager, std::unique_ptr<Replacer> replacer) : pager_(pager), replacer_(std::move(replacer)) {
    if(!replacer_) {
        throw std::runtime_error("BufferPool::BufferPool: replacer is null");
    }
}
    
BufferPool::~BufferPool() {
    try{
        flush();
    } catch (const std::exception& e){
        std::cerr<<"BufferPool::~BufferPool: an exception occurred: " << e.what() << ", ignoring anyway." << std::endl;
    }
}


page_id_t BufferPool::allocate() {
    return pager_.allocate();
}

void BufferPool::read(page_id_t id, char* buf) {
    memcpy(buf, fetch(id, true).data.data(), PAGE_SIZE);
}

void BufferPool::write(page_id_t id, const char* buf) {
    Frame& frame = fetch(id, false);
    memcpy(frame.data.data(), buf, PAGE_SIZE);
    frame.dirty = true;
}

page_id_t BufferPool::page_count() const {
    return pager_.page_count();
}

// dirty page를 전부 디스크에 쓰고 fsync.
void BufferPool::flush() {
    for(auto& [id, frame] : frames_) {
        if (frame.dirty) {
            pager_.write(id, frame.data.data());
            frame.dirty = false;
        }
    }
    pager_.sync();
}

uint64_t BufferPool::hit_count() const {
    return hit_count_;
}
uint64_t BufferPool::miss_count() const {
    return miss_count_;
}

BufferPool::Frame& BufferPool::fetch(page_id_t id, bool load) {
    auto found = frames_.find(id);
    if (found != frames_.end()) {       // Hit
        hit_count_++;
        replacer_ -> touch(id);
        return found->second;
    } else {                            // Miss
        miss_count_++;
        char rbuf[PAGE_SIZE];

        // load인 경우 read부터 시도 (read 중 예외 발생할 경우에 문제없이 종료 가능함)
        if (load) pager_.read(id, rbuf);
        Frame& new_frame = frames_[id];
        if (load) memcpy(new_frame.data.data(), rbuf, PAGE_SIZE);

        auto victim = replacer_ -> touch(id);
        if (victim.has_value()) {
            page_id_t victim_id = victim.value();
            auto node = frames_.extract(victim_id);
            
            if (victim_id == id) {
                throw std::runtime_error("BufferPool::fetch: replacer evicted the page it was just asked to admit (page id "
                                        + std::to_string(id) + ")");
            }
            if (node.mapped().dirty) {
                pager_.write(victim_id, node.mapped().data.data());
            }   
        }
        
        return new_frame;
    }
}