#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

using page_id_t = uint32_t;
constexpr size_t PAGE_SIZE = 4096;

class Pager {
public:
    // 파일을 열고, 없으면 생성. 파일 크기가 PAGE_SIZE의 배수가 아니면 예외.
    explicit Pager(const std::string& path);
    ~Pager();
    Pager(const Pager&) = delete;
    Pager& operator=(const Pager&) = delete;

    // 파일 끝에 0으로 채워진 page 하나를 추가하고 그 번호를 반환.
    page_id_t allocate();

    // buf는 최소 PAGE_SIZE 바이트. id가 범위 밖이면 예외.
    void read(page_id_t id, char* buf) const;
    void write(page_id_t id, const char* buf);

    // 쓴 내용이 디스크에 내려가도록 강제.
    void sync();

    page_id_t page_count() const;
    uint64_t read_count() const;
    uint64_t write_count() const;

private:
    int fd_;
    page_id_t page_count_;
    mutable uint64_t read_count_ = 0;
    uint64_t write_count_ = 0;
};