#pragma once

// Searches on convex chains, for TreeHull. A chain has size() and at(i): the
// vertices of an upper hull in key order. A lower hull is passed as the upper
// hull of the points reflected, y -> -y.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

#include "geometry.hpp"

namespace lpma {

// The line through a1, a2 is above the line through b1, b2 at x.
inline bool above_at(Pt a1, Pt a2, Pt b1, Pt b2, int64_t x) {
    i128 da = i128(a2.x) - a1.x, db = i128(b2.x) - b1.x;
    i128 na = i128(a1.y) * da + (i128(a2.y) - a1.y) * (i128(x) - a1.x);  // da * line a at x
    i128 nb = i128(b1.y) * db + (i128(b2.y) - b1.y) * (i128(x) - b1.x);  // db * line b at x
    return fraction_less(nb, db, na, da);
}

// The bridge of upper hulls A and B, A left of B: the vertices (a, b) of their
// common upper tangent, a the leftmost on it and b the rightmost, so that
// A[0..a] + B[b..] is the upper hull of both. Overmars and van Leeuwen's search:
// each step looks at the middle vertices a and b against the line a-b and drops
// at least half of what is left of A or of B.
template <class ChainA, class ChainB>
std::pair<int64_t, int64_t> bridge(const ChainA &A, const ChainB &B) {
    const int64_t na = A.size(), nb = B.size(), x0 = B.at(0).x;
    int64_t alo = 0, ahi = na - 1, blo = 0, bhi = nb - 1;
    while (true) {
        if (alo > ahi || blo > bhi) throw std::logic_error("bridge: lost the tangent");
        int64_t i = (alo + ahi) / 2, j = (blo + bhi) / 2;
        Pt a = A.at(i), b = B.at(j);
        // The side of a (and of b) with a neighbour above the line a-b: -1, 1, or 0 for none.
        int sa = i > 0 && cross(a, b, A.at(i - 1)) > 0 ? -1 : i + 1 < na && cross(a, b, A.at(i + 1)) > 0 ? 1 : 0;
        int sb = j > 0 && cross(a, b, B.at(j - 1)) > 0 ? -1 : j + 1 < nb && cross(a, b, B.at(j + 1)) > 0 ? 1 : 0;

        if (sa == 0 && sb == 0) {  // the line touches both hulls
            if (i > 0 && cross(a, b, A.at(i - 1)) == 0) --i;
            if (j + 1 < nb && cross(a, b, B.at(j + 1)) == 0) ++j;
            return {i, j};
        }
        if (sa < 0) ahi = i - 1;  // A's tangent vertex is left of a
        if (sb > 0) blo = j + 1;  // B's is right of b
        if (sa == 0 && sb < 0) {
            bhi = j - 1;  // the tangent is steeper than a-b: B's vertex is left of b
        } else if (sa > 0 && sb == 0) {
            alo = i + 1;  // it is flatter: A's vertex is right of a
        } else if (sa > 0 && sb < 0) {
            // A's edge after a and B's edge before b cross left of B exactly when
            // A's vertex is right of a; otherwise B's is left of b.
            if (above_at(a, A.at(i + 1), B.at(j - 1), b, x0)) alo = i + 1;
            else bhi = j - 1;
        }
    }
}

// The lowest-slope line within k of every point of a set with upper hull U and
// lower hull D (reflected), or nullopt if none is.
//
// At slope s the set's vertical width, max(y - s x) - min(y - s x), is convex in
// s, with breakpoints at the hulls' edge slopes, and a line of slope s fits when
// the width is at most 2k. After the last breakpoint where the width is still
// above 2k and falling, the highest point p and the lowest q stay the same, and
// width = 2k gives the line through (q.x, q.y + k) and (p.x, p.y - k).
template <class ChainU, class ChainD>
std::optional<ExactLine> lowest_line(const ChainU &U, const ChainD &D, int64_t k) {
    const int64_t nu = U.size(), nd = D.size();
    auto low = [&](int64_t i) { return Pt{D.at(i).x, -D.at(i).y}; };
    auto slope = [](Pt a, Pt b) { return std::pair{i128(b.y) - a.y, i128(b.x) - a.x}; };
    // The first i in [0, n) where pred(i) holds, or n; pred is monotone.
    auto first = [](int64_t n, auto pred) {
        int64_t lo = 0, hi = n;
        while (lo < hi) {
            int64_t mid = (lo + hi) / 2;
            if (pred(mid)) hi = mid;
            else lo = mid + 1;
        }
        return lo;
    };
    // The highest point at slope dy/dx, the leftmost of two: before the first upper edge no steeper.
    auto top = [&](i128 dy, i128 dx) {
        return U.at(first(nu - 1, [&](int64_t i) {
            auto [ey, ex] = slope(U.at(i), U.at(i + 1));
            return ey * dx <= dy * ex;
        }));
    };
    // The lowest point at slope dy/dx, the rightmost of two: before the first lower edge steeper.
    auto bottom = [&](i128 dy, i128 dx) {
        return low(first(nd - 1, [&](int64_t i) {
            auto [ey, ex] = slope(low(i), low(i + 1));
            return ey * dx > dy * ex;
        }));
    };
    // At or past the lowest fitting slope: the width at most 2k, or growing.
    auto past = [&](i128 dy, i128 dx, Pt p, Pt q) {
        return q.x > p.x || (i128(p.y) - q.y) * dx - dy * (i128(p.x) - q.x) <= 2 * i128(k) * dx;
    };

    // The last breakpoint not past: on the upper hull (slopes falling), the
    // first edge not past; on the lower (rising), the edge before the first past.
    int64_t u = first(nu - 1, [&](int64_t i) {
        auto [dy, dx] = slope(U.at(i), U.at(i + 1));
        return !past(dy, dx, U.at(i), bottom(dy, dx));
    });
    int64_t d = first(nd - 1, [&](int64_t i) {
        auto [dy, dx] = slope(low(i), low(i + 1));
        return past(dy, dx, top(dy, dx), low(i + 1));
    }) - 1;

    // p and q just after the greater of the two; before every breakpoint, the
    // last point and the first.
    Pt p = U.at(nu - 1), q = low(0);
    bool use_u = u < nu - 1;
    if (use_u && d >= 0 && slope_less(U.at(u), U.at(u + 1), low(d), low(d + 1))) use_u = false;
    if (use_u) {
        auto [dy, dx] = slope(U.at(u), U.at(u + 1));
        p = U.at(u);
        q = bottom(dy, dx);
    } else if (d >= 0) {
        auto [dy, dx] = slope(low(d), low(d + 1));
        q = low(d + 1);
        p = top(dy, dx);
    }
    if (p.x <= q.x) return std::nullopt;

    ExactLine line{{q.x, q.y + k}, i128(p.y) - q.y - 2 * i128(k), i128(p.x) - q.x};
    // It fits every point exactly when it fits the highest and the lowest at its slope.
    if (!line.within(top(line.dy, line.dx), k) || !line.within(bottom(line.dy, line.dx), k)) return std::nullopt;
    return line;
}

}  // namespace lpma
