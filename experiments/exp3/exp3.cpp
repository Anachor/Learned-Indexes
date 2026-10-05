#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/ORourke/brute_orourke.hpp"
#include "../../src/ORourke/pgm_orourke.hpp"
#include "../../src/PMA/pma.hpp"
#include "../common/permutation.hpp"
#include "../common/run.hpp"
#include "../common/segmentation.hpp"

#ifdef _OPENMP
#include <omp.h>
#else
inline int omp_get_thread_num() { return 0; }
inline int omp_get_num_threads() { return 1; }
inline int omp_get_num_procs() { return 1; }
inline void omp_set_num_threads(int) {}
#endif

// Experiment 3: query complexity of a static learned index on a PMA.
//
// The keys of a random permutation of {1..n} are inserted one at a time into a
// PMA (src/PMA/pma.hpp, the default parameters), and after every insert
// O'Rourke runs over its current layout. The points are (key, slot), the slot
// counting the gaps, so delta is in slots: the local search after the model
// runs over the array, gaps and all. As in exp1, k = 2*delta and the points go
// to O'Rourke as (key, 2*slot).
//
//   query complexity = log2(delta) + log2(segment size), delta = k/2
//
// 3b is FIXED_K (common/segmentation.hpp); 3a is the k minimising the query
// complexity. Same seeds (n uses seed + n) and --permutation orders as exp1,
// so the same seed and order give exp1's prefixes, and exp1 - the same keys
// packed, y = rank - is the comparison.

using Pma = PMA<>;

const char *DEFAULT_NS = "128,256,512,1024,2048,4096,8192,16384,32768,65536";
const char *DEFAULT_OUT = "results/exp3";

// Progress, weighted by prefix length because a prefix of length t costs O(t).
struct Progress {
    std::atomic<uint64_t> weight_done{0};
    std::atomic<uint64_t> prefixes_done{0};
    uint64_t weight_total = 1;           // of the current n
    uint64_t overall_done_before = 0;    // weight of the n already finished
    uint64_t overall_total = 1;
    std::chrono::steady_clock::time_point started;

    static std::string time_string(double seconds) {
        char buffer[32];
        if (seconds < 60) {
            std::snprintf(buffer, sizeof buffer, "%.0fs", seconds);
        } else {
            std::snprintf(buffer, sizeof buffer, "%dm%02ds", int(seconds) / 60, int(seconds) % 60);
        }
        return buffer;
    }

    void draw(size_t n) const {
        double done = double(weight_done.load());
        double fraction = std::min(1.0, done / double(weight_total));
        double overall = std::min(1.0, (double(overall_done_before) + done) / double(overall_total));
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        double eta = overall > 0 ? elapsed * (1 - overall) / overall : 0;

        std::string bar(20, '-');
        std::fill(bar.begin(), bar.begin() + size_t(fraction * 20), '#');
        std::fprintf(stderr, "\rn=%zu [%s] %3.0f%%  t=%zu/%zu  elapsed %s  eta %s  (overall %.0f%%)   ",
                     n, bar.c_str(), fraction * 100, size_t(prefixes_done.load()), n,
                     time_string(elapsed).c_str(), time_string(eta).c_str(), overall * 100);
        std::fflush(stderr);
    }
};

// One prefix: the PMA's capacity after its insert, and every k evaluated.
struct Prefix {
    size_t capacity = 0;
    std::vector<Row> rows;
};

struct NResult {
    std::vector<Prefix> prefixes;  // indexed by prefix length t
    double seconds = 0;
    double mean_evaluations = 0;
    size_t max_evaluations = 0;
};

// Every prefix of one random permutation of {1..n}. Threads split the prefixes
// between them; each fills its own PMA with every key - the PMA is
// deterministic, so all of them hold the same layout at every t.
NResult run_n(size_t n, uint64_t seed, Progress &progress) {
    std::vector<int64_t> permutation = make_permutation(n, seed);

    NResult result;
    result.prefixes.resize(n + 1);
    auto started = std::chrono::steady_clock::now();

    #pragma omp parallel
    {
        size_t threads = size_t(omp_get_num_threads());
        size_t id = size_t(omp_get_thread_num());
        PgmORourke<int64_t> orourke(1);
        Pma storage;
        PackedMemoryArray &pma = storage;
        std::vector<int64_t> keys, slots;
        keys.reserve(n);
        slots.reserve(n);
        auto last_drawn = std::chrono::steady_clock::now();

        for (size_t i = 0; i < n; ++i) {
            pma.insert(permutation[i]);

            size_t t = i + 1;
            if (t % threads != id) continue;

            pma.points(keys, slots);
            result.prefixes[t] = {pma.capacity(), analyse_prefix(orourke, keys, &slots)};
            progress.weight_done += t;
            ++progress.prefixes_done;

            if (id == 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(now - last_drawn).count() > 0.2) {
                    progress.draw(n);
                    last_drawn = now;
                }
            }
        }
    }

    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    size_t total_evaluations = 0;
    for (size_t t = 1; t <= n; ++t) {
        total_evaluations += result.prefixes[t].rows.size();
        result.max_evaluations = std::max(result.max_evaluations, result.prefixes[t].rows.size());
    }
    result.mean_evaluations = double(total_evaluations) / double(n);
    return result;
}

// Checks the PMA's invariants and contents, the minimisation against trying
// every k, and the segment sizes against BruteORourke, at every prefix. O(n^3),
// so small n only. Returns false on the first mismatch.
bool validate(size_t n, uint64_t seed) {
    std::vector<int64_t> permutation = make_permutation(n, seed);

    PgmORourke<int64_t> pgm(1);
    BruteORourke<int64_t> brute(1);
    Pma storage;
    PackedMemoryArray &pma = storage;
    std::vector<int64_t> sorted, keys, slots;
    for (size_t i = 0; i < n; ++i) {
        int64_t key = permutation[i];
        pma.insert(key);
        sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), key), key);
        pma.points(keys, slots);
        size_t t = sorted.size();
        if (!pma.check() || keys != sorted) {
            std::cout << "PMA MISMATCH at t=" << t << std::endl;
            return false;
        }
        int64_t k_max = full_k(keys, &slots);

        __int128 exhaustive_product = -1;
        int64_t exhaustive_k = 1;
        size_t previous_L = 0;
        for (int64_t k = 1; k <= k_max; ++k) {
            size_t L = count_segments(pgm, keys, k, &slots);
            if (previous_L && L > previous_L) {
                std::cout << "SEGMENT SIZE GREW with k at t=" << t << " k=" << k << std::endl;
                return false;
            }
            previous_L = L;

            __int128 product = __int128(k) * L;
            if (better(k, product, exhaustive_k, exhaustive_product)) {
                exhaustive_product = product;
                exhaustive_k = k;
            }
            if (t <= 64 && count_segments(brute, keys, k, &slots) != L) {
                std::cout << "BRUTE MISMATCH at t=" << t << " k=" << k << std::endl;
                return false;
            }
        }
        if (count_segments(pgm, keys, k_max, &slots) != 1) {
            std::cout << "MORE THAN ONE SEGMENT at k_max, t=" << t << std::endl;
            return false;
        }

        std::vector<Row> rows = analyse_prefix(pgm, keys, &slots);
        const Row *best = nullptr;
        for (const Row &r : rows) {
            if (r.best) best = &r;
        }
        if (!best) {
            std::cout << "NO BEST ROW at t=" << t << std::endl;
            return false;
        }
        if (best->k != exhaustive_k || __int128(best->k) * best->L != exhaustive_product) {
            std::cout << "SEARCH MISMATCH at t=" << t << ": search k=" << best->k
                      << " L=" << best->L << ", exhaustive k=" << exhaustive_k << std::endl;
            return false;
        }
        for (const Row &r : rows) {
            if (r.k < k_max && r.L != count_segments(pgm, keys, r.k, &slots)) {
                std::cout << "ROW MISMATCH at t=" << t << " k=" << r.k << std::endl;
                return false;
            }
        }
    }

    std::cout << "validate n=" << n << ": all prefixes passed" << std::endl;
    return true;
}

void usage(const char *program) {
    std::cerr << "usage: " << program << " [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--tiebreaker T] [--validate] [seed]\n"
              << "  -n N,N,...  universe sizes (default " << DEFAULT_NS << ")\n"
              << "  -j THREADS  threads (default: one per core)\n"
              << "  --out DIR   directory holding the runs (default " << DEFAULT_OUT << "); the CSVs go to\n"
              << "              DIR/<run>, run = 1, 2, ... the next unused number\n"
              << "  --validate  check the PMA, the search against trying every k, and the\n"
              << "              segment sizes against the brute-force O'Rourke; writes no files\n"
              << "  --permutation P  the insertion order of 1..n, as exp1 (default uniform):\n"
              << "              uniform, zipf:R,s, zipf:R,s,d, blocks:b, bitrev[:p], probing\n"
              << "  --tiebreaker T  the best delta when several have the same qc: minlambda, the\n"
              << "              fewest segments (default), or mindelta, the smallest delta\n"
              << "  seed        random if omitted\n";
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

// The PMA's parameters, for meta.json.
std::string pma_json() {
    PMAParams p;
    std::ostringstream o;
    o << "{\"leaf_upper\": " << p.leaf_upper << ", \"root_upper\": " << p.root_upper
      << ", \"initial_capacity\": " << p.initial_capacity << ", \"growth\": \"lazy\"}";
    return o.str();
}

int main(int argc, char **argv) {
    std::string sizes_argument = DEFAULT_NS, out_dir = DEFAULT_OUT;
    int threads = omp_get_num_procs();
    bool validate_only = false, has_seed = false;
    uint64_t seed = 0;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        bool takes_value = argument == "-n" || argument == "-j" || argument == "--out" ||
                           argument == "--permutation" || argument == "--tiebreaker";
        if (takes_value && i + 1 >= argc) usage(argv[0]);

        if (argument == "-n") {
            sizes_argument = argv[++i];
        } else if (argument == "-j") {
            threads = int(parse_number(argv[++i], argv[0]));
            if (threads < 1) usage(argv[0]);
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
    omp_set_num_threads(threads);
    std::cout << "seed: " << seed << "  threads: " << threads
              << "  permutation: " << ORDER.spec() << "  tiebreaker: " << tiebreaker_name() << std::endl;

    if (validate_only) {
        for (size_t n : sizes) {
            if (!validate(n, seed + n)) return 1;
        }
        return 0;
    }

    out_dir = next_run_dir(out_dir);
    std::cout << "run directory: " << out_dir << std::endl;

    RunMeta meta;
    meta.experiment = "exp3";
    meta.path = out_dir + "/meta.json";
    meta.run = std::stoi(std::filesystem::path(out_dir).filename().string());
    meta.seed = seed;
    meta.sizes = sizes;
    meta.cost = "log2(delta) + log2(lambda), delta in PMA slots";
    meta.extra = {{"structure", "\"PMA, points (key, slot)\""}, {"pma", pma_json()}};
    meta.started = utc_now();
    for (int i = 0; i < argc; ++i) meta.command += (i ? " " : "") + std::string(argv[i]);
    // A drawn seed is not on the command line; record it so the command reruns
    // the same permutations.
    if (!has_seed) meta.command += " " + std::to_string(seed);
    meta.write();

    Progress progress;
    progress.started = std::chrono::steady_clock::now();
    progress.overall_total = 0;
    for (size_t n : sizes) progress.overall_total += uint64_t(n) * (n + 1) / 2;

    for (size_t n : sizes) {
        std::string path = out_dir + "/exp3_n" + std::to_string(n) + ".csv";
        std::ofstream out(path);
        if (!out) {
            std::cerr << "could not write " << path << std::endl;
            return 1;
        }

        uint64_t n_seed = seed + n;
        progress.weight_total = uint64_t(n) * (n + 1) / 2;
        progress.weight_done = 0;
        progress.prefixes_done = 0;
        NResult result = run_n(n, n_seed, progress);
        progress.draw(n);
        std::fprintf(stderr, "\n");
        progress.overall_done_before += progress.weight_total;

        out << "seed,n,t,capacity,k,L,cost,fixed,best\n";
        for (size_t t = 1; t <= n; ++t) {
            const Prefix &p = result.prefixes[t];
            for (const Row &r : p.rows) {
                out << n_seed << ',' << n << ',' << t << ',' << p.capacity << ',' << r.k << ',' << r.L << ','
                    << query_complexity(r.L, r.k) << ',' << (r.fixed ? 1 : 0) << ','
                    << (r.best ? 1 : 0) << '\n';
            }
        }

        out.close();
        std::cout << "n=" << n << ": " << result.seconds << " s, "
                  << result.mean_evaluations << " k evaluated per prefix on average, "
                  << result.max_evaluations << " at most -> " << path << std::endl;

        meta.timing.push_back({n, result.seconds, result.mean_evaluations, result.max_evaluations});
        meta.write();
    }

    meta.finished = utc_now();
    meta.write();
    std::cout << "metadata -> " << meta.path << std::endl;
}
