#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include "replacer.h"
#include "pager.h"
#include "page_store.h"

class BufferPool : public PageStore {
public:
    // pager는 pool보다 오래 살아야 함.
    BufferPool(Pager& pager, std::unique_ptr<Replacer> replacer);
    // flush를 시도하되 실패해도 예외를 던지지 않음. 에러를 확인하려면 flush()를 직접 호출.
    ~BufferPool();
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    page_id_t allocate() override;
    void read(page_id_t id, char* buf) override;
    void write(page_id_t id, const char* buf) override;
    page_id_t page_count() const override;

    // dirty page를 전부 디스크에 쓰고 fsync.
    void flush();

    uint64_t hit_count() const;
    uint64_t miss_count() const;

private:
    struct Frame {
        std::array<char, PAGE_SIZE> data;
        bool dirty = false;
    };

    // id의 frame을 반환. 없으면 frame을 만들고(필요하면 Replacer가 고른 page를 내보냄),
    // load가 true일 때만 디스크에서 내용을 읽어옴.
    // victim에 대한 pwrite 중 예외 발생시 Pool의 상태 보장 안 함
    Frame& fetch(page_id_t id, bool load);

    Pager& pager_;
    std::unique_ptr<Replacer> replacer_;
    std::unordered_map<page_id_t, Frame> frames_;
    uint64_t hit_count_ = 0;
    uint64_t miss_count_ = 0;
};