// O'Rourke (gpla::Fitter), which everything else relies on, against
// BruteORourke on random points, and fraction_less against products.
//
//   g++-11 -std=c++20 -O3 tests/fitter.cpp -o tests/fitter
//   ./tests/fitter [-i iterations] [-n MAXN] [seed]

#include "gpla_common.hpp"

// Greedy segments: the same as BruteORourke's, and each line fits its points.
const char *greedy_problem(const std::vector<Pt> &points, int64_t k) {
    Fitter f(k);
    std::vector<size_t> starts;
    auto line_fits = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            if (!f.line().within(points[i], k)) return false;
        }
        return true;
    };
    for (size_t i = 0; i < points.size(); ++i) {
        if (!starts.empty() && f.add(points[i])) continue;
        if (!starts.empty() && !line_fits(starts.back(), i)) return "a line misses its points";
        f.reset();
        f.add(points[i]);
        starts.push_back(i);
    }
    if (!line_fits(starts.back(), points.size())) return "the last line misses its points";
    if (starts != brute_starts(points, k)) return "segments differ from BruteORourke";
    return nullptr;
}

// The fewest segments, trying every split: greedy must find as few.
size_t fewest_segments(const std::vector<Pt> &points, int64_t k) {
    std::vector<size_t> best(points.size() + 1, points.size());
    best[0] = 0;
    for (size_t j = 1; j <= points.size(); ++j) {
        for (size_t i = 0; i < j; ++i) {
            if (best[i] + 1 < best[j] && brute_fits({points.begin() + long(i), points.begin() + long(j)}, k))
                best[j] = best[i] + 1;
        }
    }
    return best.back();
}

// Skipping the points that do not fit: each add must agree with brute force,
// so a failed add has to leave the fitter as it was.
const char *skip_problem(const std::vector<Pt> &points, int64_t k) {
    Fitter f(k);
    std::vector<Pt> kept;
    for (const Pt &p : points) {
        kept.push_back(p);
        bool added = f.add(p);
        if (added != brute_fits(kept, k)) return added ? "add took a point that does not fit" : "add refused one that fits";
        if (!added) kept.pop_back();
    }
    for (const Pt &p : kept) {
        if (!f.line().within(p, k)) return "the line misses a kept point";
    }
    return nullptr;
}

// fraction_less against a*d < c*b, also with both terms of each fraction scaled
// by up to 2^100, the size T2 compares.
const char *fraction_problem(std::mt19937_64 &rng) {
    for (int t = 0; t < 50; ++t) {
        i128 a = i128(rng() % 2001) - 1000, b = 1 + i128(rng() % 1000);
        i128 c = i128(rng() % 2001) - 1000, d = 1 + i128(rng() % 1000);
        if (rng() % 4 == 0) {  // equal fractions
            c = 3 * a;
            d = 3 * b;
        }
        bool less = a * d < c * b;
        if (gpla::fraction_less(a, b, c, d) != less) return "fraction_less";
        i128 s = 1 + (i128(rng()) << (rng() % 37)), u = 1 + (i128(rng()) << (rng() % 37));
        if (gpla::fraction_less(a * s, b * s, c * u, d * u) != less) return "fraction_less on large terms";
    }
    return nullptr;
}

int main(int argc, char **argv) {
    Args args = parse_args(argc, argv);
    std::mt19937_64 rng(args.seed);
    int failures = 0;
    for (int it = 0; it < args.iterations; ++it) {
        size_t n = 1 + rng() % std::min<size_t>(args.max_n, 255);
        int64_t k = KS[rng() % KS.size()];
        std::vector<Pt> points = random_points(rng, n);
        failures += failed("case " + std::to_string(it) + " (n=" + std::to_string(n) + ", k=" + std::to_string(k) + ")",
                           [&]() -> const char * {
                               if (const char *p = greedy_problem(points, k)) return p;
                               if (n <= 10 && brute_starts(points, k).size() != fewest_segments(points, k))
                                   return "greedy is not the fewest";
                               if (n <= 40) {
                                   if (const char *p = skip_problem(points, k)) return p;
                               }
                               return fraction_problem(rng);
                           });
    }
    std::cout << "fitter: " << args.iterations << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
