#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "../src/ORourke/brute_orourke.hpp"
#include "../src/ORourke/pgm_orourke.hpp"
#include "../src/ORourke/zlw_orourke.hpp"

uint64_t seed;

// Stress test defaults, overridable from the command line.
const int DEFAULT_ITERATIONS = 10000;
const int DEFAULT_MAXN = 100;
const int DEFAULT_MAXD = 10;

template <typename T = int64_t>
struct TestCase {
    T delta;
    std::vector<T> x;  // strictly increasing
    std::vector<T> y;

    // Throws if the test case is malformed.
    void validate() const {
        if (delta < 0) {
            throw std::invalid_argument("delta must be non-negative");
        }
        if (x.size() != y.size()) {
            throw std::invalid_argument("x and y must have the same size");
        }
        for (size_t i = 1; i < x.size(); ++i) {
            if (x[i - 1] >= x[i]) {
                throw std::invalid_argument("x must be strictly increasing");
            }
        }
    }
};

// How y is generated.
enum class YPattern {
    Rank,    // 0, 1, 2, ...
    Gaps,    // increasing, random gaps of 1..10 (like PMA slots)
    Flat,    // non-decreasing, steps of 0 or 1
    Random,  // uniform in [0, 20n], no order
};

const char *name(YPattern p) {
    switch (p) {
        case YPattern::Rank: return "rank";
        case YPattern::Gaps: return "gaps";
        case YPattern::Flat: return "flat";
        default: return "random";
    }
}

template <typename T = int64_t>
TestCase<T> generate_testcase(size_t n, T delta,
                              T min_key = std::numeric_limits<T>::min(),
                              T max_key = std::numeric_limits<T>::max(),
                              YPattern pattern = YPattern::Rank) {
    using U = std::make_unsigned_t<T>;
    if (max_key < min_key || U(max_key) - U(min_key) < n - 1) {
        throw std::invalid_argument("key range too small for n distinct keys");
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<T> dist(min_key, max_key);

    std::unordered_set<T> seen;
    std::vector<T> keys;
    keys.reserve(n);
    while (keys.size() < n) {
        T k = dist(rng);
        if (seen.insert(k).second) {
            keys.push_back(k);
        }
    }
    std::sort(keys.begin(), keys.end());

    std::vector<T> ys(n);
    for (size_t i = 0; i < n; ++i) {
        T prev = i > 0 ? ys[i - 1] : T(0);
        switch (pattern) {
            case YPattern::Rank: ys[i] = T(i); break;
            case YPattern::Gaps: ys[i] = i > 0 ? prev + T(1 + rng() % 10) : T(0); break;
            case YPattern::Flat: ys[i] = prev + T(rng() % 2); break;
            case YPattern::Random: ys[i] = T(rng() % (20 * n + 1)); break;
        }
    }

    TestCase<T> tc = {delta, keys, ys};
    tc.validate();
    return tc;
}

// Allowed floating-point error when checking lines.
const long double LINE_EPS = 1e-9;

// Result of feeding a test case to an O'Rourke implementation point by point.
struct RunResult {
    size_t segments = 0;
    // Largest |line(x) - y| - delta over the points each line covers, for every
    // closed line and for current() after every add_point. <= 0 means valid.
    long double line_excess = 0;
};

template <typename T>
RunResult run(ORourke<T> &orourke, const TestCase<T> &tc) {
    tc.validate();
    RunResult r;
    if (tc.x.empty()) return r;

    // Updates line_excess for points [begin, end) against line l.
    auto check = [&](const Line &l, size_t begin, size_t end) {
        for (size_t k = begin; k < end; ++k) {
            long double err = std::abs(l((long double)tc.x[k]) - (long double)tc.y[k]) - tc.delta;
            r.line_excess = std::max(r.line_excess, err);
        }
    };

    orourke.reset(tc.delta);
    r.segments = 1;
    size_t begin = 0;
    for (size_t i = 0; i < tc.x.size(); ++i) {
        if (auto closed = orourke.add_point(tc.x[i], tc.y[i])) {
            check(*closed, begin, i);
            begin = i;
            ++r.segments;
        }
        check(orourke.current(), begin, i + 1);
    }
    return r;
}

// Checks segmentation(x) against the brute-force segment count for y = index,
// and that the segments cover 0..n-1 in order without gaps.
template <typename T>
bool segmentation_ok(ORourke<T> &orourke, BruteORourke<T> &brute, const TestCase<T> &tc) {
    TestCase<T> ranks = tc;
    for (size_t i = 0; i < ranks.y.size(); ++i) ranks.y[i] = T(i);

    orourke.reset(tc.delta);
    auto segments = orourke.segmentation(tc.x);
    if (segments.size() != run(brute, ranks).segments) return false;

    size_t next = 0;
    for (const auto &s : segments) {
        if (s.begin != next || s.end <= s.begin) return false;
        next = s.end;
    }
    return next == tc.x.size();
}

// A random test case for the stress tests (sets the global seed to the case's
// seed), and a one-line description of it.
std::pair<TestCase<int64_t>, std::string> random_testcase(std::mt19937_64 &rng, int MAXN, int MAXD) {
    seed = rng();
    size_t n = 1 + rng() % MAXN;
    int64_t delta = rng() % MAXD;
    int64_t max_key;
    switch (rng() % 4) {
        case 0: max_key = int64_t(n) - 1; break;       // dense: keys 0..n-1
        case 1: max_key = 2 * int64_t(n); break;
        case 2: max_key = 100 * int64_t(n); break;
        default: max_key = std::numeric_limits<int64_t>::max(); break;
    }
    YPattern pattern = YPattern(rng() % 4);

    std::string desc = "seed " + std::to_string(seed) + ": n=" + std::to_string(n) +
                       " delta=" + std::to_string(delta) + " max_key=" + std::to_string(max_key) +
                       " y=" + name(pattern);
    return {generate_testcase<int64_t>(n, delta, 0, max_key, pattern), desc};
}

// Prints x and y of a test case.
void print_points(const TestCase<int64_t> &tc) {
    std::cout << "  x:";
    for (auto v : tc.x) std::cout << ' ' << v;
    std::cout << "\n  y:";
    for (auto v : tc.y) std::cout << ' ' << v;
    std::cout << std::endl;
}

// Tests an O'Rourke implementation on random test cases: segment count equals
// BruteORourke's, every line is within delta of its points, and
// segmentation() is correct. Returns false and prints the failing case on the
// first failure.
// verbosity 1: print the results for every case. 2: also print x and y.
bool stress_test(ORourke<int64_t> &orourke, const std::string &impl, int iterations,
                 int verbosity, int MAXN, int MAXD) {
    BruteORourke<int64_t> brute(0);
    std::mt19937_64 rng(seed);
    for (int it = 0; it < iterations; ++it) {
        auto [tc, desc] = random_testcase(rng, MAXN, MAXD);
        RunResult expected = run(brute, tc);
        RunResult actual = run(orourke, tc);
        bool seg_ok = segmentation_ok(orourke, brute, tc);
        std::string result = " brute=" + std::to_string(expected.segments) + " " + impl + "=" +
                             std::to_string(actual.segments) + " line_excess=" +
                             std::to_string(double(actual.line_excess)) +
                             " segmentation=" + (seg_ok ? "ok" : "wrong");
        if (verbosity >= 1) std::cout << "case " << it << " (" << desc << ")" << result << std::endl;
        if (verbosity >= 2) print_points(tc);

        const char *failure = expected.segments != actual.segments ? "MISMATCH"
                              : actual.line_excess > LINE_EPS      ? "LINE ERROR"
                              : !seg_ok                            ? "SEGMENTATION ERROR"
                                                                   : nullptr;
        if (failure) {
            std::cout << impl << ": " << failure << " on case " << desc << result << std::endl;
            return false;
        }
    }
    std::cout << impl << ": all " << iterations << " cases passed" << std::endl;
    return true;
}

void usage(const char *prog) {
    std::cerr << "usage: " << prog << " [-v | -vv] [-i iterations] [-n MAXN] [-d MAXD] [seed]\n"
              << "  -v          print the results for every case\n"
              << "  -vv         also print x and y\n"
              << "  -i N        number of cases (default " << DEFAULT_ITERATIONS << ")\n"
              << "  -n N        n is drawn from 1..N (default " << DEFAULT_MAXN << ")\n"
              << "  -d N        delta is drawn from 0..N-1 (default " << DEFAULT_MAXD << ")\n"
              << "  seed        random if omitted\n";
    std::exit(1);
}

// Parses a whole string as a non-negative integer, or prints usage.
uint64_t parse_number(const char *s, const char *prog) {
    try {
        size_t used;
        std::string str = s;
        if (str.empty() || str[0] == '-') usage(prog);
        uint64_t v = std::stoull(str, &used);
        if (used != str.size()) usage(prog);
        return v;
    } catch (const std::exception &) {
        usage(prog);
    }
    return 0;
}

int main(int argc, char **argv) {
    int verbosity = 0;
    int iterations = DEFAULT_ITERATIONS, MAXN = DEFAULT_MAXN, MAXD = DEFAULT_MAXD;
    bool has_seed = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool takes_value = arg == "-i" || arg == "-n" || arg == "-d";
        if (takes_value && i + 1 >= argc) usage(argv[0]);

        if (arg == "-v") {
            verbosity = 1;
        } else if (arg == "-vv") {
            verbosity = 2;
        } else if (arg == "-i") {
            iterations = int(parse_number(argv[++i], argv[0]));
        } else if (arg == "-n") {
            MAXN = int(parse_number(argv[++i], argv[0]));
        } else if (arg == "-d") {
            MAXD = int(parse_number(argv[++i], argv[0]));
        } else if (!has_seed) {
            seed = parse_number(argv[i], argv[0]);
            has_seed = true;
        } else {
            usage(argv[0]);
        }
    }
    if (iterations < 1 || MAXN < 1 || MAXD < 1) usage(argv[0]);
    if (!has_seed) seed = std::random_device{}();
    std::cout << "seed: " << seed << std::endl;

    // Every test runs on the same cases.
    uint64_t start_seed = seed;
    PgmORourke<int64_t> pgm(0);
    ZlwORourke<int64_t> zlw(0);
    bool ok = stress_test(pgm, "pgm", iterations, verbosity, MAXN, MAXD);
    seed = start_seed;
    ok &= stress_test(zlw, "zlw", iterations, verbosity, MAXN, MAXD);
    return ok ? 0 : 1;
}
