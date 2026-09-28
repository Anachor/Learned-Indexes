#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/ORourke/brute_orourke.hpp"
#include "../../src/ORourke/pgm_orourke.hpp"
#include "../common/permutation.hpp"
#include "../common/run.hpp"
#include "../common/segmentation.hpp"

// Experiment 2: query complexity of a dynamic learned index - Bentley-Saxe over
// static O'Rourke indexes, as in the PGM paper (Section 3): base 2, no buffer.
//
// Levels 0, 1, 2, ... are each empty or hold exactly 2^i keys, sorted, with
// their own static index. Inserting the t-th key finds the first empty level i
// (the number of trailing zero bits of t), merges levels 0..i-1 and the new key
// into it, and empties levels 0..i-1. So after t inserts the non-empty levels
// are the 1-bits of t, and level i holds the 2^i keys inserted at t - 2^i + 1 .. t
// where t is the time it was built.
//
// Each build segments only the new level, on (key, 2*rank within the level) -
// every level is its own static index - with the same k as exp1: FIXED_K (2a)
// and the best k for that level (2b), one analyse_prefix call. A level keeps its
// segmentation until it is merged away.
//
// The CSV has one build per row group: the prefix t it was built at, its level
// and size, and every k evaluated on it. The levels at prefix t, and so the
// total over levels, follow from t's bits: level i (bit i set) was built at t
// with its low i bits cleared. The plot script sums them.

const char *DEFAULT_NS = "128,256,512,1024,2048,4096,8192,16384,32768,65536";
const char *DEFAULT_OUT = "results/exp2";

// Level of the build at prefix t: the first empty level, t's trailing zero bits.
unsigned build_level(size_t t) {
    unsigned level = 0;
    while (!((t >> level) & 1)) ++level;
    return level;
}

// The base-2 Bentley-Saxe levels of the keys inserted so far.
struct Levels {
    std::vector<std::vector<int64_t>> keys;  // keys[i]: empty or 2^i keys, sorted

    // Inserts key as the t-th key (t = 1, 2, ...); returns the level built.
    unsigned insert(int64_t key, size_t t) {
        unsigned level = build_level(t);
        if (keys.size() <= level) keys.resize(level + 1);
        std::vector<int64_t> merged{key}, next;
        for (unsigned j = 0; j < level; ++j) {
            next.clear();
            std::merge(merged.begin(), merged.end(), keys[j].begin(), keys[j].end(), std::back_inserter(next));
            merged.swap(next);
            keys[j].clear();
        }
        keys[level] = std::move(merged);
        return level;
    }
};

struct Build {
    unsigned level;
    size_t size;
    std::vector<Row> rows;
};

struct NResult {
    std::vector<Build> builds;  // indexed by prefix length t; builds[0] unused
    double seconds = 0;
    double mean_evaluations = 0;
    size_t max_evaluations = 0;
};

// Every insert of one permutation of {1..n}, and the level each builds.
NResult run_n(size_t n, uint64_t seed) {
    std::vector<int64_t> permutation = make_permutation(n, seed);
    auto started = std::chrono::steady_clock::now();

    NResult result;
    result.builds.resize(n + 1);
    PgmORourke<int64_t> orourke(1);
    Levels levels;
    size_t total_evaluations = 0;
    for (size_t t = 1; t <= n; ++t) {
        unsigned level = levels.insert(permutation[t - 1], t);
        const std::vector<int64_t> &keys = levels.keys[level];
        result.builds[t] = {level, keys.size(), analyse_prefix(orourke, keys)};
        total_evaluations += result.builds[t].rows.size();
        result.max_evaluations = std::max(result.max_evaluations, result.builds[t].rows.size());
    }

    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    result.mean_evaluations = double(total_evaluations) / double(n);
    return result;
}

// Checks the levels against their definition, and each build's search against
// trying every k and its segment sizes against BruteORourke. Small n only.
// Returns false on the first mismatch.
bool validate(size_t n, uint64_t seed) {
    std::vector<int64_t> permutation = make_permutation(n, seed);

    PgmORourke<int64_t> pgm(1);
    BruteORourke<int64_t> brute(1);
    Levels levels;
    std::vector<int64_t> sorted;
    std::vector<size_t> built_at;  // built_at[i]: the prefix level i was last built at
    for (size_t t = 1; t <= n; ++t) {
        int64_t key = permutation[t - 1];
        sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), key), key);
        unsigned level = levels.insert(key, t);
        if (built_at.size() <= level) built_at.resize(level + 1);
        built_at[level] = t;

        // The non-empty levels are t's 1-bits, level i holds 2^i keys, and
        // together they are the prefix.
        std::vector<int64_t> all;
        for (unsigned i = 0; i < levels.keys.size(); ++i) {
            const std::vector<int64_t> &keys = levels.keys[i];
            bool bit = (t >> i) & 1;
            if (keys.size() != (bit ? size_t(1) << i : 0)) {
                std::cout << "LEVEL SIZE at t=" << t << " level=" << i << ": " << keys.size() << std::endl;
                return false;
            }
            if (!std::is_sorted(keys.begin(), keys.end())) {
                std::cout << "LEVEL NOT SORTED at t=" << t << " level=" << i << std::endl;
                return false;
            }
            // Built at t with its low i bits cleared, from the 2^i keys before.
            if (bit) {
                size_t at = t & ~((size_t(1) << i) - 1);
                if (built_at[i] != at) {
                    std::cout << "BUILD TIME at t=" << t << " level=" << i << std::endl;
                    return false;
                }
                std::vector<int64_t> expected(permutation.begin() + (at - keys.size()), permutation.begin() + at);
                std::sort(expected.begin(), expected.end());
                if (expected != keys) {
                    std::cout << "LEVEL KEYS at t=" << t << " level=" << i << std::endl;
                    return false;
                }
            }
            all.insert(all.end(), keys.begin(), keys.end());
        }
        std::sort(all.begin(), all.end());
        if (all != sorted) {
            std::cout << "LEVELS ARE NOT THE PREFIX at t=" << t << std::endl;
            return false;
        }

        // The new level's segmentation.
        const std::vector<int64_t> &keys = levels.keys[level];
        std::vector<Row> rows = analyse_prefix(pgm, keys);
        const Row *best = nullptr;
        for (const Row &r : rows) {
            if (r.best) best = &r;
        }
        auto [exhaustive_k, exhaustive_product] = exhaustive_best(pgm, keys);
        if (!best || best->k != exhaustive_k || __int128(best->k) * best->L != exhaustive_product) {
            std::cout << "SEARCH MISMATCH at t=" << t << " level=" << level << std::endl;
            return false;
        }
        int64_t k_max = std::max<int64_t>(1, int64_t(keys.size()) - 1);
        for (const Row &r : rows) {
            if (r.k < k_max && r.L != count_segments(pgm, keys, r.k)) {
                std::cout << "ROW MISMATCH at t=" << t << " k=" << r.k << std::endl;
                return false;
            }
        }
        if (keys.size() <= 64) {
            for (int64_t k = 1; k <= k_max; ++k) {
                if (count_segments(brute, keys, k) != count_segments(pgm, keys, k)) {
                    std::cout << "BRUTE MISMATCH at t=" << t << " level=" << level << " k=" << k << std::endl;
                    return false;
                }
            }
        }
    }

    std::cout << "validate n=" << n << ": all prefixes passed" << std::endl;
    return true;
}

void usage(const char *program) {
    std::cerr << "usage: " << program << " [-n N,N,...] [--out DIR] [--permutation P] [--tiebreaker T] [--validate] [seed]\n"
              << "  -n N,N,...  universe sizes (default " << DEFAULT_NS << ")\n"
              << "  --out DIR   directory holding the runs (default " << DEFAULT_OUT << "); the CSVs go to\n"
              << "              DIR/<run>, run = 1, 2, ... the next unused number\n"
              << "  --validate  check the levels, the search against trying every k, and the\n"
              << "              segment sizes against the brute-force O'Rourke; writes no files\n"
              << "  --permutation P  the insertion order of 1..n (default uniform), as for exp1:\n"
              << "              uniform, zipf:R,s, zipf:R,s,d, blocks:b, bitrev[:p], probing\n"
              << "              (described in the README)\n"
              << "  --tiebreaker T  each level's best delta when several have the same qc:\n"
              << "              minlambda, the fewest segments (default), or mindelta, the smallest delta\n"
              << "  seed        random if omitted; n uses seed + n, as in exp1, so the same seed\n"
              << "              and permutation give exp1's prefixes\n";
    std::exit(1);
}

uint64_t parse_number(const std::string &s, const char *program) {
    try {
        size_t used;
        uint64_t value = std::stoull(s, &used);
        if (used != s.size()) usage(program);
        return value;
    } catch (const std::exception &) {
        usage(program);
    }
    return 0;
}

std::vector<size_t> parse_sizes(const std::string &s, const char *program) {
    std::vector<size_t> sizes;
    std::stringstream stream(s);
    std::string token;
    while (std::getline(stream, token, ',')) {
        size_t value = size_t(parse_number(token, program));
        if (value < 1) usage(program);
        sizes.push_back(value);
    }
    if (sizes.empty()) usage(program);
    return sizes;
}

int main(int argc, char **argv) {
    std::string sizes_argument = DEFAULT_NS, out_dir = DEFAULT_OUT;
    bool validate_only = false, has_seed = false;
    uint64_t seed = 0;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        bool takes_value = argument == "-n" || argument == "--out" || argument == "--permutation" ||
                           argument == "--tiebreaker";
        if (takes_value && i + 1 >= argc) usage(argv[0]);

        if (argument == "-n") {
            sizes_argument = argv[++i];
        } else if (argument == "--out") {
            out_dir = argv[++i];
        } else if (argument == "--validate") {
            validate_only = true;
        } else if (argument == "--tiebreaker") {
            if (!parse_tiebreaker(argv[++i])) {
                std::cerr << "unknown tiebreaker: " << argv[i] << " (mindelta or minlambda)\n";
                usage(argv[0]);
            }
        } else if (argument == "--permutation") {
            if (!parse_order(argv[++i], ORDER)) {
                std::cerr << "unknown permutation: " << argv[i] << "\n";
                usage(argv[0]);
            }
        } else if (!has_seed) {
            seed = parse_number(argument, argv[0]);
            has_seed = true;
        } else {
            usage(argv[0]);
        }
    }

    std::vector<size_t> sizes = parse_sizes(sizes_argument, argv[0]);
    if (!order_fits(sizes)) return 2;

    if (!has_seed) seed = std::random_device{}();
    std::cout << "seed: " << seed << "  permutation: " << ORDER.spec() << "  tiebreaker: " << tiebreaker_name()
              << std::endl;

    if (validate_only) {
        for (size_t n : sizes) {
            if (!validate(n, seed + n)) return 1;
        }
        return 0;
    }

    out_dir = next_run_dir(out_dir);
    std::cout << "run directory: " << out_dir << std::endl;

    RunMeta meta;
    meta.experiment = "exp2";
    meta.path = out_dir + "/meta.json";
    meta.run = std::stoi(std::filesystem::path(out_dir).filename().string());
    meta.seed = seed;
    meta.sizes = sizes;
    meta.cost = "per level log2(delta) + log2(lambda), summed over the non-empty levels";
    meta.extra = {{"structure", "\"bentley-saxe, base 2, no buffer\""}};
    meta.started = utc_now();
    for (int i = 0; i < argc; ++i) meta.command += (i ? " " : "") + std::string(argv[i]);
    // A drawn seed is not on the command line; record it so the command reruns
    // the same permutations.
    if (!has_seed) meta.command += " " + std::to_string(seed);
    meta.write();

    for (size_t n : sizes) {
        std::string path = out_dir + "/exp2_n" + std::to_string(n) + ".csv";
        std::ofstream out(path);
        if (!out) {
            std::cerr << "could not write " << path << std::endl;
            return 1;
        }

        uint64_t n_seed = seed + n;
        NResult result = run_n(n, n_seed);

        out << "seed,n,t,level,size,k,L,cost,fixed,best\n";
        for (size_t t = 1; t <= n; ++t) {
            const Build &b = result.builds[t];
            for (const Row &r : b.rows) {
                out << n_seed << ',' << n << ',' << t << ',' << b.level << ',' << b.size << ',' << r.k << ','
                    << r.L << ',' << query_complexity(r.L, r.k) << ',' << (r.fixed ? 1 : 0) << ','
                    << (r.best ? 1 : 0) << '\n';
            }
        }

        out.close();
        std::cout << "n=" << n << ": " << result.seconds << " s, " << result.mean_evaluations
                  << " k evaluated per build on average, " << result.max_evaluations << " at most -> " << path
                  << std::endl;

        meta.timing.push_back({n, result.seconds, result.mean_evaluations, result.max_evaluations});
        meta.write();
    }

    meta.finished = utc_now();
    meta.write();
    std::cout << "metadata -> " << meta.path << std::endl;
}
