#pragma once

// O'Rourke on a sorted array, shared by the experiments.
//
// The points of a sorted array x are (key, rank). k = 2*delta, so delta covers
// the half-integers; the points go to O'Rourke as (key, 2*rank) with integer
// bound k.
//
// Every function also takes y, the keys' positions when they are not their
// ranks - exp3's PMA slots, gaps included: the points are then (key, y[i]),
// sent as (key, 2*y[i]). y must be non-decreasing; null means the ranks.
//
//   query complexity = log2(delta) + log2(segment size), delta = k/2

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../../src/ORourke/orourke.hpp"

// delta = 0.5, 1, 2, 4, 8, 16, 32
inline const std::vector<int64_t> FIXED_K = {1, 2, 4, 8, 16, 32, 64};

// Which k is best when several give the same k * L (the same query complexity),
// from --tiebreaker: minlambda, the fewest segments - the largest delta (the
// default) - or mindelta, the smallest delta. Set in main, before any search.
inline bool TIE_MIN_LAMBDA = true;

// --tiebreaker's value: "mindelta" or "minlambda"; false if it is neither.
inline bool parse_tiebreaker(const std::string &s) {
    if (s != "mindelta" && s != "minlambda") return false;
    TIE_MIN_LAMBDA = s == "minlambda";
    return true;
}

inline const char *tiebreaker_name() { return TIE_MIN_LAMBDA ? "minlambda" : "mindelta"; }

// True when (k, product) beats the best so far (best_k, best_product), ties
// going the --tiebreaker way. best_product < 0: nothing yet. With equal k * L,
// a larger k has fewer segments.
inline bool better(int64_t k, __int128 product, int64_t best_k, __int128 best_product) {
    if (best_product < 0 || product < best_product) return true;
    return product == best_product && (TIE_MIN_LAMBDA ? k > best_k : k < best_k);
}

// One evaluated error bound on one sorted array.
struct Row {
    int64_t k;
    size_t L;    // segment size
    bool fixed;  // one of FIXED_K
    bool best;   // the minimiser
};

// log2(delta) + log2(lambda). Minimising it is minimising k * L, which is what
// the search does, exactly, in integers.
inline double query_complexity(size_t L, int64_t k) { return std::log2(double(k) / 2) + std::log2(double(L)); }

// The position of x[i]: its rank, or y[i].
inline int64_t position(const std::vector<int64_t> *y, size_t i) { return y ? (*y)[i] : int64_t(i); }

// The smallest k at which one horizontal line, through the middle of the
// positions, fits every point: their span, last minus first. At least 1.
inline int64_t full_k(const std::vector<int64_t> &x, const std::vector<int64_t> *y) {
    if (x.empty()) return 1;
    return std::max<int64_t>(1, position(y, x.size() - 1) - position(y, 0));
}

// Segment size of a sorted array under bound k, using 2 * position.
inline size_t count_segments(ORourke<int64_t> &orourke, const std::vector<int64_t> &x, int64_t k,
                             const std::vector<int64_t> *y = nullptr) {
    if (x.empty()) return 0;
    orourke.reset(k);
    size_t segments = 1;
    for (size_t i = 0; i < x.size(); ++i) {
        if (orourke.add_point(x[i], 2 * position(y, i))) ++segments;
    }
    return segments;
}

// Every k evaluated on this array, sorted by k: the fixed ones first, then
// whatever the minimisation needs.
//
// The minimisation is exact. L(k) never increases with k, so where L is flat the
// smallest k wins and the optimum sits at a step of L. A range [lo, hi] can be
// discarded when lo * L(hi), the best product it could hold, cannot beat the
// incumbent.
inline std::vector<Row> analyse_prefix(ORourke<int64_t> &orourke, const std::vector<int64_t> &x,
                                       const std::vector<int64_t> *y = nullptr) {
    int64_t k_max = full_k(x, y);

    std::vector<Row> rows;
    auto L = [&](int64_t k) -> size_t {
        for (const Row &r : rows) {
            if (r.k == k) return r.L;
        }
        // At k >= k_max a horizontal line through the middle position always fits.
        size_t v = k >= k_max ? 1 : count_segments(orourke, x, k, y);
        rows.push_back({k, v, false, false});
        return v;
    };

    // The fixed k first: also a good incumbent for the search below.
    for (int64_t k : FIXED_K) L(k);
    for (Row &r : rows) r.fixed = true;

    __int128 best_product = -1;
    int64_t best_k = 1;
    auto consider = [&](int64_t k, size_t Lk) {
        if (k > k_max) return;
        __int128 product = __int128(k) * Lk;
        if (better(k, product, best_k, best_product)) {
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

// The k minimising k * L(k) by trying every k, ties the --tiebreaker way, for
// validation: (k, k * L).
inline std::pair<int64_t, __int128> exhaustive_best(ORourke<int64_t> &orourke, const std::vector<int64_t> &x,
                                                   const std::vector<int64_t> *y = nullptr) {
    int64_t k_max = full_k(x, y);
    __int128 best_product = -1;
    int64_t best_k = 1;
    for (int64_t k = 1; k <= k_max; ++k) {
        __int128 product = __int128(k) * count_segments(orourke, x, k, y);
        if (better(k, product, best_k, best_product)) {
            best_product = product;
            best_k = k;
        }
    }
    return {best_k, best_product};
}
