#pragma once

using page_id_t = uint32_t;
constexpr size_t PAGE_SIZE = 4096;

class PageStore {
public:
    virtual ~PageStore() = default;
    virtual page_id_t allocate() = 0;
    virtual void read(page_id_t id, char* buf) = 0;
    virtual void write(page_id_t id, const char* buf) = 0;
    virtual page_id_t page_count() const = 0;
};