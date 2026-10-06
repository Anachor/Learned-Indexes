#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/ORourke/brute_orourke.hpp"
#include "../../src/ORourke/pgm_orourke.hpp"
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

// Experiment 1: query complexity of a static learned index on a sorted array.
//
// For every prefix of a random permutation of {1..n} the points are (key, rank).
// k = 2*delta, so delta covers the half-integers; the points go to O'Rourke as
// (key, 2*rank) with integer bound k.
//
//   query complexity = log2(delta) + log2(segment size), delta = k/2
//
// 1b is FIXED_K (common/segmentation.hpp); 1a is the k minimising the query complexity. Both are
// measured in one pass, and every k evaluated is written out.

const char *DEFAULT_NS = "128,256,512,1024,2048,4096,8192,16384,32768,65536";
const char *DEFAULT_OUT = "results/exp1";

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
    std::vector<int64_t> permutation = make_permutation(n, seed);

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
    std::vector<int64_t> permutation = make_permutation(n, seed);

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
            if (better(k, product, exhaustive_k, exhaustive_product)) {
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

// The segments of a sorted prefix under bound k, as count_segments finds them on
// (key, 2*rank), with each line scaled back to (key, rank).
std::vector<Segment> segments_of(ORourke<int64_t> &orourke, const std::vector<int64_t> &x, int64_t k) {
    orourke.reset(k);
    std::vector<Segment> segments;
    size_t begin = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (auto closed = orourke.add_point(x[i], 2 * int64_t(i))) {
            segments.push_back({begin, i, *closed});
            begin = i;
        }
    }
    if (!x.empty()) segments.push_back({begin, x.size(), orourke.current()});
    for (Segment &s : segments) {
        s.line.y0 /= 2;
        s.line.slope /= 2;
    }
    return segments;
}

// The segments as JSON: [begin, end, x0, y0, slope] each, the line in (key, rank).
std::string segments_json(const std::vector<Segment> &segments) {
    std::ostringstream out;
    out.precision(17);
    out << '[';
    for (size_t s = 0; s < segments.size(); ++s) {
        const Segment &g = segments[s];
        out << (s ? "," : "") << '[' << g.begin << ',' << g.end << ',' << double(g.line.x0) << ','
            << double(g.line.y0) << ',' << double(g.line.slope) << ']';
    }
    out << ']';
    return out.str();
}

// One prefix of length t, delta = 1/2, 1, 3/2, 2, ... (k = 1, 2, 3, ...), with
// qc = log2(delta) + log2(lambda). lambda >= 1, so qc >= log2(delta): once the
// next delta reaches the best delta * lambda so far, no larger delta can have a
// lower qc and the search stops. In k: stop when k + 1 >= k_best * L_best - or,
// with --tiebreaker minlambda, when k + 1 > k_best * L_best, since a larger
// delta with one segment can still tie.
//
// Prints delta,lambda,qc as CSV. With json: the keys, the table, and the best
// delta's segments, for the results server to draw. With segments_k: only the
// segments for that k, which the server fetches when another row is picked -
// sending every delta's segments up front would be megabytes at large t.
void simulate(size_t n, size_t t, uint64_t seed, bool json, int64_t segments_k) {
    std::vector<int64_t> permutation = make_permutation(n, seed);
    std::vector<int64_t> x(permutation.begin(), permutation.begin() + t);
    std::sort(x.begin(), x.end());
    PgmORourke<int64_t> orourke(1);

    if (segments_k) {
        std::cout << "{\"k\":" << segments_k << ",\"segments\":"
                  << segments_json(segments_of(orourke, x, segments_k)) << "}\n";
        return;
    }

    std::vector<std::pair<int64_t, size_t>> tried;  // (k, lambda)
    __int128 best_product = -1;
    int64_t best_k = 1;
    for (int64_t k = 1;; ++k) {
        size_t L = count_segments(orourke, x, k);
        tried.emplace_back(k, L);
        __int128 product = __int128(k) * L;
        if (better(k, product, best_k, best_product)) {
            best_product = product;
            best_k = k;
        }
        if (TIE_MIN_LAMBDA ? __int128(k + 1) > best_product : __int128(k + 1) >= best_product) break;
    }

    if (!json) {
        std::cout << "seed,n,t,delta,lambda,qc\n";
        for (const auto &[k, L] : tried) {
            std::cout << seed << ',' << n << ',' << t << ',' << double(k) / 2 << ',' << L << ','
                      << query_complexity(L, k) << '\n';
        }
        return;
    }

    std::ostringstream out;
    out.precision(17);
    out << "{\"seed\":" << seed << ",\"n\":" << n << ",\"t\":" << t << ",\"keys\":[";
    for (size_t i = 0; i < x.size(); ++i) out << (i ? "," : "") << x[i];
    out << "],\"deltas\":[";
    for (size_t j = 0; j < tried.size(); ++j) {
        const auto &[k, L] = tried[j];
        out << (j ? "," : "") << "{\"k\":" << k << ",\"delta\":" << double(k) / 2 << ",\"lambda\":" << L
            << ",\"qc\":" << query_complexity(L, k) << '}';
    }
    out << "],\"best_k\":" << best_k << ",\"segments\":" << segments_json(segments_of(orourke, x, best_k))
        << "}\n";
    std::cout << out.str();
}


void usage(const char *program) {
    std::cerr << "usage: " << program << " [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--tiebreaker T] [--validate] [seed]\n"
              << "       " << program << " --simulate T [--json | --segments K] [--permutation P] [--tiebreaker T] -n N seed\n"
              << "  -n N,N,...  universe sizes (default " << DEFAULT_NS << ")\n"
              << "  -j THREADS  threads (default: one per core)\n"
              << "  --out DIR   directory holding the runs (default " << DEFAULT_OUT << "); the CSVs go to\n"
              << "              DIR/<run>, run = 1, 2, ... the next unused number\n"
              << "  --validate  check the search against trying every k, and the segment\n"
              << "              sizes against the brute-force O'Rourke; writes no files\n"
              << "  --simulate T  the prefix of length T of one n, with the run's seed: delta =\n"
              << "              1/2, 1, 3/2, ... until no larger delta can lower qc, printing\n"
              << "              every delta tried as CSV (delta,lambda,qc with\n"
              << "              qc = log2 delta + log2 lambda); writes no files\n"
              << "  --json      with --simulate: JSON with the keys, the table and the best\n"
              << "              delta's segments\n"
              << "  --segments K  with --simulate: JSON with the segments for k = K (delta = K/2)\n"
              << "  --permutation P  the insertion order of 1..n (default uniform):\n"
              << "              uniform     a uniformly random permutation\n"
              << "              zipf:R,s    R equal key regions, each insert from a region picked\n"
              << "                          with weight 1/rank^s, hot regions placed at random\n"
              << "              zipf:R,s,d  the same, recursively d levels deep (regions within regions)\n"
              << "              blocks:b    blocks of b consecutive keys, blocks and keys in\n"
              << "                          random order\n"
              << "              bitrev[:p]  almost sorted, then bits reversed: 0..n-1 in order, p*n\n"
              << "                          swaps of two random positions (default 0), each\n"
              << "                          position's bits reversed; needs n a power of two\n"
              << "              probing     linear probing: a random key, or the next free one\n"
              << "                          above it, wrapping from n to 1\n"
              << "              --simulate needs the same P as the run it reproduces\n"
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

int main(int argc, char **argv) {
    std::string sizes_argument = DEFAULT_NS, out_dir = DEFAULT_OUT;
    int threads = omp_get_num_procs();
    bool validate_only = false, has_seed = false;
    uint64_t seed = 0;
    size_t simulate_t = 0;  // 0: not simulating
    bool json = false;
    int64_t segments_k = 0;  // 0: the table, not one k's segments

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        bool takes_value = argument == "-n" || argument == "-j" || argument == "--out" ||
                           argument == "--simulate" || argument == "--segments" ||
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
        } else if (argument == "--simulate") {
            simulate_t = size_t(parse_number(argv[++i], argv[0]));
            if (simulate_t < 1) usage(argv[0]);
        } else if (argument == "--json") {
            json = true;
        } else if (argument == "--segments") {
            segments_k = int64_t(parse_number(argv[++i], argv[0]));
            if (segments_k < 1) usage(argv[0]);
        } else if (!has_seed) {
            seed = parse_number(argument, argv[0]);
            has_seed = true;
        } else {
            usage(argv[0]);
        }
    }

    std::vector<size_t> sizes = parse_sizes(sizes_argument, argv[0]);

    if (!order_fits(sizes)) return 2;

    // Needs the run's seed to reproduce its keys, so no random default. Same
    // per-n seed as a run: seed + n.
    if (simulate_t) {
        if (!has_seed || validate_only || sizes.size() != 1 || simulate_t > sizes[0]) usage(argv[0]);
        simulate(sizes[0], simulate_t, seed + sizes[0], json, segments_k);
        return 0;
    }

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
    meta.experiment = "exp1";
    meta.path = out_dir + "/meta.json";
    meta.run = std::stoi(std::filesystem::path(out_dir).filename().string());
    meta.seed = seed;
    meta.sizes = sizes;
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
