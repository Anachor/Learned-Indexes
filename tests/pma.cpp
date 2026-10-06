// PMA insertion workload, and its stress test.
//
// Workload (the default): for each order, inserts n keys (default 2^20) and
// writes <order>.csv to a new run folder, results/pma/<run> (run = 1, 2, ...,
// the next unused number) - a row at about 64 sizes per doubling, plus around
// every grow: the size, the capacity, the density, and the cost so far, keys
// moved and time per insert - and meta.json. Plot with tests/plot_pma.py.
//
//   ./tests/pma [-n N] [--orders O,O,...] [--out DIR] [seed]
//
// O is sorted, reverse, or any --permutation of the experiments (uniform,
// zipf:16,1, ...); the default is uniform,zipf:16,1,sorted,reverse. --out is
// the folder holding the runs (default results/pma).
//
// Stress test: random insert sequences, checked after every insert against a
// std::set (the same keys, in order) and the PMA's own invariants. Writes
// nothing. -n is the largest case (default 200).
//
//   ./tests/pma --stress [-i iterations] [-n MAXN] [seed]
//
//   g++-11 -std=c++20 -O3 tests/pma.cpp -o tests/pma
//
// The PMA's parameters are compile time, so inserts stay as fast as with the
// defaults: pick them with -DPMA_LEAF_UPPER=0.9 -DPMA_ROOT_UPPER=0.5
// -DPMA_INITIAL_CAPACITY=2 (defaults: those of PMAParams), one binary per
// setting. meta.json records them; --stress checks them too.

#include <chrono>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../experiments/common/permutation.hpp"
#include "../experiments/common/run.hpp"
#include "../src/PMA/pma.hpp"

#ifndef PMA_LEAF_UPPER
#define PMA_LEAF_UPPER (PMAParams{}.leaf_upper)
#endif
#ifndef PMA_ROOT_UPPER
#define PMA_ROOT_UPPER (PMAParams{}.root_upper)
#endif
#ifndef PMA_INITIAL_CAPACITY
#define PMA_INITIAL_CAPACITY (PMAParams{}.initial_capacity)
#endif

constexpr PMAParams PARAMS{.leaf_upper = PMA_LEAF_UPPER, .root_upper = PMA_ROOT_UPPER,
                           .initial_capacity = PMA_INITIAL_CAPACITY};

// Inserts keys one at a time, checking everything after each; false on the
// first mismatch, with what went wrong on stderr.
template <class Pma>
bool stress_case(const std::vector<int64_t> &keys, const std::string &desc) {
    Pma storage;
    PackedMemoryArray &pma = storage;
    std::set<int64_t> expected;
    std::vector<int64_t> got, slots;
    for (size_t i = 0; i < keys.size(); ++i) {
        bool inserted = pma.insert(keys[i]);
        bool fresh = expected.insert(keys[i]).second;
        pma.points(got, slots);
        const char *error = nullptr;
        if (inserted != fresh) error = "insert's return value";
        else if (!pma.check()) error = "invariants";
        else if (got != std::vector<int64_t>(expected.begin(), expected.end())) error = "contents";
        else if (pma.size() != expected.size()) error = "size";
        if (error) {
            std::cerr << desc << ": wrong " << error << " after insert " << i << " (key " << keys[i] << ")\n";
            return false;
        }
    }
    return true;
}

// The keys of a stress case: n keys from 0..range-1 (duplicates when range is
// small), in one of several orders.
std::vector<int64_t> stress_keys(std::mt19937_64 &rng, size_t n, int pattern) {
    std::vector<int64_t> keys(n);
    int64_t range = int64_t(rng() % 2 ? n : 4 * n + 1);
    for (auto &k : keys) k = int64_t(rng() % uint64_t(range));
    switch (pattern) {
        case 0: break;                                        // random
        case 1: std::sort(keys.begin(), keys.end()); break;  // ascending
        case 2: std::sort(keys.rbegin(), keys.rend()); break;
        default:  // clustered: most keys near a few centres
            for (auto &k : keys) k = int64_t(rng() % 4) * range + int64_t(rng() % 8);
    }
    return keys;
}

int stress(int iterations, size_t max_n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    int failures = 0;
    for (int it = 0; it < iterations; ++it) {
        size_t n = 1 + rng() % max_n;
        int pattern = int(rng() % 4);
        auto keys = stress_keys(rng, n, pattern);
        std::string desc = "case " + std::to_string(it) + " (n=" + std::to_string(n) +
                           ", pattern " + std::to_string(pattern) + ")";
        // the parameters built with, and a sparser PMA starting from a single leaf
        if (!stress_case<PMA<PARAMS>>(keys, desc + " built")) ++failures;
        if (!stress_case<PMA<PMAParams{.leaf_upper = 0.9, .root_upper = 0.5, .initial_capacity = 2}>>(
                keys, desc + " sparse"))
            ++failures;
    }
    std::cout << iterations << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}

// results/pma/<order>.csv for an order: zipf:16,1 -> zipf_16_1.csv.
std::string csv_name(std::string order) {
    for (char &c : order)
        if (c == ':' || c == ',') c = '_';
    return order + ".csv";
}

// Inserts n keys in this order, writing the rows to <run folder>/<order>.csv.
int workload(size_t n, const std::string &order, uint64_t seed, const std::filesystem::path &out) {
    std::vector<int64_t> keys;
    if (order == "sorted" || order == "reverse") {
        for (size_t i = 1; i <= n; ++i) keys.push_back(int64_t(i));
        if (order == "reverse") std::reverse(keys.begin(), keys.end());
    } else if (parse_order(order, ORDER)) {
        keys = make_permutation(n, seed);
    } else {
        std::cerr << "unknown order " << order << "\n";
        return 2;
    }

    std::filesystem::path path = out / csv_name(order);
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::cerr << "cannot write " << path << "\n";
        return 2;
    }

    PMA<PARAMS> storage;
    PackedMemoryArray &pma = storage;
    std::fprintf(f, "order,n,capacity,leaf_size,density,moves,moves_per_insert,ns_per_insert,rebalances,grows\n");
    auto row = [&](double ns) {
        auto &s = pma.stats();
        size_t m = pma.size();
        // order quoted: zipf:16,1 has a comma
        std::fprintf(f, "\"%s\",%zu,%zu,%zu,%.6f,%llu,%.4f,%.2f,%llu,%llu\n", order.c_str(), m, pma.capacity(),
                     storage.leaf_size(), double(m) / double(pma.capacity()), (unsigned long long)s.moves,
                     double(s.moves) / double(m), ns / double(m), (unsigned long long)s.rebalances,
                     (unsigned long long)s.grows);
    };

    // rows at about 64 sizes per doubling, and just before and at every grow
    double ns = 0;
    size_t next = 1;
    auto clock = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; ++i) {
        size_t capacity = pma.capacity();
        auto before = pma.stats().grows;
        pma.insert(keys[i]);
        bool grew = pma.stats().grows != before;
        if (grew || pma.size() >= next || i + 1 == n) {
            auto now = std::chrono::steady_clock::now();
            ns += std::chrono::duration<double, std::nano>(now - clock).count();
            if (grew && pma.size() > 1) {
                // the last size at the old capacity: same totals, minus this insert's rebuild
                std::fprintf(f, "\"%s\",%zu,%zu,,%.6f,,,,,\n", order.c_str(), pma.size() - 1, capacity,
                             double(pma.size() - 1) / double(capacity));
            }
            row(ns);
            while (next <= pma.size()) next += std::max<size_t>(1, next / 64);
            clock = std::chrono::steady_clock::now();
        }
    }
    std::fclose(f);
    std::cout << "wrote " << path.string() << "\n";
    return 0;
}

// <run>/meta.json: what the workload run is. Rewritten after every order, so
// an interrupted run says how far it got: status stays "running" unless the run
// finished.
struct WorkloadMeta {
    std::string path, command, started, finished;
    int run = 0;
    uint64_t seed = 0;
    size_t n = 0;
    std::vector<std::string> orders;
    std::vector<double> seconds;  // per finished order

    void write() const {
        const PMAParams &p = PARAMS;
        std::ostringstream o;
        o << "{\n"
          << "  \"experiment\": \"pma\",\n"
          << "  \"run\": " << run << ",\n"
          << "  \"status\": \"" << (finished.empty() ? "running" : "complete") << "\",\n"
          << "  \"seed\": " << seed << ",\n"
          << "  \"n\": " << n << ",\n"
          << "  \"orders\": [";
        for (size_t i = 0; i < orders.size(); ++i) o << (i ? ", " : "") << json_string(orders[i]);
        o << "],\n"
          << "  \"pma\": {\"leaf_upper\": " << p.leaf_upper << ", \"root_upper\": " << p.root_upper
          << ", \"initial_capacity\": " << p.initial_capacity << ", \"growth\": \"lazy\"},\n"
          << "  \"command\": " << json_string(command) << ",\n"
          << "  \"started\": " << json_string(started) << ",\n"
          << "  \"finished\": " << (finished.empty() ? "null" : json_string(finished)) << ",\n"
          << "  \"seconds\": {";
        for (size_t i = 0; i < seconds.size(); ++i) o << (i ? ", " : "") << json_string(orders[i]) << ": " << seconds[i];
        o << "}\n}\n";

        // Write and rename, so a reader never sees half a file.
        std::string temporary = path + "/meta.json.tmp";
        std::ofstream(temporary) << o.str();
        std::filesystem::rename(temporary, path + "/meta.json");
    }
};

int main(int argc, char **argv) {
    int iterations = 2000;
    size_t n = 0;  // stress: 200, workload: 2^20
    bool check = false;
    std::string orders = "uniform,zipf:16,1,sorted,reverse";
    std::filesystem::path out = "results/pma";
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-i" && i + 1 < argc) iterations = std::atoi(argv[++i]);
        else if (a == "-n" && i + 1 < argc) n = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--stress") check = true;
        else if (a == "--orders" && i + 1 < argc) orders = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else seed = std::strtoull(a.c_str(), nullptr, 10);
    }
    if (check) return stress(iterations, n ? n : 200, seed);

    // orders are separated by ',', except where the piece after it continues a
    // spec's arguments (zipf:16,1)
    std::vector<std::string> list;
    std::string piece;
    std::stringstream stream(orders);
    while (std::getline(stream, piece, ',')) {
        bool argument = !list.empty() && list.back().find(':') != std::string::npos &&
                        !piece.empty() && (std::isdigit((unsigned char)piece[0]) || piece[0] == '.');
        if (argument) list.back() += "," + piece;
        else list.push_back(piece);
    }
    n = n ? n : size_t(1) << 20;
    for (auto &order : list) {  // before making the run folder, so a typo leaves none
        bool bad = order != "sorted" && order != "reverse" && !parse_order(order, ORDER);
        if (bad || (ORDER.kind == "bitrev" && (n & (n - 1)))) {
            std::cerr << (bad ? "unknown order " : "bitrev needs n a power of two: ") << order << "\n";
            return 2;
        }
    }

    WorkloadMeta meta;
    meta.path = next_run_dir(out.string());
    meta.run = std::stoi(std::filesystem::path(meta.path).filename().string());
    meta.seed = seed;
    meta.n = n;
    meta.orders = list;
    for (int i = 0; i < argc; ++i) meta.command += (i ? " " : "") + std::string(argv[i]);
    meta.started = utc_now();
    meta.write();
    std::cout << "run " << meta.run << ": " << meta.path << "\n";
    for (auto &order : list) {
        auto start = std::chrono::steady_clock::now();
        if (int status = workload(n, order, seed, meta.path)) return status;
        meta.seconds.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        meta.write();
    }
    meta.finished = utc_now();
    meta.write();
    return 0;
}
