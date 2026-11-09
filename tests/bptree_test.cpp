#include "bptree.h"
#include "buffer_pool.h"
#include "lru.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static const char* DB_PATH = "bptree_test.db";
constexpr size_t CHECK_EVERY = 50;
constexpr size_t POOL_PAGES = 8;

static void require(bool cond, const std::string& msg) {
    if (!cond) throw std::runtime_error(msg);
}

static void insert_all(BPlusTree& tree, std::map<Key, Value>& ref, const std::vector<Key>& keys,
                       size_t value_base, const std::string& name) {
    for (size_t i = 0; i < keys.size(); ++i) {
        Key k = keys[i];
        Value v = static_cast<Value>(value_base + i);
        bool inserted = tree.insert(k, v);
        bool expected = ref.insert({k, v}).second;
        require(inserted == expected, name + ": insert(" + std::to_string(k) + ") returned wrong result");
        if ((i + 1) % CHECK_EVERY == 0) tree.check();
    }
}

static void verify(const BPlusTree& tree, const std::map<Key, Value>& ref,
                   std::mt19937& rng, const std::string& name) {
    tree.check();
    require(tree.size() == ref.size(), name + ": size " + std::to_string(tree.size())
            + ", expected " + std::to_string(ref.size()));

    for (const auto& [k, v] : ref) {
        auto got = tree.find(k);
        require(got.has_value() && *got == v, name + ": find(" + std::to_string(k) + ") wrong");
    }

    Key lo_bound = ref.empty() ? 0 : ref.begin()->first - 10;
    Key hi_bound = ref.empty() ? 0 : ref.rbegin()->first + 10;
    std::uniform_int_distribution<Key> dist(lo_bound, hi_bound);

    for (int r = 0; r < 2000; ++r) {
        Key k = dist(rng);
        auto got = tree.find(k);
        auto it = ref.find(k);
        require(got.has_value() == (it != ref.end()), name + ": find(" + std::to_string(k) + ") presence mismatch");
    }

    for (int r = 0; r < 2000; ++r) {
        Key lo = dist(rng), hi = dist(rng);
        std::vector<std::pair<Key, Value>> expected;
        for (auto it = ref.lower_bound(lo); it != ref.end() && it->first <= hi; ++it) {
            expected.emplace_back(*it);
        }
        require(tree.range(lo, hi) == expected,
                name + ": range(" + std::to_string(lo) + ", " + std::to_string(hi) + ") mismatch");
    }
}

static void run(const std::string& name, const std::vector<Key>& keys, std::mt19937& rng) {
    std::remove(DB_PATH);
    std::map<Key, Value> ref;

    {
        Pager pager(DB_PATH);
        BufferPool pool(pager, std::make_unique<LRU>(POOL_PAGES));
        BPlusTree tree(pool);
        insert_all(tree, ref, keys, 0, name);
        verify(tree, ref, rng, name);
    }
    {
        Pager pager(DB_PATH);
        BufferPool pool(pager, std::make_unique<LRU>(POOL_PAGES));
        BPlusTree tree(pool);
        verify(tree, ref, rng, name + " (reopened)");

        std::vector<Key> more(200);
        std::uniform_int_distribution<Key> d(-1000, 3000);
        for (Key& k : more) k = d(rng);
        insert_all(tree, ref, more, keys.size(), name + " (insert after reopen)");
        verify(tree, ref, rng, name + " (insert after reopen)");
    }
    {
        Pager pager(DB_PATH);
        BufferPool pool(pager, std::make_unique<LRU>(POOL_PAGES));
        BPlusTree tree(pool);
        verify(tree, ref, rng, name + " (reopened twice)");
    }

    std::printf("OK   %-11s size=%zu\n", name.c_str(), ref.size());
}

static void test_bad_magic() {
    std::remove(DB_PATH);
    {
        Pager pager(DB_PATH);
        pager.allocate();
    }
    Pager pager(DB_PATH);
    BufferPool pool(pager, std::make_unique<LRU>(POOL_PAGES));
    bool threw = false;
    try {
        BPlusTree tree(pool);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "bad magic: opened a file without magic");
    std::printf("OK   bad magic\n");
}

int main() {
    try {
        std::mt19937 rng(12345);
        const Key N = 2000;

        std::vector<Key> asc(N);
        std::iota(asc.begin(), asc.end(), 0);
        std::vector<Key> desc(asc.rbegin(), asc.rend());
        std::vector<Key> shuffled = asc;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        std::vector<Key> dup(N * 2);
        std::uniform_int_distribution<Key> narrow(0, N / 2 - 1);
        for (Key& k : dup) k = narrow(rng);

        run("empty", {}, rng);
        run("ascending", asc, rng);
        run("descending", desc, rng);
        run("shuffled", shuffled, rng);
        run("duplicates", dup, rng);
        test_bad_magic();
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
        std::remove(DB_PATH);
        return 1;
    }
    std::remove(DB_PATH);
    return 0;
}