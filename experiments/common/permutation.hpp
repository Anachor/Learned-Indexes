#pragma once

// Insertion orders of {1..n}, shared by the experiments.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// The insertion order of {1..n}, chosen with --permutation:
//
//   uniform      a uniformly random permutation (the default)
//   zipf:R,s     the keys split into R equal regions; each insert picks a region
//                with weight 1/rank^s among those not yet full, then a random
//                key in it. Region ranks are shuffled, so the hot regions sit
//                anywhere. Prefixes mix dense and sparse regions.
//   zipf:R,s,d   recursive: the same pick repeated d levels down - a region,
//                then one of its R subregions, and so on - with the ranks
//                shuffled independently in every region, so hot and cold areas
//                nest at every scale. zipf:R,s is d = 1.
//   blocks:b     the keys in blocks of b consecutive keys; the blocks in random
//                order, the keys in each block in random order
//   probing      linear probing: pick a random key, and if it is already in,
//                take the next one up, wrapping from n to 1
//   bitrev:p     almost sorted, then bits reversed: the positions 0..n-1 in
//                order, p*n swaps of two random positions (default 0), then
//                each position's log2(n) bits reversed. With no swaps, the van
//                der Corput order: every prefix spread evenly over the keys;
//                more swaps move it toward uniform. Needs n a power of two.
//
// Uniform prefixes are random subsets, whose best fit is almost always a
// single segment; the others vary the key density along the prefix.
struct Order {
    std::string kind = "uniform";
    size_t regions = 0;  // zipf R
    double skew = 0;     // zipf s
    size_t depth = 1;    // zipf d
    size_t block = 0;    // blocks b
    double swaps = 0;    // bitrev p: swaps per key

    // The spec as given back to --permutation: "zipf:16,1", "zipf:4,1,3".
    std::string spec() const {
        char buffer[64];
        if (kind == "zipf" && depth == 1) std::snprintf(buffer, sizeof buffer, "zipf:%zu,%g", regions, skew);
        else if (kind == "zipf") std::snprintf(buffer, sizeof buffer, "zipf:%zu,%g,%zu", regions, skew, depth);
        else if (kind == "blocks") std::snprintf(buffer, sizeof buffer, "blocks:%zu", block);
        else if (kind == "bitrev" && swaps == 0) return kind;
        else if (kind == "bitrev") std::snprintf(buffer, sizeof buffer, "bitrev:%g", swaps);
        else return kind;
        return buffer;
    }
};

// Parsed from --permutation in main, before anything draws a permutation.
inline Order ORDER;

// Parses a --permutation spec; false if it is not one.
inline bool parse_order(const std::string &s, Order &order) {
    size_t colon = s.find(':');
    std::string kind = s.substr(0, colon), args = colon == std::string::npos ? "" : s.substr(colon + 1);
    order = Order();
    order.kind = kind;
    try {
        if (kind == "uniform" || kind == "probing") return args.empty();
        if (kind == "blocks") {
            size_t used;
            long long b = std::stoll(args, &used);
            if (used != args.size() || b < 1) return false;
            order.block = size_t(b);
            return true;
        }
        if (kind == "bitrev") {
            if (args.empty()) return colon == std::string::npos;  // "bitrev", not "bitrev:"
            size_t used;
            double swaps = std::stod(args, &used);
            if (used != args.size() || !(swaps >= 0)) return false;
            order.swaps = swaps;
            return true;
        }
        if (kind == "zipf") {
            std::vector<std::string> parts;
            std::stringstream stream(args);
            for (std::string part; std::getline(stream, part, ',');) parts.push_back(part);
            // getline drops a trailing empty piece, so count the commas too
            size_t commas = size_t(std::count(args.begin(), args.end(), ','));
            if ((parts.size() != 2 && parts.size() != 3) || commas != parts.size() - 1) return false;
            size_t used_r, used_s, used_d = 0;
            long long regions = std::stoll(parts[0], &used_r);
            double skew = std::stod(parts[1], &used_s);
            long long depth = parts.size() == 3 ? std::stoll(parts[2], &used_d) : 1;
            if (used_r != parts[0].size() || used_s != parts[1].size() || regions < 1 || !(skew >= 0)) return false;
            if (parts.size() == 3 && (used_d != parts[2].size() || depth < 1)) return false;
            order.regions = size_t(regions);
            order.skew = skew;
            order.depth = size_t(depth);
            return true;
        }
    } catch (const std::exception &) {
    }
    return false;
}

// The insertion order of {1..n} for this seed, under ORDER. Every experiment and
// mode draws it from here, so the same seed and order give the same keys
// everywhere (exp2 sees exactly exp1's prefixes). The uniform order is the same shuffle as before the other orders
// existed, so earlier runs reproduce.
inline std::vector<int64_t> make_permutation(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<int64_t> permutation;
    permutation.reserve(n);

    if (ORDER.kind == "zipf" && ORDER.depth > 1) {
        // A tree of regions: each splits its keys (lo, hi] into R equal parts,
        // d levels down; a region of at most one key is not split further.
        // Every region shuffles its own children's ranks. An insert walks from
        // the root, picking at each level among the children with keys left,
        // with weight 1/rank^s, and takes a random key from the leaf it ends in.
        size_t R = ORDER.regions;
        std::vector<double> weight(R);
        for (size_t i = 0; i < R; ++i) weight[i] = 1 / std::pow(double(i + 1), ORDER.skew);

        struct Region {
            size_t left;                  // keys not yet inserted
            std::vector<size_t> by_rank;  // children, hottest first; empty for a leaf
            std::vector<int64_t> keys;    // a leaf's keys, shuffled
        };
        std::vector<Region> tree;
        std::function<size_t(size_t, size_t, size_t)> build = [&](size_t lo, size_t hi, size_t level) {
            size_t index = tree.size();
            tree.push_back({hi - lo, {}, {}});
            if (level == ORDER.depth || hi - lo <= 1) {
                std::vector<int64_t> keys;
                for (size_t key = lo + 1; key <= hi; ++key) keys.push_back(int64_t(key));
                std::shuffle(keys.begin(), keys.end(), rng);
                tree[index].keys = std::move(keys);
                return index;
            }
            std::vector<size_t> children;
            for (size_t g = 0; g < R; ++g) {
                children.push_back(build(lo + g * (hi - lo) / R, lo + (g + 1) * (hi - lo) / R, level + 1));
            }
            std::shuffle(children.begin(), children.end(), rng);
            tree[index].by_rank = std::move(children);
            return index;
        };
        size_t root = build(0, n, 0);

        std::vector<double> live(R);
        while (permutation.size() < n) {
            size_t at = root;
            while (!tree[at].by_rank.empty()) {
                --tree[at].left;
                const std::vector<size_t> &children = tree[at].by_rank;
                for (size_t i = 0; i < R; ++i) live[i] = tree[children[i]].left ? weight[i] : 0;
                std::discrete_distribution<size_t> pick(live.begin(), live.end());
                at = children[pick(rng)];
            }
            --tree[at].left;
            permutation.push_back(tree[at].keys.back());
            tree[at].keys.pop_back();
        }
    } else if (ORDER.kind == "zipf") {
        // Region g holds keys (g*n/R, (g+1)*n/R], each region's keys pre-shuffled
        // so taking from the back is a random pick. With R > n some regions are
        // empty and never picked.
        size_t R = ORDER.regions;
        std::vector<std::vector<int64_t>> regions(R);
        for (size_t g = 0; g < R; ++g) {
            for (size_t key = g * n / R + 1; key <= (g + 1) * n / R; ++key) regions[g].push_back(int64_t(key));
            std::shuffle(regions[g].begin(), regions[g].end(), rng);
        }
        std::vector<size_t> by_rank(R);  // by_rank[i]: the region with weight 1/(i+1)^s
        std::iota(by_rank.begin(), by_rank.end(), 0);
        std::shuffle(by_rank.begin(), by_rank.end(), rng);

        std::vector<double> weight(R);
        for (size_t i = 0; i < R; ++i) weight[i] = 1 / std::pow(double(i + 1), ORDER.skew);
        std::vector<double> live(R);
        while (permutation.size() < n) {
            for (size_t i = 0; i < R; ++i) live[i] = regions[by_rank[i]].empty() ? 0 : weight[i];
            std::discrete_distribution<size_t> pick(live.begin(), live.end());
            std::vector<int64_t> &region = regions[by_rank[pick(rng)]];
            permutation.push_back(region.back());
            region.pop_back();
        }
    } else if (ORDER.kind == "blocks") {
        size_t b = ORDER.block, count = (n + b - 1) / b;
        std::vector<size_t> blocks(count);
        std::iota(blocks.begin(), blocks.end(), 0);
        std::shuffle(blocks.begin(), blocks.end(), rng);
        for (size_t block : blocks) {
            size_t first = permutation.size();
            for (size_t key = block * b + 1; key <= std::min(n, (block + 1) * b); ++key) {
                permutation.push_back(int64_t(key));
            }
            std::shuffle(permutation.begin() + first, permutation.end(), rng);
        }
    } else if (ORDER.kind == "bitrev") {
        // Reversing log2(n) bits permutes 0..n-1 only when n is a power of two.
        if ((n & (n - 1)) != 0) {
            std::cerr << "--permutation " << ORDER.spec() << " needs n a power of two, not n = " << n << "\n";
            std::exit(2);
        }
        size_t bits = 0;
        while ((size_t(1) << bits) < n) ++bits;
        std::vector<size_t> positions(n);
        std::iota(positions.begin(), positions.end(), 0);
        if (ORDER.swaps > 0) {
            std::uniform_int_distribution<size_t> any(0, n - 1);
            size_t count = size_t(std::llround(ORDER.swaps * double(n)));
            for (size_t i = 0; i < count; ++i) {
                size_t a = any(rng), b = any(rng);
                std::swap(positions[a], positions[b]);
            }
        }
        for (size_t position : positions) {
            size_t reversed = 0;
            for (size_t b = 0; b < bits; ++b) reversed |= ((position >> b) & 1) << (bits - 1 - b);
            permutation.push_back(int64_t(reversed + 1));
        }
    } else if (ORDER.kind == "probing") {
        // next[i]: the smallest free key >= i, via path-compressed pointers, so
        // a probe costs near O(1) however long the runs grow. Index n + 1 means
        // "past the end", from where the probe wraps to 1.
        std::vector<size_t> next(n + 2);
        std::iota(next.begin(), next.end(), 0);
        auto free_from = [&](size_t i) {
            size_t root = i;
            while (next[root] != root) root = next[root];
            while (next[i] != root) {
                size_t up = next[i];
                next[i] = root;
                i = up;
            }
            return root;
        };
        std::uniform_int_distribution<size_t> pick(1, n);
        for (size_t inserted = 0; inserted < n; ++inserted) {
            size_t key = free_from(pick(rng));
            if (key == n + 1) key = free_from(1);
            permutation.push_back(int64_t(key));
            next[key] = key + 1;
        }
    } else {
        permutation.resize(n);
        std::iota(permutation.begin(), permutation.end(), 1);
        std::shuffle(permutation.begin(), permutation.end(), rng);
    }
    return permutation;
}

// bitrev only permutes 1..n for n a power of two. Checked for every n before a
// run folder is made, not part-way through the run; false (with a message) if
// one does not fit.
inline bool order_fits(const std::vector<size_t> &sizes) {
    if (ORDER.kind != "bitrev") return true;
    for (size_t n : sizes) {
        if ((n & (n - 1)) != 0) {
            std::cerr << "--permutation " << ORDER.spec() << " needs every n a power of two; n = " << n
                      << " is not\n";
            return false;
        }
    }
    return true;
}
