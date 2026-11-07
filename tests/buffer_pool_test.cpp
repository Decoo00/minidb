#include "buffer_pool.h"
#include "lru.h"
#include "pager.h"
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

using Page = std::array<char, PAGE_SIZE>;
static const char* DB_PATH = "buffer_pool_test.db";

static void require(bool cond, const std::string& msg) {
    if (!cond) throw std::runtime_error(msg);
}

static Page make_page(uint32_t seed) {
    Page p;
    for (size_t i = 0; i < PAGE_SIZE; ++i) p[i] = static_cast<char>((seed * 131 + i * 7) & 0xFF);
    return p;
}

static Page read_page(BufferPool& pool, page_id_t id) {
    Page p;
    pool.read(id, p.data());
    return p;
}

static void test_hit() {
    std::remove(DB_PATH);
    Pager pager(DB_PATH);
    BufferPool pool(pager, std::make_unique<LRU>(4));
    page_id_t a = pool.allocate();

    uint64_t r0 = pager.read_count();
    pool.write(a, make_page(1).data());
    require(pager.read_count() == r0, "hit: write miss read from disk");
    require(read_page(pool, a) == make_page(1), "hit: content mismatch");
    require(pager.read_count() == r0, "hit: read after write went to disk");
    require(pool.miss_count() == 1 && pool.hit_count() == 1, "hit: wrong hit/miss count");
}

static void test_eviction() {
    std::remove(DB_PATH);
    Pager pager(DB_PATH);
    BufferPool pool(pager, std::make_unique<LRU>(2));
    page_id_t a = pool.allocate(), b = pool.allocate(), c = pool.allocate();
    uint64_t r0 = pager.read_count(), w0 = pager.write_count();

    pool.write(a, make_page(10).data());
    pool.write(b, make_page(11).data());
    require(pager.write_count() == w0, "eviction: wrote before pool was full");

    pool.write(c, make_page(12).data());                  // a(dirty) 내보냄
    require(pager.write_count() == w0 + 1, "eviction: dirty victim not written back");

    require(read_page(pool, a) == make_page(10), "eviction: evicted page content lost");   // b(dirty) 내보냄
    require(pager.read_count() == r0 + 1, "eviction: evicted page not re-read from disk");
    require(pager.write_count() == w0 + 2, "eviction: second dirty victim not written back");

    pool.flush();
    uint64_t w1 = pager.write_count();
    read_page(pool, b);                                   // c(clean) 내보냄
    require(pager.write_count() == w1, "eviction: clean victim was written");
}

static void test_flush() {
    std::remove(DB_PATH);
    Pager pager(DB_PATH);
    BufferPool pool(pager, std::make_unique<LRU>(4));
    page_id_t a = pool.allocate(), b = pool.allocate(), c = pool.allocate();

    pool.write(a, make_page(20).data());
    pool.write(b, make_page(21).data());
    read_page(pool, c);                                   // clean으로 올라옴

    uint64_t w0 = pager.write_count();
    pool.flush();
    require(pager.write_count() - w0 == 2, "flush: should write exactly the 2 dirty pages");
    pool.flush();
    require(pager.write_count() - w0 == 2, "flush: second flush wrote again");
}

static void test_destructor_flush() {
    std::remove(DB_PATH);
    {
        Pager pager(DB_PATH);
        BufferPool pool(pager, std::make_unique<LRU>(4));
        pool.write(pool.allocate(), make_page(99).data());
    }   // pool이 먼저 소멸하면서 flush
    Pager pager(DB_PATH);
    Page p;
    pager.read(0, p.data());
    require(p == make_page(99), "destructor: dirty page not flushed on destruction");
}

static void test_random() {
    std::remove(DB_PATH);
    const page_id_t PAGES = 32;
    const int OPS = 20000;
    std::map<page_id_t, Page> ref;
    {
        Pager pager(DB_PATH);
        BufferPool pool(pager, std::make_unique<LRU>(4));
        for (page_id_t i = 0; i < PAGES; ++i) ref[pool.allocate()] = Page{};

        std::mt19937 rng(4242);
        std::uniform_int_distribution<page_id_t> pick(0, PAGES - 1);
        std::uniform_int_distribution<int> coin(0, 1);
        for (int op = 0; op < OPS; ++op) {
            page_id_t id = pick(rng);
            if (coin(rng)) {
                Page p = make_page(static_cast<uint32_t>(op));
                pool.write(id, p.data());
                ref[id] = p;
            } else {
                require(read_page(pool, id) == ref[id],
                        "random: page " + std::to_string(id) + " mismatch at op " + std::to_string(op));
            }
        }
        require(pool.hit_count() + pool.miss_count() == static_cast<uint64_t>(OPS), "random: hit + miss != ops");
        pool.flush();
    }

    Pager pager(DB_PATH);
    for (const auto& [id, expected] : ref) {
        Page p;
        pager.read(id, p.data());
        require(p == expected, "random: page " + std::to_string(id) + " not persisted");
    }
}

static void test_null_replacer() {
    std::remove(DB_PATH);
    Pager pager(DB_PATH);
    bool threw = false;
    try {
        BufferPool pool(pager, nullptr);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "null replacer: accepted");
}

int main() {
    try {
        test_hit();              std::printf("OK   hit\n");
        test_eviction();         std::printf("OK   eviction\n");
        test_flush();            std::printf("OK   flush\n");
        test_destructor_flush(); std::printf("OK   destructor flush\n");
        test_random();           std::printf("OK   random\n");
        test_null_replacer();    std::printf("OK   null replacer\n");
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
        std::remove(DB_PATH);
        return 1;
    }
    std::remove(DB_PATH);
    return 0;
}