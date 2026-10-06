#pragma once

// What the GPLA's tests share: brute force, random inputs, and the
// comparison of the three hulls' segments. SmallTreeHull, T2 with leaves of two
// points, splits and merges leaves at almost every step.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../src/Hull/scan_hull.hpp"
#include "../src/Hull/tree_hull.hpp"
#include "../src/Hull/vector_hull.hpp"
#include "../src/GPLA/gpla.hpp"
#include "../src/ORourke/brute_orourke.hpp"

using gpla::ExactLine;
using gpla::Fitter;
using gpla::i128;
using gpla::Key;
using gpla::PointView;
using gpla::Pt;
using gpla::ScanHull;
using gpla::SlotRange;
using gpla::BasicTreeHull;
using gpla::TreeHull;
using gpla::VectorHull;

constexpr Key KEY_MIN = std::numeric_limits<Key>::min(), KEY_MAX = std::numeric_limits<Key>::max();

// The bounds tried: delta = 0 to 512, and the largest allowed.
const std::vector<int64_t> KS = {0, 1, 2, 3, 4, 8, 16, 64, 1024, gpla::MAX_K};

using SmallTreeHull = BasicTreeHull<2>;

using SparsePMA = PMA<PMAParams{.leaf_upper = 0.9, .root_upper = 0.5, .initial_capacity = 2}>;

// -i iterations, -n the most points or inserts per case, and a seed.
struct Args {
    int iterations = 200;
    size_t max_n = 150;
    uint64_t seed = 1;
};

inline Args parse_args(int argc, char **argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "-i" && i + 1 < argc) a.iterations = std::atoi(argv[++i]);
        else if (s == "-n" && i + 1 < argc) a.max_n = std::max<size_t>(1, std::strtoull(argv[++i], nullptr, 10));
        else a.seed = std::strtoull(s.c_str(), nullptr, 10);
    }
    return a;
}

// Runs check(), which returns what is wrong or nullptr; an exception counts as
// wrong. Prints it with where; 1 if wrong.
template <class F>
int failed(const std::string &where, F &&check) {
    std::string problem;
    try {
        if (const char *p = check()) problem = p;
    } catch (const std::exception &e) {
        problem = std::string("exception: ") + e.what();
    }
    if (problem.empty()) return 0;
    std::cerr << where << ": " << problem << "\n";
    return 1;
}

// Does one line fit the points? By BruteORourke, independent of gpla.
inline bool brute_fits(const std::vector<Pt> &points, int64_t k) {
    BruteORourke<int64_t> brute(k);
    for (const Pt &p : points) {
        if (brute.add_point(p.x, p.y)) return false;  // it had to start a new segment
    }
    return true;
}

// Where BruteORourke's greedy segments start.
inline std::vector<size_t> brute_starts(const std::vector<Pt> &points, int64_t k) {
    BruteORourke<int64_t> brute(k);
    std::vector<size_t> starts;
    for (size_t i = 0; i < points.size(); ++i) {
        if (brute.add_point(points[i].x, points[i].y) || i == 0) starts.push_back(i);
    }
    return starts;
}

inline std::vector<Pt> points_of(const PointView &pts, SlotRange r) {
    std::vector<Pt> out;
    pts.all_of(r, [&](Pt p) {
        out.push_back(p);
        return true;
    });
    return out;
}

// n points with increasing keys and y = 2 * slot: keys dense, gapped or
// anywhere in int64; slots ranks, PMA-like, flat, unordered or huge.
inline std::vector<Pt> random_points(std::mt19937_64 &rng, size_t n) {
    std::vector<Key> xs;
    if (rng() % 3 == 0) {
        Key x = Key(rng() % 2000001) - 1000000;
        for (size_t i = 0; i < n; ++i) xs.push_back(x + Key(i));
    } else if (rng() % 2) {
        Key x = Key(rng() % 1000);
        for (size_t i = 0; i < n; ++i) xs.push_back(x += Key(1 + rng() % 100));
    } else {  // the ends of int64 sometimes included
        std::set<Key> s;
        if (rng() % 2) s.insert(KEY_MIN);
        if (rng() % 2) s.insert(KEY_MAX);
        while (s.size() < n) s.insert(Key(rng()));
        xs.assign(s.begin(), s.end());
        xs.resize(n);
    }
    std::vector<Pt> points(n);
    int64_t slot = 0, shape = int64_t(rng() % 5);
    for (size_t i = 0; i < n; ++i) {
        if (shape == 0) slot = int64_t(i);                                          // ranks
        else if (shape == 1) slot += i ? int64_t(1 + rng() % 4) : 0;               // a PMA's slots
        else if (shape == 2) slot += int64_t(rng() % 2);                           // flat
        else if (shape == 3) slot = int64_t(rng() % (4 * n + 1));                 // unordered
        else slot += int64_t(rng() % (uint64_t(1) << 49));                         // huge
        points[i] = {xs[i], 2 * slot};
    }
    return points;
}

// n keys, duplicates possible: random, ascending, descending, clustered,
// anywhere in int64, about evenly spaced, or on two scales.
inline std::pair<std::vector<Key>, std::string> random_keys(std::mt19937_64 &rng, size_t n) {
    std::vector<Key> keys(n);
    Key range = Key(rng() % 2 ? n : 4 * n + 1);
    for (Key &k : keys) k = Key(rng() % uint64_t(range));
    switch (rng() % 7) {
        case 0: return {keys, "random"};
        case 1: std::sort(keys.begin(), keys.end()); return {keys, "ascending"};
        case 2: std::sort(keys.rbegin(), keys.rend()); return {keys, "descending"};
        case 3:
            for (Key &k : keys) k = Key(rng() % 4) * range + Key(rng() % 8);
            return {keys, "clustered"};
        case 4:
            for (Key &k : keys) {
                uint64_t r = rng() % 8;
                k = r == 0 ? KEY_MIN + Key(rng() % 3) : r == 1 ? KEY_MAX - Key(rng() % 3) : Key(rng());
            }
            return {keys, "full range"};
        case 5:
            for (size_t i = 0; i < n; ++i) keys[i] = Key(i) * 1000 + Key(rng() % 3);
            std::shuffle(keys.begin(), keys.end(), rng);
            return {keys, "even"};
        default:
            for (Key &k : keys) k = rng() % 2 ? Key(rng() % 64) : Key(rng() % (uint64_t(1) << 40));
            return {keys, "two scales"};
    }
}

// The three hulls' segments are the same structure: the same keys and slots,
// the same line, and T2's hull (any leaf size) is T1's.
template <class T2>
const char *structure_problem(const gpla::Segment<ScanHull> &s0, const gpla::Segment<VectorHull> &s1,
                              const gpla::Segment<T2> &s2) {
    if (s0.first != s1.first || s0.first != s2.first || s0.last != s1.last || s0.last != s2.last ||
        s0.slots != s1.slots || s0.slots != s2.slots)
        return "the hulls give different segments";
    if (!s0.line.same(s1.line) || !s0.line.same(s2.line)) return "the hulls give different lines";
    if (s1.hull.upper != s2.hull.chain(0) || s1.hull.lower != s2.hull.chain(1)) return "T2's hull differs from T1's";
    return nullptr;
}
