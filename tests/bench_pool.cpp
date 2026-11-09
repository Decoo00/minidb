#include "bptree.h"
#include "buffer_pool.h"
#include "lru.h"
#include "pager.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static const char* DB_PATH = "bench_pool.db";

struct Result {
    uint64_t logical;      // 트리가 store에 요청한 read + write 수
    uint64_t disk_reads;
    uint64_t disk_writes;
    double hit_rate;       // pool이 없으면 음수
    double seconds;
};

static void workload(BPlusTree& tree, const std::vector<Key>& keys, const std::vector<Key>& probes) {
    for (Key k : keys) tree.insert(k, k);
    for (Key k : probes) tree.find(k);
}

static double since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

static Result run_direct(const std::vector<Key>& keys, const std::vector<Key>& probes) {
    std::remove(DB_PATH);
    Pager pager(DB_PATH);
    auto t0 = std::chrono::steady_clock::now();
    {
        BPlusTree tree(pager);
        workload(tree, keys, probes);
    }
    pager.sync();
    double secs = since(t0);
    return {pager.read_count() + pager.write_count(), pager.read_count(), pager.write_count(), -1.0, secs};
}

static Result run_pool(size_t pages, const std::vector<Key>& keys, const std::vector<Key>& probes) {
    std::remove(DB_PATH);
    Pager pager(DB_PATH);
    BufferPool pool(pager, std::make_unique<LRU>(pages));
    auto t0 = std::chrono::steady_clock::now();
    {
        BPlusTree tree(pool);
        workload(tree, keys, probes);
    }
    pool.flush();   // 남은 dirty page의 write-back까지 측정에 포함
    double secs = since(t0);
    uint64_t logical = pool.hit_count() + pool.miss_count();
    return {logical, pager.read_count(), pager.write_count(),
            static_cast<double>(pool.hit_count()) / logical, secs};
}

static void print_row(const std::string& label, const Result& r) {
    char hit[16];
    if (r.hit_rate < 0) std::snprintf(hit, sizeof(hit), "-");
    else std::snprintf(hit, sizeof(hit), "%.1f%%", r.hit_rate * 100);
    std::printf("%-6s %10llu %12llu %12llu %9s %9.3f\n", label.c_str(),
                static_cast<unsigned long long>(r.logical),
                static_cast<unsigned long long>(r.disk_reads),
                static_cast<unsigned long long>(r.disk_writes), hit, r.seconds);
}

int main(int argc, char** argv) {
    std::vector<size_t> sizes = {1, 2, 4, 8, 16, 64, 256, 1024};
    bool with_baseline = true;
    if (argc > 1) {
        sizes = {std::stoul(argv[1])};
        with_baseline = false;
    }
    try {
        const Key N = 100000;
        std::mt19937 rng(2026);

        std::vector<Key> keys(N);
        std::iota(keys.begin(), keys.end(), 0);
        std::shuffle(keys.begin(), keys.end(), rng);

        std::vector<Key> probes(N);
        std::uniform_int_distribution<Key> any(0, 2 * N - 1);   // 절반은 없는 key
        for (Key& k : probes) k = any(rng);

        std::printf("N=%lld inserts + %lld finds, MAX_KEYS=%zu\n\n",
                    static_cast<long long>(N), static_cast<long long>(N), MAX_KEYS);
        std::printf("%-6s %10s %12s %12s %9s %9s\n", "pool", "logical", "disk_reads", "disk_writes", "hit_rate", "time(s)");

        Result base;
        if (with_baseline) {
            base = run_direct(keys, probes);
            print_row("none", base);
        }

        for (size_t pages : sizes) {
            Result r = run_pool(pages, keys, probes);
            if (with_baseline && r.logical != base.logical) {
                throw std::runtime_error("pool " + std::to_string(pages) + ": logical accesses "
                    + std::to_string(r.logical) + " != direct " + std::to_string(base.logical));
            }
            print_row(std::to_string(pages), r);
        }
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
        std::remove(DB_PATH);
        return 1;
    }
    std::remove(DB_PATH);
    return 0;
}