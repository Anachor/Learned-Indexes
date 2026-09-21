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
//   query complexity = log2(delta) + log2(segment size), delta = k/2
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

// log2(delta) + log2(lambda). Minimising it is minimising k * L, which is what
// the search does, exactly, in integers.
double query_complexity(size_t L, int64_t k) { return std::log2(double(k) / 2) + std::log2(double(L)); }

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

// The insertion order of {1..n}, chosen with --permutation:
//
//   uniform      a uniformly random permutation (the default)
//   zipf:R,s     the keys split into R equal regions; each insert picks a region
//                with weight 1/rank^s among those not yet full, then a random
//                key in it. Region ranks are shuffled, so the hot regions sit
//                anywhere. Prefixes mix dense and sparse regions.
//   blocks:b     the keys in blocks of b consecutive keys; the blocks in random
//                order, the keys in each block in random order
//   probing      linear probing: pick a random key, and if it is already in,
//                take the next one up, wrapping from n to 1
//
// Uniform prefixes are random subsets, whose best fit is almost always a
// single segment; the others vary the key density along the prefix.
struct Order {
    std::string kind = "uniform";
    size_t regions = 0;  // zipf R
    double skew = 0;     // zipf s
    size_t block = 0;    // blocks b

    // The spec as given back to --permutation: "zipf:16,1".
    std::string spec() const {
        char buffer[64];
        if (kind == "zipf") std::snprintf(buffer, sizeof buffer, "zipf:%zu,%g", regions, skew);
        else if (kind == "blocks") std::snprintf(buffer, sizeof buffer, "blocks:%zu", block);
        else return kind;
        return buffer;
    }
};

// Parsed from --permutation in main, before anything draws a permutation.
Order ORDER;

// Parses a --permutation spec; false if it is not one.
bool parse_order(const std::string &s, Order &order) {
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
        if (kind == "zipf") {
            size_t comma = args.find(',');
            if (comma == std::string::npos) return false;
            std::string r = args.substr(0, comma), sk = args.substr(comma + 1);
            size_t used_r, used_s;
            long long regions = std::stoll(r, &used_r);
            double skew = std::stod(sk, &used_s);
            if (used_r != r.size() || used_s != sk.size() || regions < 1 || !(skew >= 0)) return false;
            order.regions = size_t(regions);
            order.skew = skew;
            return true;
        }
    } catch (const std::exception &) {
    }
    return false;
}

// The insertion order of {1..n} for this seed, under ORDER. run_n, validate and
// simulate all draw it from here, so a simulation sees exactly the keys a run
// saw. The uniform order is the same shuffle as before the other orders
// existed, so earlier runs reproduce.
std::vector<int64_t> make_permutation(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<int64_t> permutation;
    permutation.reserve(n);

    if (ORDER.kind == "zipf") {
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
// lower qc and the search stops. In k: stop when k + 1 >= k_best * L_best.
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
        if (best_product < 0 || product < best_product) {
            best_product = product;
            best_k = k;
        }
        if (__int128(k + 1) >= best_product) break;
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

// The commit this binary was built from, baked in by experiments/exp1/build.sh.
// A plain g++ build leaves it unknown.
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#ifndef GIT_DIRTY
#define GIT_DIRTY "unknown"  // "true" / "false" from build.sh
#endif

std::string json_string(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
            out += buffer;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string utc_now() {
    std::time_t now = std::time(nullptr);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buffer;
}

// DIR/<run>/meta.json: what the run is, so its numbers can be interpreted and
// reproduced. Rewritten after every n, so an interrupted run says how far it
// got: status stays "running" unless the run finished.
struct RunMeta {
    std::string path, command, started, finished;
    int run = 0;
    uint64_t seed = 0;
    std::vector<size_t> sizes;
    struct Timing { size_t n; double seconds, mean_evaluations; size_t max_evaluations; };
    std::vector<Timing> timing;

    void write() const {
        std::ostringstream o;
        o << "{\n"
          << "  \"experiment\": \"exp1\",\n"
          << "  \"run\": " << run << ",\n"
          << "  \"status\": \"" << (finished.empty() ? "running" : "complete") << "\",\n"
          << "  \"seed\": " << seed << ",\n"
          << "  \"seed_rule\": \"each n uses seed + n\",\n"
          << "  \"permutation\": " << json_string(ORDER.spec()) << ",\n"
          << "  \"ns\": [";
        for (size_t i = 0; i < sizes.size(); ++i) o << (i ? ", " : "") << sizes[i];
        o << "],\n  \"fixed_deltas\": [";
        for (size_t i = 0; i < FIXED_K.size(); ++i) o << (i ? ", " : "") << double(FIXED_K[i]) / 2;
        o << "],\n"
          << "  \"cost\": \"log2(delta) + log2(lambda)\",\n"
          << "  \"commit\": " << json_string(GIT_COMMIT) << ",\n"
          << "  \"dirty\": " << (std::string(GIT_DIRTY) == "unknown" ? "null" : GIT_DIRTY) << ",\n"
          << "  \"command\": " << json_string(command) << ",\n"
          << "  \"started\": " << json_string(started) << ",\n"
          << "  \"finished\": " << (finished.empty() ? "null" : json_string(finished)) << ",\n"
          << "  \"timing\": {";
        for (size_t i = 0; i < timing.size(); ++i) {
            const Timing &t = timing[i];
            o << (i ? "," : "") << "\n    \"" << t.n << "\": {\"seconds\": " << t.seconds
              << ", \"mean_evaluations\": " << t.mean_evaluations
              << ", \"max_evaluations\": " << t.max_evaluations << "}";
        }
        o << (timing.empty() ? "}" : "\n  }") << "\n}\n";

        // Write and rename, so a reader never sees half a file.
        std::string temporary = path + ".tmp";
        std::ofstream(temporary) << o.str();
        std::filesystem::rename(temporary, path);
    }
};

void usage(const char *program) {
    std::cerr << "usage: " << program << " [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--validate] [seed]\n"
              << "       " << program << " --simulate T [--json | --segments K] [--permutation P] -n N seed\n"
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
              << "              blocks:b    blocks of b consecutive keys, blocks and keys in\n"
              << "                          random order\n"
              << "              probing     linear probing: a random key, or the next free one\n"
              << "                          above it, wrapping from n to 1\n"
              << "              --simulate needs the same P as the run it reproduces\n"
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
                           argument == "--permutation";
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
              << "  permutation: " << ORDER.spec() << std::endl;

    if (validate_only) {
        for (size_t n : sizes) {
            if (!validate(n, seed + n)) return 1;
        }
        return 0;
    }

    out_dir = next_run_dir(out_dir);
    std::cout << "run directory: " << out_dir << std::endl;

    RunMeta meta;
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
