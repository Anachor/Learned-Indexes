// Insert and lookup time of the GPLA with each hull: T0 (scan),
// T1 (vector), T2 (tree, or tree1 ... tree128 for other leaf sizes:
// src/Hull/by_name.hpp), and a lookup's key comparisons against
// qc = log2(delta) + log2(lambda).
//
// For each n, order and delta, each hull inserts the keys 2, 4, ..., 2n in the
// order's sequence, timing every insert, then looks up q random keys (half of
// them absent, the odd ones). Every run is repeated R times, the hulls taking
// turns; the median of each figure is printed. The hulls must end with the
// same number of segments.
//
// The comparisons are counted on one more, untimed, index with the first hull
// (the hulls build the same segments, so they all compare alike): the lookups
// again, split into finding the segment (the segment map, ~log2 lambda) and the
// rest (one check against the segment's last key, then the binary search over
// the line's 2*delta + 1 slots, ~log2 delta + 1). The PMA's own search, used by
// inserts, is not counted.
//
//   g++-11 -std=c++20 -O2 tests/gpla_speed.cpp -o tests/gpla_speed
//   ./tests/gpla_speed [-n N,N,...] [--orders O,...] [--deltas D,...] [--hulls H,...] [-q Q] [-r R] [--csv FILE] [seed]
//
// Orders: sorted, reverse, or an experiments --permutation (uniform, zipf:16,1, ...).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../experiments/common/permutation.hpp"
#include "../experiments/common/segmentation.hpp"
#include "../src/Hull/by_name.hpp"
#include "../src/GPLA/gpla.hpp"

using Clock = std::chrono::steady_clock;
using gpla::Key;

volatile uint64_t sink;  // keeps the lookups from being optimised away

struct Result {
    size_t segments = 0;
    double mean_us = 0, p99_us = 0, max_us = 0, lookup_ns = 0;  // per insert, per lookup
};

template <class H>
Result run(const std::vector<Key> &keys, int64_t k, const std::vector<Key> &lookups) {
    gpla::GPLA<H> index(k);
    std::vector<double> us;
    for (Key key : keys) {
        auto start = Clock::now();
        index.insert(key);
        us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    uint64_t sum = 0;
    auto start = Clock::now();
    for (Key x : lookups) sum += index.lower_bound(x).value_or(1);
    double ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    sink = sink + sum;

    Result r;
    r.segments = index.segment_count();
    for (double t : us) r.mean_us += t / double(us.size());
    std::sort(us.begin(), us.end());
    r.p99_us = us[us.size() * 99 / 100];
    r.max_us = us.back();
    r.lookup_ns = ns / double(lookups.size());
    return r;
}

Result run(const std::string &hull, const std::vector<Key> &keys, int64_t k, const std::vector<Key> &lookups) {
    Result r;
    if (!gpla::with_hull(hull, [&](auto h) { r = run<typename decltype(h)::type>(keys, k, lookups); })) {
        std::cerr << "unknown hull " << hull << " (" << gpla::HULL_NAMES << ")\n";
        std::exit(2);
    }
    return r;
}

// std::less that counts its calls.
struct CountingLess {
    static inline uint64_t count = 0;
    bool operator()(Key a, Key b) const {
        ++count;
        return a < b;
    }
};

struct Comparisons {
    double segment = 0, rest = 0, mean = 0;  // per lookup
    uint64_t max = 0;
};

template <class H>
Comparisons count_comparisons(const std::vector<Key> &keys, int64_t k, const std::vector<Key> &lookups) {
    gpla::GPLA<H, PMA<>, CountingLess> index(k);
    for (Key key : keys) index.insert(key);
    uint64_t segment = 0, total = 0, max = 0;
    for (Key x : lookups) {
        uint64_t before = CountingLess::count;
        index.segments().upper_bound(x);  // what the lookup does first
        segment += CountingLess::count - before;
        before = CountingLess::count;
        sink = sink + index.lower_bound(x).value_or(1);
        total += CountingLess::count - before;
        max = std::max(max, CountingLess::count - before);
    }
    double q = double(lookups.size());
    return {double(segment) / q, double(total - segment) / q, double(total) / q, max};
}

// The run()s have already turned away an unknown hull.
Comparisons count_comparisons(const std::string &hull, const std::vector<Key> &keys, int64_t k,
                              const std::vector<Key> &lookups) {
    Comparisons c;
    gpla::with_hull(hull, [&](auto h) { c = count_comparisons<typename decltype(h)::type>(keys, k, lookups); });
    return c;
}

// Each figure's median over the repeats.
Result median(std::vector<Result> rs) {
    Result m = rs[0];
    for (double Result::*field : {&Result::mean_us, &Result::p99_us, &Result::max_us, &Result::lookup_ns}) {
        std::sort(rs.begin(), rs.end(), [&](const Result &a, const Result &b) { return a.*field < b.*field; });
        m.*field = rs[rs.size() / 2].*field;
    }
    return m;
}

// Splits at commas, keeping zipf:16,1 whole.
std::vector<std::string> split_list(const std::string &s) {
    std::vector<std::string> list;
    std::string piece;
    std::stringstream stream(s);
    while (std::getline(stream, piece, ',')) {
        if (!list.empty() && list.back().find(':') != std::string::npos && std::isdigit((unsigned char)piece[0]))
            list.back() += "," + piece;
        else list.push_back(piece);
    }
    return list;
}

// The keys 2, 4, ..., 2n in the order's sequence.
std::vector<Key> order_keys(const std::string &order, size_t n, uint64_t seed) {
    std::vector<Key> keys;
    if (order == "sorted" || order == "reverse") {
        for (size_t i = 1; i <= n; ++i) keys.push_back(Key(i));
        if (order == "reverse") std::reverse(keys.begin(), keys.end());
    } else if (parse_order(order, ORDER) && order_fits({n})) {
        keys = make_permutation(n, seed);
    } else {
        std::cerr << "unknown order " << order << "\n";
        std::exit(2);
    }
    for (Key &key : keys) key *= 2;
    return keys;
}

int main(int argc, char **argv) {
    std::string sizes = "1024,4096,16384,65536", orders = "uniform,zipf:16,1", deltas = "0.5,8,128,1024";
    std::string hulls = "scan,vector,tree", csv_path;
    size_t queries = 1000000;
    int repeats = 3;
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-n" && i + 1 < argc) sizes = argv[++i];
        else if (a == "--orders" && i + 1 < argc) orders = argv[++i];
        else if (a == "--deltas" && i + 1 < argc) deltas = argv[++i];
        else if (a == "--hulls" && i + 1 < argc) hulls = argv[++i];
        else if (a == "-q" && i + 1 < argc) queries = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "-r" && i + 1 < argc) repeats = std::max(1, std::atoi(argv[++i]));
        else if (a == "--csv" && i + 1 < argc) csv_path = argv[++i];
        else seed = std::strtoull(a.c_str(), nullptr, 10);
    }
    FILE *csv = csv_path.empty() ? nullptr : std::fopen(csv_path.c_str(), "w");
    if (csv)
        std::fprintf(csv, "n,order,delta,hull,segments,insert_mean_us,insert_p99_us,insert_max_us,lookup_ns,qc,"
                          "cmp_segment,cmp_rest,cmp_mean,cmp_max\n");

    // Warm up, so the first runs are not timed at a low clock speed.
    for (auto start = Clock::now(); Clock::now() - start < std::chrono::milliseconds(500);) sink = sink + 1;

    std::printf("%8s %-11s %6s %-7s %8s %14s %14s %14s %11s %6s %7s %7s %7s %7s\n", "n", "order", "delta", "hull",
                "segments", "insert us mean", "insert us p99", "insert us max", "lookup ns", "qc", "cmp seg", "cmp rest",
                "cmp", "cmp max");
    int mismatches = 0;
    for (const std::string &size : split_list(sizes)) {
        size_t n = std::strtoull(size.c_str(), nullptr, 10);
        for (const std::string &order : split_list(orders)) {
            std::vector<Key> keys = order_keys(order, n, seed + n);
            std::mt19937_64 rng(seed + n);
            std::vector<Key> lookups(queries);
            for (Key &x : lookups) x = Key(1 + rng() % (2 * n + 1));

            for (const std::string &delta : split_list(deltas)) {
                int64_t k = int64_t(2 * std::strtod(delta.c_str(), nullptr));
                std::vector<std::string> names = split_list(hulls);
                std::vector<std::vector<Result>> results(names.size());
                for (int rep = 0; rep < repeats; ++rep) {
                    for (size_t h = 0; h < names.size(); ++h) results[h].push_back(run(names[h], keys, k, lookups));
                }
                Comparisons c = count_comparisons(names[0], keys, k, lookups);
                for (size_t h = 0; h < names.size(); ++h) {
                    Result r = median(results[h]);
                    if (r.segments != results[0][0].segments) ++mismatches;
                    double qc = query_complexity(r.segments, k);
                    std::printf("%8zu %-11s %6s %-7s %8zu %14.2f %14.2f %14.1f %11.1f %6.2f %7.2f %7.2f %7.2f %7llu\n", n,
                                order.c_str(), delta.c_str(), names[h].c_str(), r.segments, r.mean_us, r.p99_us,
                                r.max_us, r.lookup_ns, qc, c.segment, c.rest, c.mean, (unsigned long long)c.max);
                    std::fflush(stdout);
                    if (csv) {
                        std::fprintf(csv, "%zu,\"%s\",%s,%s,%zu,%.4f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.4f,%llu\n", n,
                                     order.c_str(), delta.c_str(), names[h].c_str(), r.segments, r.mean_us, r.p99_us,
                                     r.max_us, r.lookup_ns, qc, c.segment, c.rest, c.mean, (unsigned long long)c.max);
                        std::fflush(csv);
                    }
                }
            }
        }
    }
    if (csv) std::fclose(csv);
    if (mismatches) std::cerr << mismatches << " runs where the hulls ended with different segments\n";
    return mismatches ? 1 : 0;
}
