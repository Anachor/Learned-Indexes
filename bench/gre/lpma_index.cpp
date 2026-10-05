// The learned PMA as a GRE index: see lpma_index.h. Compiled as C++20.

#include "lpma_index.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "../../src/Hull/by_name.hpp"
#include "../../src/LPMA/learned_pma.hpp"

namespace {

// GRE's keys are unsigned; flipping the top bit keeps their order as int64_t.
constexpr uint64_t TOP_BIT = uint64_t(1) << 63;
lpma::Key to_key(uint64_t x) { return lpma::Key(x ^ TOP_BIT); }
uint64_t from_key(lpma::Key x) { return uint64_t(x) ^ TOP_BIT; }

template <class H>
class LpmaIndex final : public indexInterface<uint64_t, uint64_t> {
public:
    explicit LpmaIndex(int64_t k) : index_(k) {}

    void init(Param *) override {}

    // GRE hands the keys over sorted. There is no bulk load yet, so they go in
    // one by one.
    void bulk_load(std::pair<uint64_t, uint64_t> *key_value, size_t num, Param *param) override {
        if (param && param->worker_num > 1) {
            std::fprintf(stderr, "lpma is single-threaded: use --thread_num=1\n");
            std::exit(1);
        }
        for (size_t i = 0; i < num; ++i) index_.insert(to_key(key_value[i].first));
    }

    bool get(uint64_t key, uint64_t &, Param *) override { return index_.contains(to_key(key)); }
    bool put(uint64_t key, uint64_t, Param *) override { return index_.insert(to_key(key)); }
    bool update(uint64_t, uint64_t, Param *) override { return false; }
    bool remove(uint64_t, Param *) override { return false; }

    // Up to key_num keys from the smallest >= key_low_bound on.
    size_t scan(uint64_t key_low_bound, size_t key_num, std::pair<uint64_t, uint64_t> *result, Param *) override {
        auto slot = index_.lower_bound_slot(to_key(key_low_bound));
        if (!slot) return 0;
        const auto &pma = index_.pma();
        size_t n = 0;
        for (size_t s = *slot; s < pma.capacity() && n < key_num; ++s) {
            if (pma.occupied(s)) result[n++] = {from_key(pma.key_at(s)), 0};
        }
        return n;
    }

    // The PMA's arrays (a key and a used flag per slot, a count per leaf), the
    // segment map's nodes, and the hulls: VectorHull's chains, TreeHull's nodes
    // and leaves (not the free ones its pools keep for reuse).
    long long memory_consumption() override {
        const auto &pma = index_.pma();
        size_t bytes = pma.capacity() * (sizeof(int64_t) + sizeof(uint8_t)) +
                       pma.capacity() / pma.leaf_size() * sizeof(size_t);
        for (const auto &entry : index_.segments()) {
            bytes += sizeof(entry) + 4 * sizeof(void *);  // a red-black tree node: colour, parent, two children
            if constexpr (std::is_same_v<H, lpma::VectorHull>)
                bytes += (entry.second.hull.upper.capacity() + entry.second.hull.lower.capacity()) * sizeof(lpma::Pt);
            if constexpr (requires { entry.second.hull.bytes(); }) bytes += entry.second.hull.bytes();
        }
        return (long long)bytes;
    }

private:
    lpma::LearnedPMA<H> index_;
};

}  // namespace

indexInterface<uint64_t, uint64_t> *make_lpma_index(const std::string &name) {
    std::string hull = "tree", delta = "32", leaf;
    std::stringstream parts(name);
    std::string part;
    if (!std::getline(parts, part, '-') || part != "lpma") return nullptr;
    while (std::getline(parts, part, '-')) {
        if (part.rfind("delta", 0) == 0) delta = part.substr(5);
        else if (part.rfind("leaf", 0) == 0) leaf = part.substr(4);
        else if (part == "scan" || part == "vector" || part == "tree") hull = part;
        else return nullptr;
    }
    if (!leaf.empty()) {
        if (hull != "tree") return nullptr;
        hull += leaf;  // tree16: by_name.hpp's name for 16-point leaves
    }
    char *end = nullptr;
    double d = std::strtod(delta.c_str(), &end);
    if (delta.empty() || *end || !(d >= 0) || d > double(lpma::MAX_K / 2) || 2 * d != std::floor(2 * d)) return nullptr;
    int64_t k = int64_t(2 * d);
    indexInterface<uint64_t, uint64_t> *index = nullptr;
    lpma::with_hull(hull, [&](auto h) { index = new LpmaIndex<typename decltype(h)::type>(k); });
    return index;
}
