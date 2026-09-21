#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/ORourke/brute_orourke.hpp"
#include "../../src/ORourke/pgm_orourke.hpp"

#ifdef _OPENMP
#include <omp.h>
#else
inline int omp_get_thread_num() { return 0; }
inline int omp_get_num_threads() { return 1; }
inline int omp_get_num_procs() { return 1; }
inline void omp_set_num_threads(int) {}
#endif

// Experiment 1: query complexity of a static learned index on a sorted array.
//
// For every prefix of a random permutation of {1..n} the points are (key, rank).
// k = 2*delta, so delta covers the half-integers; the points go to O'Rourke as
// (key, 2*rank) with integer bound k.
//
//   query complexity = log2(segment size) + log2(k)
//
// 1b is the fixed k below; 1a is the k minimising the query complexity. Both are
// measured in one pass, and every k evaluated is written out.

// delta = 0.5, 1, 2, 4, 8, 16, 32
const std::vector<int64_t> FIXED_K = {1, 2, 4, 8, 16, 32, 64};
const char *DEFAULT_NS = "128,256,512,1024,2048,4096,8192,16384,32768,65536";
const char *DEFAULT_OUT = "results/exp1";

// One evaluated error bound on one prefix.
struct Row {
    int64_t k;
    size_t L;    // segment size
    bool fixed;  // one of FIXED_K (1b)
    bool best;   // the minimiser (1a)
};

double query_complexity(size_t L, int64_t k) { return std::log2(double(L)) + std::log2(double(k)); }

// Segment size of a sorted prefix under bound k, using y = 2*rank.
size_t count_segments(ORourke<int64_t> &orourke, const std::vector<int64_t> &x, int64_t k) {
    if (x.empty()) return 0;
    orourke.reset(k);
    size_t segments = 1;
    for (size_t i = 0; i < x.size(); ++i) {
        if (orourke.add_point(x[i], 2 * int64_t(i))) ++segments;
    }
    return segments;
}

// Every k evaluated on this prefix, sorted by k: the fixed ones first, then
// whatever the minimisation needs.
//
// The minimisation is exact. L(k) never increases with k, so where L is flat the
// smallest k wins and the optimum sits at a step of L. A range [lo, hi] can be
// discarded when lo * L(hi), the best product it could hold, cannot beat the
// incumbent.
std::vector<Row> analyse_prefix(ORourke<int64_t> &orourke, const std::vector<int64_t> &x) {
    int64_t k_max = std::max<int64_t>(1, int64_t(x.size()) - 1);

    std::vector<Row> rows;
    auto L = [&](int64_t k) -> size_t {
        for (const Row &r : rows) {
            if (r.k == k) return r.L;
        }
        // At k >= k_max a horizontal line through the middle rank always fits.
        size_t v = k >= k_max ? 1 : count_segments(orourke, x, k);
        rows.push_back({k, v, false, false});
        return v;
    };

    // 1b first: also a good incumbent for the search below.
    for (int64_t k : FIXED_K) L(k);
    for (Row &r : rows) r.fixed = true;

    __int128 best_product = -1;
    int64_t best_k = 1;
    auto consider = [&](int64_t k, size_t Lk) {
        if (k > k_max) return;
        __int128 product = __int128(k) * Lk;
        if (best_product < 0 || product < best_product || (product == best_product && k < best_k)) {
            best_product = product;
            best_k = k;
        }
    };
    for (const Row &r : rows) consider(r.k, r.L);
    for (int64_t k = 1; k <= k_max; k *= 2) consider(k, L(k));
    consider(k_max, L(k_max));

    // Ranges [lo, hi] whose endpoint values are known.
    std::vector<std::array<int64_t, 4>> stack{{1, k_max, int64_t(L(1)), int64_t(L(k_max))}};
    while (!stack.empty()) {
        auto [lo, hi, L_lo, L_hi] = stack.back();
        stack.pop_back();
        if (hi <= lo + 1) continue;
        if (L_lo == L_hi) continue;                        // L is flat here, lo already considered
        if (__int128(lo) * L_hi > best_product) continue;  // nothing here can win

        int64_t mid = lo + (hi - lo) / 2;
        size_t L_mid = L(mid);
        consider(mid, L_mid);
        stack.push_back({lo, mid, L_lo, int64_t(L_mid)});
        stack.push_back({mid, hi, int64_t(L_mid), L_hi});
    }

    for (Row &r : rows) {
        if (r.k == best_k) r.best = true;
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.k < b.k; });
    return rows;
}

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

struct NResult {
    std::vector<std::vector<Row>> rows;  // indexed by prefix length t
    double seconds = 0;
    double mean_evaluations = 0;
    size_t max_evaluations = 0;
};

// Every prefix of one random permutation of {1..n}. Threads split the prefixes
// between them; each keeps its own sorted copy of the keys inserted so far.
NResult run_n(size_t n, uint64_t seed, Progress &progress) {
    std::vector<int64_t> permutation(n);
    std::iota(permutation.begin(), permutation.end(), 1);
    std::mt19937_64 rng(seed);
    std::shuffle(permutation.begin(), permutation.end(), rng);

    NResult result;
    result.rows.resize(n + 1);
    auto started = std::chrono::steady_clock::now();

    #pragma omp parallel
    {
        size_t threads = size_t(omp_get_num_threads());
        size_t id = size_t(omp_get_thread_num());
        PgmORourke<int64_t> orourke(1);
        std::vector<int64_t> sorted;
        sorted.reserve(n);
        auto last_drawn = std::chrono::steady_clock::now();

        for (size_t i = 0; i < n; ++i) {
            int64_t key = permutation[i];
            sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), key), key);

            size_t t = i + 1;
            if (t % threads != id) continue;

            result.rows[t] = analyse_prefix(orourke, sorted);
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
        total_evaluations += result.rows[t].size();
        result.max_evaluations = std::max(result.max_evaluations, result.rows[t].size());
    }
    result.mean_evaluations = double(total_evaluations) / double(n);
    return result;
}

// Checks the minimisation against trying every k, and the segment sizes against
// BruteORourke. O(n^3), so small n only. Returns false on the first mismatch.
bool validate(size_t n, uint64_t seed) {
    std::vector<int64_t> permutation(n);
    std::iota(permutation.begin(), permutation.end(), 1);
    std::mt19937_64 rng(seed);
    std::shuffle(permutation.begin(), permutation.end(), rng);

    PgmORourke<int64_t> pgm(1);
    BruteORourke<int64_t> brute(1);
    std::vector<int64_t> sorted;
    for (size_t i = 0; i < n; ++i) {
        int64_t key = permutation[i];
        sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), key), key);
        size_t t = sorted.size();
        int64_t k_max = std::max<int64_t>(1, int64_t(t) - 1);

        __int128 exhaustive_product = -1;
        int64_t exhaustive_k = 1;
        size_t previous_L = 0;
        for (int64_t k = 1; k <= k_max; ++k) {
            size_t L = count_segments(pgm, sorted, k);
            if (previous_L && L > previous_L) {
                std::cout << "SEGMENT SIZE GREW with k at t=" << t << " k=" << k << std::endl;
                return false;
            }
            previous_L = L;

            __int128 product = __int128(k) * L;
            if (exhaustive_product < 0 || product < exhaustive_product) {
                exhaustive_product = product;
                exhaustive_k = k;
            }
            if (t <= 64 && count_segments(brute, sorted, k) != L) {
                std::cout << "BRUTE MISMATCH at t=" << t << " k=" << k << std::endl;
                return false;
            }
        }

        std::vector<Row> rows = analyse_prefix(pgm, sorted);
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
            if (r.L != count_segments(pgm, sorted, r.k) && r.k < k_max) {
                std::cout << "ROW MISMATCH at t=" << t << " k=" << r.k << std::endl;
                return false;
            }
        }
    }

    // The full permutation is {1..n}: exactly linear, so one segment.
    if (count_segments(pgm, sorted, 1) != 1) {
        std::cout << "EXPECTED SEGMENT SIZE 1 for the full permutation" << std::endl;
        return false;
    }

    std::cout << "validate n=" << n << ": all prefixes passed" << std::endl;
    return true;
}

// Creates DIR/<run> for the next unused run number and returns its path.
std::string next_run_dir(const std::string &base) {
    namespace fs = std::filesystem;
    int run = 0;
    if (fs::exists(base)) {
        for (const auto &entry : fs::directory_iterator(base)) {
            std::string name = entry.path().filename().string();
            if (entry.is_directory() && !name.empty() &&
                name.find_first_not_of("0123456789") == std::string::npos) {
                run = std::max(run, std::stoi(name));
            }
        }
    }
    std::string dir = base + "/" + std::to_string(run + 1);
    fs::create_directories(dir);
    return dir;
}

void usage(const char *program) {
    std::cerr << "usage: " << program << " [-n N,N,...] [-j THREADS] [--out DIR] [--validate] [seed]\n"
              << "  -n N,N,...  universe sizes (default " << DEFAULT_NS << ")\n"
              << "  -j THREADS  threads (default: one per core)\n"
              << "  --out DIR   directory holding the runs (default " << DEFAULT_OUT << "); the CSVs go to\n"
              << "              DIR/<run>, run = 1, 2, ... the next unused number\n"
              << "  --validate  check the search against trying every k, and the segment\n"
              << "              sizes against the brute-force O'Rourke; writes no files\n"
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

int main(int argc, char **argv) {
    std::string sizes_argument = DEFAULT_NS, out_dir = DEFAULT_OUT;
    int threads = omp_get_num_procs();
    bool validate_only = false, has_seed = false;
    uint64_t seed = 0;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        bool takes_value = argument == "-n" || argument == "-j" || argument == "--out";
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
        } else if (!has_seed) {
            seed = parse_number(argument, argv[0]);
            has_seed = true;
        } else {
            usage(argv[0]);
        }
    }

    std::vector<size_t> sizes = parse_sizes(sizes_argument, argv[0]);
    if (!has_seed) seed = std::random_device{}();
    omp_set_num_threads(threads);
    std::cout << "seed: " << seed << "  threads: " << threads << std::endl;

    if (validate_only) {
        for (size_t n : sizes) {
            if (!validate(n, seed + n)) return 1;
        }
        return 0;
    }

    out_dir = next_run_dir(out_dir);
    std::cout << "run directory: " << out_dir << std::endl;

    Progress progress;
    progress.started = std::chrono::steady_clock::now();
    progress.overall_total = 0;
    for (size_t n : sizes) progress.overall_total += uint64_t(n) * (n + 1) / 2;

    for (size_t n : sizes) {
        std::string path = out_dir + "/exp1_n" + std::to_string(n) + ".csv";
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

        out << "seed,n,t,k,L,cost,fixed,best\n";
        for (size_t t = 1; t <= n; ++t) {
            for (const Row &r : result.rows[t]) {
                out << n_seed << ',' << n << ',' << t << ',' << r.k << ',' << r.L << ','
                    << query_complexity(r.L, r.k) << ',' << (r.fixed ? 1 : 0) << ','
                    << (r.best ? 1 : 0) << '\n';
            }
        }

        std::cout << "n=" << n << ": " << result.seconds << " s, "
                  << result.mean_evaluations << " k evaluated per prefix on average, "
                  << result.max_evaluations << " at most -> " << path << std::endl;
    }
}
