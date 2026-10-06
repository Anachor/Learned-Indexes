#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/Hull/scan_hull.hpp"
#include "../../src/Hull/tree_hull.hpp"
#include "../../src/Hull/vector_hull.hpp"
#include "../../src/GPLA/gpla.hpp"
#include "../../src/ORourke/pgm_orourke.hpp"
#include "../../src/PMA/pma.hpp"
#include "../common/permutation.hpp"
#include "../common/run.hpp"
#include "../common/segmentation.hpp"

#ifdef _OPENMP
#include <omp.h>
#else
inline int omp_get_thread_num() { return 0; }
inline int omp_get_num_procs() { return 1; }
inline void omp_set_num_threads(int) {}
#endif

// Experiment 4: query complexity of the GPLA (src/GPLA), our dynamic
// structure, at every prefix.
//
// The keys of a permutation of {1..n} are inserted one at a time into a
// GPLA, once for each delta of a grid (--deltas, default 0.5, 1, 2, ...,
// 1024), each keeping its delta throughout. After every insert, its number of
// segments lambda:
//
//   query complexity = log2(delta) + log2(lambda), delta = k/2, in slots
//
// best marks, at each prefix, the delta of the grid with the lowest query
// complexity, ties the --tiebreaker way. The PMA is exp3's (the default
// parameters), and the seeds (n uses seed + n) and --permutation orders are
// exp1's, so the same seed and order give exp3's layouts: exp3's static
// O'Rourke on the same points is the comparison, and lambda is at most twice
// its optimum - 1, at the same delta.
//
// --hull picks what each segment keeps to test joins (src/Hull). The
// segments are the same either way; only the time differs.

const char *DEFAULT_NS = "128,256,512,1024,2048,4096,8192,16384,32768,65536";
const char *DEFAULT_OUT = "results/exp4";
const char *DEFAULT_DELTAS = "0.5,1,2,4,8,16,32,64,128,256,512,1024";

// Progress over every insert of every delta.
struct Progress {
    std::atomic<uint64_t> done{0};       // inserts, of the current n
    uint64_t total = 1;                   // n * deltas
    uint64_t overall_done_before = 0;     // inserts of the n already finished
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
        double fraction = std::min(1.0, double(done.load()) / double(total));
        double overall = std::min(1.0, (double(overall_done_before) + double(done.load())) / double(overall_total));
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        double eta = overall > 0 ? elapsed * (1 - overall) / overall : 0;

        std::string bar(20, '-');
        std::fill(bar.begin(), bar.begin() + size_t(fraction * 20), '#');
        std::fprintf(stderr, "\rn=%zu [%s] %3.0f%%  inserts=%llu/%llu  elapsed %s  eta %s  (overall %.0f%%)   ",
                     n, bar.c_str(), fraction * 100, (unsigned long long)done.load(), (unsigned long long)total,
                     time_string(elapsed).c_str(), time_string(eta).c_str(), overall * 100);
        std::fflush(stderr);
    }
};

// One delta over one permutation: lambda and the PMA's capacity after each
// insert t (index t), the time the inserts took, and the index's counters.
struct DeltaRun {
    int64_t k = 0;
    std::vector<uint32_t> segments;
    std::vector<uint64_t> capacity;
    double seconds = 0;
    uint64_t rebuilt_points = 0, rebuilt_segments = 0, splits = 0, join_attempts = 0, joins = 0, grows = 0;
};

template <class H>
DeltaRun run_delta(const std::vector<int64_t> &permutation, int64_t k, Progress &progress) {
    size_t n = permutation.size();
    DeltaRun r;
    r.k = k;
    r.segments.assign(n + 1, 0);
    r.capacity.assign(n + 1, 0);
    gpla::GPLA<H> index(k);

    auto started = std::chrono::steady_clock::now();
    auto last_drawn = started;
    uint64_t pending = 0;
    for (size_t i = 0; i < n; ++i) {
        index.insert(permutation[i]);
        r.segments[i + 1] = uint32_t(index.segment_count());
        r.capacity[i + 1] = index.pma().capacity();
        if (++pending == 1024) {
            progress.done += pending;
            pending = 0;
            if (omp_get_thread_num() == 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(now - last_drawn).count() > 0.2) {
                    progress.draw(n);
                    last_drawn = now;
                }
            }
        }
    }
    progress.done += pending;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    const auto &s = index.stats();
    r.rebuilt_points = s.rebuilt_points;
    r.rebuilt_segments = s.rebuilt_segments;
    r.splits = s.splits;
    r.join_attempts = s.join_attempts;
    r.joins = s.joins;
    r.grows = s.grows;
    return r;
}

// Every delta on one permutation of {1..n}, the deltas in parallel, the largest
// first: they keep the largest segments, so they take the longest.
std::vector<DeltaRun> run_n(size_t n, uint64_t seed, const std::vector<int64_t> &ks, const std::string &hull,
                            Progress &progress) {
    std::vector<int64_t> permutation = make_permutation(n, seed);
    std::vector<DeltaRun> runs(ks.size());
    std::vector<size_t> order(ks.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ks[a] > ks[b]; });

    #pragma omp parallel for schedule(dynamic, 1)
    for (size_t j = 0; j < order.size(); ++j) {
        size_t d = order[j];
        if (hull == "scan") runs[d] = run_delta<gpla::ScanHull>(permutation, ks[d], progress);
        else if (hull == "tree") runs[d] = run_delta<gpla::TreeHull>(permutation, ks[d], progress);
        else runs[d] = run_delta<gpla::VectorHull>(permutation, ks[d], progress);
    }
    return runs;
}

// At every prefix and delta: the three hulls' indexes pass check() (their
// segments, lines, no two neighbours joinable, T2's trees) and agree; their PMA
// holds exp3's layout;
// lambda <= 2 * optimum - 1, the optimum counted as exp3 counts it (PGM's
// O'Rourke); and lower_bound is right for every key 0..n+1. Small n only.
bool validate(size_t n, uint64_t seed, const std::vector<int64_t> &ks) {
    std::vector<int64_t> permutation = make_permutation(n, seed);
    PgmORourke<int64_t> pgm(1);
    for (int64_t k : ks) {
        gpla::GPLA<gpla::ScanHull> scan(k);
        gpla::GPLA<gpla::VectorHull> vector(k);
        gpla::GPLA<gpla::TreeHull> tree(k);
        PMA<> plain;
        std::set<int64_t> inserted;
        std::vector<int64_t> keys, slots, other_keys, other_slots;
        for (size_t i = 0; i < n; ++i) {
            size_t t = i + 1;
            auto fail = [&](const std::string &what) {
                std::cout << what << " at t=" << t << ", delta=" << double(k) / 2 << std::endl;
                return false;
            };
            scan.insert(permutation[i]);
            vector.insert(permutation[i]);
            tree.insert(permutation[i]);
            plain.insert(permutation[i]);
            inserted.insert(permutation[i]);

            plain.points(keys, slots);
            for (const PackedMemoryArray *pma : {static_cast<const PackedMemoryArray *>(&scan.pma()),
                                                 static_cast<const PackedMemoryArray *>(&vector.pma()),
                                                 static_cast<const PackedMemoryArray *>(&tree.pma())}) {
                pma->points(other_keys, other_slots);
                if (other_keys != keys || other_slots != slots) return fail("LAYOUT DIFFERS FROM exp3's PMA");
            }
            if (const char *problem = scan.problem()) return fail(std::string("ScanHull: ") + problem);
            if (const char *problem = vector.problem()) return fail(std::string("VectorHull: ") + problem);
            if (const char *problem = tree.problem()) return fail(std::string("TreeHull: ") + problem);
            if (scan.segment_count() != vector.segment_count() || scan.segment_count() != tree.segment_count())
                return fail("THE HULLS GIVE DIFFERENT SEGMENTS");
            size_t optimal = count_segments(pgm, keys, k, &slots);
            if (scan.segment_count() > 2 * optimal - 1) return fail("MORE THAN 2 * OPTIMAL - 1 SEGMENTS");
            for (int64_t q = 0; q <= int64_t(n) + 1; ++q) {
                auto it = inserted.lower_bound(q);
                std::optional<int64_t> expected;
                if (it != inserted.end()) expected = *it;
                if (scan.lower_bound(q) != expected || vector.lower_bound(q) != expected ||
                    tree.lower_bound(q) != expected)
                    return fail("LOWER_BOUND(" + std::to_string(q) + ") WRONG");
            }
        }
    }
    std::cout << "validate n=" << n << ": all prefixes passed, " << ks.size() << " deltas" << std::endl;
    return true;
}

void usage(const char *program) {
    std::cerr << "usage: " << program
              << " [-n N,N,...] [-j THREADS] [--out DIR] [--deltas D,D,...] [--hull H] [--permutation P]"
                 " [--tiebreaker T] [--validate] [seed]\n"
              << "  -n N,N,...    universe sizes (default " << DEFAULT_NS << ")\n"
              << "  -j THREADS    threads, one delta each at a time (default: one per core)\n"
              << "  --out DIR     directory holding the runs (default " << DEFAULT_OUT << "); the CSVs go to\n"
              << "                DIR/<run>, run = 1, 2, ... the next unused number\n"
              << "  --deltas D,D,...  the deltas, multiples of 0.5 (default " << DEFAULT_DELTAS << ")\n"
              << "  --hull H      what each segment keeps to test joins: scan (T0), vector (T1) or\n"
              << "                tree (T2, the default); the same segments with each, only the time differs\n"
              << "  --validate    check the index at every prefix against exp3's PMA and O'Rourke;\n"
              << "                writes no files\n"
              << "  --permutation P  the insertion order of 1..n, as exp1 (default uniform):\n"
              << "                uniform, zipf:R,s, zipf:R,s,d, blocks:b, bitrev[:p], probing\n"
              << "  --tiebreaker T  the best delta when several have the same qc: minlambda, the\n"
              << "                fewest segments (default), or mindelta, the smallest delta\n"
              << "  seed          random if omitted\n";
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

// --deltas as k = 2 * delta, ascending and distinct.
std::vector<int64_t> parse_deltas(const std::string &s, const char *program) {
    std::vector<int64_t> ks;
    std::stringstream stream(s);
    std::string token;
    while (std::getline(stream, token, ',')) {
        double k = 0;
        try {
            size_t used;
            k = 2 * std::stod(token, &used);
            if (used != token.size()) usage(program);
        } catch (const std::exception &) {
            usage(program);
        }
        if (!(k >= 1 && k <= double(gpla::MAX_K)) || k != std::floor(k)) {
            std::cerr << "delta " << token << " is not a positive multiple of 0.5\n";
            usage(program);
        }
        ks.push_back(int64_t(k));
    }
    if (ks.empty()) usage(program);
    std::sort(ks.begin(), ks.end());
    ks.erase(std::unique(ks.begin(), ks.end()), ks.end());
    return ks;
}

// The PMA's parameters, for meta.json.
std::string pma_json() {
    PMAParams p;
    std::ostringstream o;
    o << "{\"leaf_upper\": " << p.leaf_upper << ", \"root_upper\": " << p.root_upper
      << ", \"initial_capacity\": " << p.initial_capacity << ", \"growth\": \"lazy\"}";
    return o.str();
}

std::string delta_string(int64_t k) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", double(k) / 2);
    return buffer;
}

// One n's entry of meta.json's "inserts": per delta, the time and the work of
// its n inserts.
std::string inserts_json(const std::vector<DeltaRun> &runs) {
    std::ostringstream o;
    o << "{";
    for (size_t d = 0; d < runs.size(); ++d) {
        const DeltaRun &r = runs[d];
        o << (d ? ", " : "") << json_string(delta_string(r.k)) << ": {\"seconds\": " << r.seconds
          << ", \"rebuilt_points\": " << r.rebuilt_points << ", \"rebuilt_segments\": " << r.rebuilt_segments
          << ", \"splits\": " << r.splits << ", \"join_attempts\": " << r.join_attempts
          << ", \"joins\": " << r.joins << ", \"grows\": " << r.grows << "}";
    }
    return o.str() + "}";
}

int main(int argc, char **argv) {
    std::string sizes_argument = DEFAULT_NS, out_dir = DEFAULT_OUT, deltas_argument = DEFAULT_DELTAS;
    std::string hull = "tree";
    int threads = omp_get_num_procs();
    bool validate_only = false, has_seed = false;
    uint64_t seed = 0;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        bool takes_value = argument == "-n" || argument == "-j" || argument == "--out" || argument == "--deltas" ||
                           argument == "--hull" || argument == "--permutation" || argument == "--tiebreaker";
        if (takes_value && i + 1 >= argc) usage(argv[0]);

        if (argument == "-n") {
            sizes_argument = argv[++i];
        } else if (argument == "-j") {
            threads = int(parse_number(argv[++i], argv[0]));
            if (threads < 1) usage(argv[0]);
        } else if (argument == "--out") {
            out_dir = argv[++i];
        } else if (argument == "--deltas") {
            deltas_argument = argv[++i];
        } else if (argument == "--hull") {
            hull = argv[++i];
            if (hull != "vector" && hull != "scan" && hull != "tree") {
                std::cerr << "unknown hull: " << hull << " (scan, vector or tree)\n";
                usage(argv[0]);
            }
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
    std::vector<int64_t> ks = parse_deltas(deltas_argument, argv[0]);
    if (!order_fits(sizes)) return 2;

    if (!has_seed) seed = std::random_device{}();
    omp_set_num_threads(threads);
    std::cout << "seed: " << seed << "  threads: " << threads << "  permutation: " << ORDER.spec()
              << "  tiebreaker: " << tiebreaker_name() << "  hull: " << hull << std::endl;

    if (validate_only) {
        for (size_t n : sizes) {
            if (!validate(n, seed + n, ks)) return 1;
        }
        return 0;
    }

    out_dir = next_run_dir(out_dir);
    std::cout << "run directory: " << out_dir << std::endl;

    RunMeta meta;
    meta.experiment = "exp4";
    meta.path = out_dir + "/meta.json";
    meta.run = std::stoi(std::filesystem::path(out_dir).filename().string());
    meta.seed = seed;
    meta.sizes = sizes;
    meta.fixed_k = ks;
    meta.cost = "log2(delta) + log2(lambda), delta in PMA slots, lambda the GPLA's";
    std::string structure = "GPLA (src/GPLA), points (key, slot): " + hull +
                            " hull, no two neighbouring segments joinable";
    std::map<size_t, std::string> inserts;  // per n, inserts_json
    auto set_extra = [&] {
        std::string json = "{";
        for (const auto &[n, entry] : inserts) {
            json += (json.size() > 1 ? ",\n    " : "\n    ") + json_string(std::to_string(n)) + ": " + entry;
        }
        json += inserts.empty() ? "}" : "\n  }";
        meta.extra = {{"structure", json_string(structure)},
                      {"pma", pma_json()},
                      {"hull", json_string(hull == "scan"   ? "ScanHull"
                                           : hull == "tree" ? "TreeHull<" + std::to_string(gpla::TreeHull::LEAF_SIZE) + ">"
                                                            : "VectorHull")},
                      {"inserts", json}};
    };
    set_extra();
    meta.started = utc_now();
    for (int i = 0; i < argc; ++i) meta.command += (i ? " " : "") + std::string(argv[i]);
    // A drawn seed is not on the command line; record it so the command reruns
    // the same permutations.
    if (!has_seed) meta.command += " " + std::to_string(seed);
    meta.write();

    Progress progress;
    progress.started = std::chrono::steady_clock::now();
    progress.overall_total = 0;
    for (size_t n : sizes) progress.overall_total += uint64_t(n) * ks.size();

    for (size_t n : sizes) {
        std::string path = out_dir + "/exp4_n" + std::to_string(n) + ".csv";
        std::ofstream out(path);
        if (!out) {
            std::cerr << "could not write " << path << std::endl;
            return 1;
        }

        uint64_t n_seed = seed + n;
        progress.total = uint64_t(n) * ks.size();
        progress.done = 0;
        auto started = std::chrono::steady_clock::now();
        std::vector<DeltaRun> runs = run_n(n, n_seed, ks, hull, progress);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        progress.draw(n);
        std::fprintf(stderr, "\n");
        progress.overall_done_before += progress.total;

        out << "seed,n,t,capacity,k,L,cost,best\n";
        for (size_t t = 1; t <= n; ++t) {
            // Every delta's PMA holds the same layout: the keys and their order are the same.
            for (const DeltaRun &r : runs) {
                if (r.capacity[t] != runs[0].capacity[t]) {
                    std::cerr << "the PMAs differ at t=" << t << std::endl;
                    return 1;
                }
            }
            __int128 best_product = -1;
            int64_t best_k = 0;
            for (const DeltaRun &r : runs) {
                __int128 product = __int128(r.k) * r.segments[t];
                if (better(r.k, product, best_k, best_product)) {
                    best_product = product;
                    best_k = r.k;
                }
            }
            for (const DeltaRun &r : runs) {
                out << n_seed << ',' << n << ',' << t << ',' << r.capacity[t] << ',' << r.k << ',' << r.segments[t]
                    << ',' << query_complexity(r.segments[t], r.k) << ',' << (r.k == best_k ? 1 : 0) << '\n';
            }
        }
        out.close();

        double slowest = 0;
        for (const DeltaRun &r : runs) slowest = std::max(slowest, r.seconds);
        std::cout << "n=" << n << ": " << seconds << " s (slowest delta " << slowest << " s) -> " << path
                  << std::endl;

        meta.timing.push_back({n, seconds, double(ks.size()), ks.size()});
        inserts[n] = inserts_json(runs);
        set_extra();
        meta.write();
    }

    meta.finished = utc_now();
    meta.write();
    std::cout << "metadata -> " << meta.path << std::endl;
}
