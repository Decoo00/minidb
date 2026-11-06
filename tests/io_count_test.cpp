#include "bptree.h"
#include "pager.h"
#include <cstdio>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

static const char* DB_PATH = "io_count_test.db";
constexpr uint64_t HALF = MAX_KEYS / 2;

static void require(bool cond, const std::string& msg) {
    if (!cond) throw std::runtime_error(msg);
}

// find 한 번의 읽기 횟수 = 트리 높이
static uint64_t height(const BPlusTree& tree, const Pager& pager, Key k) {
    uint64_t r0 = pager.read_count(), w0 = pager.write_count();
    tree.find(k);
    require(pager.write_count() == w0, "find wrote a page");
    return pager.read_count() - r0;
}

// 노드가 최소한만 차 있을 때 key n개로 만들 수 있는 최대 높이.
// 높이 h(>=2)의 최소 key 수 = 2 * (HALF+1)^(h-2) * HALF  (root 자식 2개, internal 자식 HALF+1개, leaf key HALF개)
static uint64_t max_height(uint64_t n) {
    uint64_t h = 1, min_keys = 2 * HALF;
    while (min_keys <= n) {
        ++h;
        min_keys *= HALF + 1;
    }
    return h;
}

int main() {
    try {
        std::remove(DB_PATH);
        Pager pager(DB_PATH);
        BPlusTree tree(pager);
        std::mt19937 rng(777);

        const Key N = 50000;
        std::vector<Key> keys(N);
        std::iota(keys.begin(), keys.end(), 0);
        std::shuffle(keys.begin(), keys.end(), rng);

        uint64_t h = height(tree, pager, 0);
        require(h == 1, "empty tree height " + std::to_string(h));
        uint64_t total_splits = 0;

        // 새 key insert: 읽기 = h, 쓰기 = 2 + 2s (leaf 1 + meta 1 + split마다 오른쪽 노드 1 + 부모 1)
        for (Key k : keys) {
            uint64_t r0 = pager.read_count(), w0 = pager.write_count();
            uint64_t p0 = pager.page_count();
            require(tree.insert(k, k), "insert(" + std::to_string(k) + ") returned false");
            uint64_t reads = pager.read_count() - r0;
            uint64_t writes = pager.write_count() - w0;
            uint64_t allocs = pager.page_count() - p0;

            uint64_t h_after = height(tree, pager, k);
            require(h_after == h || h_after == h + 1,
                    "height jumped " + std::to_string(h) + " -> " + std::to_string(h_after));
            uint64_t root_split = h_after - h;
            uint64_t splits = allocs - root_split;

            require(reads == h, "insert(" + std::to_string(k) + "): " + std::to_string(reads)
                    + " reads, expected " + std::to_string(h));
            require(writes == 2 + 2 * splits, "insert(" + std::to_string(k) + "): " + std::to_string(writes)
                    + " writes, expected " + std::to_string(2 + 2 * splits) + " (splits=" + std::to_string(splits) + ")");
            total_splits += splits;
            h = h_after;
        }
        require(h <= max_height(N), "height " + std::to_string(h) + " exceeds bound " + std::to_string(max_height(N)));

        std::uniform_int_distribution<Key> any(-N, 2 * N);
        std::uniform_int_distribution<Key> existing(0, N - 1);

        // 중복 insert: 읽기 = h, 쓰기 0, page 할당 0
        for (int i = 0; i < 1000; ++i) {
            Key k = existing(rng);
            uint64_t r0 = pager.read_count(), w0 = pager.write_count();
            uint64_t p0 = pager.page_count();
            require(!tree.insert(k, -1), "duplicate insert(" + std::to_string(k) + ") returned true");
            require(pager.read_count() - r0 == h, "duplicate insert: wrong read count");
            require(pager.write_count() == w0, "duplicate insert wrote a page");
            require(pager.page_count() == p0, "duplicate insert allocated a page");
        }

        // find: 있든 없든 읽기 = h
        for (int i = 0; i < 1000; ++i) {
            Key k = any(rng);
            require(height(tree, pager, k) == h, "find(" + std::to_string(k) + "): wrong read count");
        }

        // range: h <= 읽기 <= h + 결과수/HALF + 2 (중간 leaf는 최소 HALF개를 담고, 첫 leaf와 마지막 leaf는 일부만 쓸 수 있음)
        for (int i = 0; i < 1000; ++i) {
            Key lo = any(rng), hi = any(rng);
            uint64_t r0 = pager.read_count(), w0 = pager.write_count();
            size_t n = tree.range(lo, hi).size();
            uint64_t reads = pager.read_count() - r0;
            require(pager.write_count() == w0, "range wrote a page");
            require(reads >= h && reads <= h + n / HALF + 2,
                    "range(" + std::to_string(lo) + ", " + std::to_string(hi) + "): "
                    + std::to_string(reads) + " reads for " + std::to_string(n) + " results");
        }

        tree.check();
        std::printf("OK   N=%lld MAX_KEYS=%zu height=%llu (bound %llu) splits=%llu pages=%llu\n",
                    static_cast<long long>(N), MAX_KEYS,
                    static_cast<unsigned long long>(h), static_cast<unsigned long long>(max_height(N)),
                    static_cast<unsigned long long>(total_splits),
                    static_cast<unsigned long long>(pager.page_count()));
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
        std::remove(DB_PATH);
        return 1;
    }
    std::remove(DB_PATH);
    return 0;
}