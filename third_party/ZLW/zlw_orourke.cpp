#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Experiment 1: packed sorted array + optimal piecewise-linear approximation
// ============================================================================
//
// Problem being measured
// ----------------------
// Start from a random permutation of the key universe {1,...,N}.  For a prefix
// of length t, sort the t keys and regard the packed-array rank of the i-th key
// as its y-coordinate:
//
//      point i = (sorted_key[i], i).
//
// For a fixed vertical error delta >= 1, compute the *minimum possible number*
// L(delta) of line segments needed so that every point assigned to a segment is
// within vertical distance delta of that line.
//
// The Experiment-1 model/search-complexity proxy is
//
//      C(delta) = log2 L(delta) + log2 delta
//               = log2(L(delta) * delta).
//
// For the "optimized envelope" we then choose the best integer delta:
//
//      C* = min_delta C(delta).
//
// This file contains BOTH algorithmic parts:
//   1. a streaming O'Rourke-style optimal PLA segment counter for fixed delta;
//   2. an exact optimizer over integer delta using monotonicity + branch/bound.
//
// No PMA is involved here.  Experiment 1 deliberately uses compact ranks as the
// y-coordinate and is the static packed-array baseline.
// ============================================================================

using Real = long double;
using Key = std::int64_t;

// A data point (x,y).  x is a key and y is its compact rank in Experiment 1.
struct Point {
    Real x{}, y{};
};

// Basic vector subtraction, used by orientation tests below.
static Point operator-(const Point& a, const Point& b) {
    return {a.x - b.x, a.y - b.y};
}

// 2D cross product of vectors a and b.
// Its sign is the usual left/right-turn orientation predicate.
static Real cross(const Point& a, const Point& b) {
    return a.x * b.y - a.y * b.x;
}

// ============================================================================
// FastPLAWorkspace
// ============================================================================
//
// Streaming feasibility test for ONE segment under a fixed vertical error eps.
//
// For each original point p=(x,y), any approximating line f(x) must satisfy
//
//      y - eps <= f(x) <= y + eps.
//
// Geometrically, p therefore contributes two boundary points:
//
//      lo = (x, y-eps),    up = (x, y+eps).
//
// A single line can approximate the entire current block iff there exists a
// slope/intercept whose graph passes through all of these vertical slabs.
//
// The standard O'Rourke/PGM streaming construction maintains the feasible slope
// wedge by keeping convex chains of lower and upper slab endpoints.  Each point
// is processed amortized O(1), so counting all optimal greedy segments is O(n)
// for a fixed eps.
//
// IMPORTANT: The code does not explicitly materialize a slope interval as two
// floating-point slopes.  Instead it stores four support points `rect_` that
// define its two extremal slopes.  This avoids divisions and compares slopes by
// cross-multiplication.
// ============================================================================
class FastPLAWorkspace {
    // A slope represented as dy/dx without division.  Since x is strictly
    // increasing, dx is positive, so cross-multiplication preserves order.
    struct Slope {
        Real dx{}, dy{};

        friend bool operator<(const Slope& a, const Slope& b) {
            // a.dy/a.dx < b.dy/b.dx
            return a.dy * b.dx < a.dx * b.dy;
        }
        friend bool operator>(const Slope& a, const Slope& b) {
            return a.dy * b.dx > a.dx * b.dy;
        }
    };

    // Current allowed vertical error.
    Real eps_ = 1;

    // Convex chains of lower/upper slab endpoints.  These are the geometric
    // support structures from which the tightest feasible-slope constraints are
    // recovered when a new point narrows the wedge.
    std::vector<Point> lower_, upper_;

    // We never need to erase old prefixes of the chains physically.  Instead,
    // these indices mark the first still-relevant support point.
    std::size_t lower_start_ = 0;
    std::size_t upper_start_ = 0;

    // Number of original points currently accepted into this one segment.
    std::size_t count_ = 0;

    // The four support points defining the current feasible slope wedge.
    //
    // The two limiting slopes are
    //
    //      s1 = slope(rect_[0] -> rect_[2])
    //      s2 = slope(rect_[1] -> rect_[3]).
    //
    // Their exact semantic roles evolve with the support hull updates, but the
    // invariant is that every feasible segment slope lies between these limits.
    Point rect_[4]{};

    // Input x's must be strictly increasing for the streaming geometric method.
    Real last_x_ = -std::numeric_limits<Real>::infinity();

    // Return the slope from a to b in division-free representation.
    static Slope delta(const Point& a, const Point& b) {
        return {b.x - a.x, b.y - a.y};
    }

    // Signed orientation of triangle (o,a,b).
    // Positive / negative signs identify turns and are used to maintain convex
    // hull chains by deleting obsolete points from the back.
    static Real orient(const Point& o, const Point& a, const Point& b) {
        return cross(a - o, b - o);
    }

public:
    // Reserve memory once so repeated fixed-delta evaluations do not repeatedly
    // allocate.  This matters because the delta optimizer invokes the segment
    // counter many times on the same prefix.
    void reserve(std::size_t n) {
        lower_.reserve(n);
        upper_.reserve(n);
    }

    // Start a fresh candidate segment for a new value of eps.
    void reset(Real eps) {
        eps_ = eps;
        lower_.clear();
        upper_.clear();
        lower_start_ = upper_start_ = count_ = 0;
        last_x_ = -std::numeric_limits<Real>::infinity();
    }

    // The four support points of the last segment with at least two points.
    const Point* rect() const { return rect_; }

    // Try to append one point to the current segment.
    //
    // Return value:
    //   true  = there still exists some line fitting ALL points seen since the
    //           last reset within vertical error eps_;
    //   false = adding this point makes such a line impossible.
    //
    // On false, count_ is reset to 0.  The caller then starts a new segment at
    // this same point.  That greedy "longest feasible prefix" rule is optimal
    // for the minimum-number-of-segments PLA problem.
    bool add(Point p) {
        // O'Rourke's streaming geometry assumes sorted, distinct x-coordinates.
        if (count_ && !(p.x > last_x_))
            throw std::runtime_error("x must be strictly increasing");
        last_x_ = p.x;

        // Vertical slab boundaries contributed by p.
        Point up{p.x, p.y + eps_};
        Point lo{p.x, p.y - eps_};

        // One point can always be fit.  Initialize both hull chains with its
        // upper/lower slab endpoints.
        if (count_ == 0) {
            rect_[0] = up;
            rect_[1] = lo;
            upper_.push_back(up);
            lower_.push_back(lo);
            count_ = 1;
            return true;
        }

        // Any two points with distinct x can also be fit for eps>=0.  These two
        // slabs establish the initial feasible slope wedge.
        if (count_ == 1) {
            rect_[2] = lo;
            rect_[3] = up;
            upper_.push_back(up);
            lower_.push_back(lo);
            count_ = 2;
            return true;
        }

        // Current lower/upper limiting slopes of the feasible wedge.
        const Slope s1 = delta(rect_[0], rect_[2]);
        const Slope s2 = delta(rect_[1], rect_[3]);

        // --------------------------------------------------------------------
        // Feasibility rejection test.
        // --------------------------------------------------------------------
        // If the new upper endpoint falls below the lower admissible slope, or
        // the new lower endpoint lies above the upper admissible slope, the
        // slope wedge becomes empty.  Therefore no single line can fit the old
        // points together with p.
        if (delta(rect_[2], up) < s1 || delta(rect_[3], lo) > s2) {
            count_ = 0;
            return false;
        }

        // --------------------------------------------------------------------
        // Tighten one side of the feasible wedge when the new upper endpoint
        // creates a stricter constraint.
        // --------------------------------------------------------------------
        if (delta(rect_[1], up) < s2) {
            // Search the active portion of the lower convex chain for the
            // support point giving the tightest tangent from `up`.
            std::size_t best = lower_start_;
            Slope best_slope = delta(up, lower_[best]);
            for (std::size_t i = lower_start_ + 1; i < lower_.size(); ++i) {
                Slope v = delta(up, lower_[i]);
                if (v > best_slope) break;
                best_slope = v;
                best = i;
            }

            // Update support points for this wedge boundary.
            rect_[1] = lower_[best];
            rect_[3] = up;
            lower_start_ = best;

            // Insert `up` into the upper convex chain, removing points that can
            // no longer be tangent/support points.  Each old point can be popped
            // only once, giving amortized O(1) update time.
            while (upper_.size() >= upper_start_ + 2 &&
                   orient(upper_[upper_.size()-2], upper_.back(), up) <= 0) {
                upper_.pop_back();
            }
            upper_.push_back(up);
        }

        // --------------------------------------------------------------------
        // Symmetric tightening when the new lower endpoint creates a stricter
        // constraint on the other side of the feasible wedge.
        // --------------------------------------------------------------------
        if (delta(rect_[0], lo) > s1) {
            // Find the appropriate tangent/support point on the upper chain.
            std::size_t best = upper_start_;
            Slope best_slope = delta(lo, upper_[best]);
            for (std::size_t i = upper_start_ + 1; i < upper_.size(); ++i) {
                Slope v = delta(lo, upper_[i]);
                if (v < best_slope) break;
                best_slope = v;
                best = i;
            }

            rect_[0] = upper_[best];
            rect_[2] = lo;
            upper_start_ = best;

            // Maintain convexity of the lower chain.
            while (lower_.size() >= lower_start_ + 2 &&
                   orient(lower_[lower_.size()-2], lower_.back(), lo) >= 0) {
                lower_.pop_back();
            }
            lower_.push_back(lo);
        }

        ++count_;
        return true;
    }
};

// Result returned by the integer-delta optimizer.
struct OracleResult {
    std::size_t L = 0;          // optimal number of PLA segments at chosen delta
    std::uint64_t delta = 1;    // chosen integer error parameter
    double C = 0;               // log2(L * delta)
    std::size_t evals = 0;      // how many distinct delta values were evaluated
};

// ============================================================================
// IntegerDeltaOracle
// ============================================================================
//
// For one fixed set of n sorted points, this class solves
//
//      min_{1 <= delta <= max_delta} delta * L(delta),
//
// which is equivalent to minimizing log2 L + log2 delta.
//
// Key structural fact:
//      L(delta) is non-increasing as delta grows.
//
// Larger error can never require MORE segments.  This monotonicity gives a
// rigorous lower bound over an interval [lo,hi]:
//
//      for any d in [lo,hi],
//          L(d) >= L(hi)  and  d >= lo,
//
//      hence
//          d * L(d) >= lo * L(hi).
//
// If that lower bound cannot beat the best product already found, we can prune
// the whole interval without checking every integer delta.
// ============================================================================
class IntegerDeltaOracle {
    FastPLAWorkspace w_;

    // Count the minimum number of segments needed at one fixed integer d.
    //
    // The greedy construction is:
    //   - extend the current segment as far as feasibility permits;
    //   - when the next point makes it infeasible, terminate the segment just
    //     before that point and start the next segment at that point.
    //
    // For this one-dimensional ordered PLA problem, the longest-feasible-prefix
    // greedy rule yields the minimum possible number of segments.
    template<class Getter>
    std::size_t count_segments(std::size_t n, Getter& get, std::uint64_t d) {
        if (n == 0) return 0;

        w_.reset((Real)d);
        std::size_t L = 1;  // nonempty input begins with one segment

        for (std::size_t i = 0; i < n; ++i) {
            Point p = get(i);

            if (!w_.add(p)) {
                // p is the first point that does NOT fit in the previous
                // segment.  Therefore that previous segment is maximal.
                ++L;

                // Start the next segment at p itself.
                w_.reset((Real)d);
                bool ok = w_.add(p);
                assert(ok);  // a one-point segment is always feasible
            }
        }
        return L;
    }

public:
    explicit IntegerDeltaOracle(std::size_t n = 0) {
        w_.reserve(n);
    }

    // Exactly optimize the Experiment-1 objective over all INTEGER deltas in
    // [1,max_delta].  "Exact" means that pruning is based on rigorous lower
    // bounds; it is not a heuristic grid search.
    template<class Getter>
    OracleResult optimize(std::size_t n, Getter get, std::uint64_t max_delta) {
        max_delta = std::max<std::uint64_t>(1, max_delta);

        // Memoize L(d), since recursive branch-and-bound may touch the same
        // endpoint/midpoint multiple times.
        std::unordered_map<std::uint64_t,std::size_t> cache;
        std::size_t evals = 0;

        auto eval = [&](std::uint64_t d) {
            auto it = cache.find(d);
            if (it != cache.end()) return it->second;

            std::size_t L = count_segments(n, get, d);
            cache.emplace(d, L);
            ++evals;
            return L;
        };

        // Initialize the incumbent at delta=1.
        std::uint64_t best_d = 1;
        std::size_t best_L = eval(1);

        // We minimize L*delta directly, because log2 is strictly increasing:
        //
        //      argmin log2(L*delta) = argmin L*delta.
        long double best_product = (long double)best_L;

        // Update incumbent, with deterministic tie-breaking toward smaller
        // delta and then smaller L.
        auto consider = [&](std::uint64_t d, std::size_t L) {
            long double prod = (long double)d * (long double)L;
            if (prod < best_product ||
                (prod == best_product &&
                 (d < best_d || (d == best_d && L < best_L)))) {
                best_product = prod;
                best_d = d;
                best_L = L;
            }
        };

        // Also evaluate the far endpoint so the first recursive interval knows
        // L(lo) and L(hi).
        std::size_t Lmax = eval(max_delta);
        consider(max_delta, Lmax);

        // Recursive exact branch-and-bound over an integer interval [lo,hi].
        std::function<void(std::uint64_t,std::uint64_t,
                           std::size_t,std::size_t)> solve;

        solve = [&](std::uint64_t lo, std::uint64_t hi,
                    std::size_t Llo, std::size_t Lhi) {
            // Endpoints may themselves be optimal.
            consider(lo, Llo);
            consider(hi, Lhi);

            // Nothing strictly inside the interval.
            if (lo + 1 >= hi) return;

            // If L is the same at both endpoints, monotonicity implies L is
            // constant everywhere in between.  Since d only increases, the
            // smallest product on this interval is attained at lo, already
            // considered above.
            if (Llo == Lhi) return;

            // Rigorous interval lower bound:
            //
            //     every d in [lo,hi] has d >= lo,
            //     and monotonicity gives L(d) >= L(hi)=Lhi,
            //
            // so d*L(d) >= lo*Lhi.
            long double lower_bound =
                (long double)Lhi * (long double)lo;

            // If even this optimistic bound cannot improve the incumbent, the
            // entire interval is safely discarded.
            if (lower_bound > best_product ||
                (lower_bound == best_product && lo >= best_d)) {
                return;
            }

            // Otherwise inspect the midpoint and recurse on the two halves.
            std::uint64_t mid = lo + (hi - lo)/2;
            std::size_t Lmid = eval(mid);
            consider(mid, Lmid);

            solve(lo, mid, Llo, Lmid);
            solve(mid, hi, Lmid, Lhi);
        };

        // Note: best_L may still equal L(1), which is exactly what is required
        // for the left endpoint of the initial interval.
        solve(1, max_delta, eval(1), Lmax);

        return {
            best_L,
            best_d,
            std::log2((double)best_product),
            evals
        };
    }
};

// ============================================================================
// Experiment helpers
// ============================================================================

// Generate the insertion order: a uniformly shuffled permutation of [1,N].
// Using mt19937_64 with an explicit seed makes every experiment reproducible.
static std::vector<Key> permutation(std::size_t N, std::uint64_t seed) {
    std::vector<Key> p(N);
    std::iota(p.begin(), p.end(), Key{1});

    std::mt19937_64 gen(seed);
    std::shuffle(p.begin(), p.end(), gen);
    return p;
}

// Parse a command-line list such as
//
//      "0.10,0.25,0.50,0.75,0.90"
//
// into floating-point prefix fractions.
static std::vector<double> parse_fractions(const std::string& s) {
    std::vector<double> out;
    std::stringstream ss(s);
    std::string tok;

    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(std::stod(tok));
    }
    return out;
}

// ============================================================================
// Main experiment driver
// ============================================================================
int main(int argc, char** argv) {
    try {
        // Defaults used by the scaling harness.
        std::size_t N = 4096;
        std::uint64_t seed = 1;
        std::string out_path = "scaling_one.csv";

        // These are fractions of the insertion process.  t = round(c*N).
        // We intentionally avoid using only t=N because at t=N the complete set
        // {1,...,N} has a perfectly linear rank function and is therefore a
        // degenerate/easy endpoint.
        std::vector<double> fractions = {
            .01,.02,.05,.10,.20,.30,.40,.50,.60,.70,.80,.90,.95,.98,.99
        };

        // --------------------------------------------------------------------
        // Command-line parsing
        // --------------------------------------------------------------------
        // Supported flags:
        //   --n N
        //   --seed S
        //   --out FILE.csv
        //   --fractions c1,c2,c3,...
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];

            auto need = [&](const char* flag) {
                if (i + 1 >= argc)
                    throw std::runtime_error(std::string("missing ") + flag);
                return std::string(argv[++i]);
            };

            if (a == "--n") {
                N = std::stoull(need("--n"));
            } else if (a == "--seed") {
                seed = std::stoull(need("--seed"));
            } else if (a == "--out") {
                out_path = need("--out");
            } else if (a == "--fractions") {
                fractions = parse_fractions(need("--fractions"));
            } else {
                throw std::runtime_error("unknown argument: " + a);
            }
        }

        // One random insertion order is generated per (N,seed) job.
        auto perm = permutation(N, seed);

        // Reuse one oracle/workspace across all sampled prefixes in this job.
        IntegerDeltaOracle oracle(N);

        std::ofstream out(out_path);
        if (!out)
            throw std::runtime_error("could not open output file: " + out_path);

        // Output columns:
        //   N          universe size
        //   seed       RNG seed
        //   prefix     actual number t of inserted keys
        //   c          t/N
        //   best_delta exact best integer delta in the searched range
        //   best_L     optimal segment count at that delta
        //   C          log2(best_L * best_delta)
        //   evals      number of distinct delta values actually evaluated
        out << "N,seed,prefix,c,best_delta,best_L,C,evals\n";
        out << std::setprecision(12);

        // --------------------------------------------------------------------
        // Evaluate every requested prefix fraction.
        // --------------------------------------------------------------------
        for (double c0 : fractions) {
            // Convert fraction to an actual prefix length t, clamped to [1,N].
            std::size_t t =
                (std::size_t)std::llround(c0 * (double)N);
            t = std::max<std::size_t>(1,
                std::min<std::size_t>(N, t));

            // The first t items of the random permutation are exactly the keys
            // currently inserted.  Experiment 1 stores them in a packed sorted
            // array, hence we sort these keys.
            std::vector<Key> keys(perm.begin(), perm.begin() + t);
            std::sort(keys.begin(), keys.end());

            // Construct the learned-index points lazily:
            //
            //      x_i = sorted key value,
            //      y_i = compact packed-array rank i.
            //
            // Because keys are distinct and sorted, x_i is strictly increasing,
            // satisfying FastPLAWorkspace's precondition.
            auto get = [&](std::size_t i) -> Point {
                return {(Real)keys[i], (Real)i};
            };

            // ---------------------------------------------------------------
            // Exact integer-delta search range used in Experiment 1.
            // ---------------------------------------------------------------
            // y ranges from 0 to t-1, so y-span = t-1.
            // Searching beyond about half the span cannot improve the product
            // objective: once delta is that large, a single segment suffices in
            // the broadest possible vertical sense, while increasing delta only
            // makes L*delta larger.  The original Experiment-1 code therefore
            // uses
            //
            //      max_delta = max(1, floor(t/2)).
            std::uint64_t max_delta =
                std::max<std::uint64_t>(1, (std::uint64_t)(t/2));

            // Solve min_delta L(delta)*delta exactly over integer deltas.
            auto r = oracle.optimize(t, get, max_delta);

            // Emit one row for this prefix.
            out << N << ','
                << seed << ','
                << t << ','
                << ((double)t/(double)N) << ','
                << r.delta << ','
                << r.L << ','
                << r.C << ','
                << r.evals << '\n';
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}