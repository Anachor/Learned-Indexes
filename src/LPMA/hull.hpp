#pragma once

// What a segment keeps so it can test joins, and the versions so far.
//
// A segment's points are the occupied slots of a slot range, as (key, 2*slot).
// Hull<H> is the interface Segment<H> (segment.hpp) uses. The segment passes its
// slot ranges in, so H holds only what it adds on top of them:
//
//   ScanHull    nothing. A join streams every point of both segments, read
//               from the PMA, through O'Rourke; a split costs nothing.
//   VectorHull  the upper and lower hull chains. A join streams only their
//               vertices; a split rebuilds both pieces' chains from the PMA.
//
// A line is within k of every point exactly when the strip around it holds
// their convex hull, so the hull vertices decide a join.

#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "../PMA/packed_memory_array.hpp"
#include "geometry.hpp"

namespace lpma {

using SlotRange = PackedMemoryArray::SlotRange;

inline constexpr size_t NONE = size_t(-1);

// Read-only access to a PMA's current layout as points.
struct PointView {
    const PackedMemoryArray &pma;

    bool occupied(size_t slot) const { return pma.occupied(slot); }
    Key key(size_t slot) const { return pma.key_at(slot); }
    Pt at(size_t slot) const { return {pma.key_at(slot), 2 * int64_t(slot)}; }

    // Calls f on the points of r in order while it returns true; false if it
    // returned false.
    template <class F>
    bool all_of(SlotRange r, F &&f) const {
        for (size_t s = r.begin; s < r.end; ++s) {
            if (pma.occupied(s) && !f(at(s))) return false;
        }
        return true;
    }

    // The last occupied slot in [lo, end), or NONE.
    size_t prev_occupied(size_t end, size_t lo) const {
        while (end > lo) {
            if (pma.occupied(--end)) return end;
        }
        return NONE;
    }

    // The first occupied slot in [begin, hi), or NONE.
    size_t next_occupied(size_t begin, size_t hi) const {
        for (; begin < hi; ++begin) {
            if (pma.occupied(begin)) return begin;
        }
        return NONE;
    }
};

// In try_join, l's keys are all below r's. split gets the slot ranges of the
// two pieces it keeps (empty when there is no piece); the points between them
// are dropped, and only the pieces' slots are read.
template <class H>
concept Hull = std::movable<H> &&
               requires(const H &l, const H &r, H h, SlotRange s, const PointView &pts, int64_t k) {
                   // For the points in slots s, which one line fits.
                   { H::build(pts, s) } -> std::same_as<H>;
                   // One line within k of the points of both: the combined H and
                   // that line. nullopt if there is none. l and r never change.
                   { H::try_join(l, s, r, s, pts, k) } -> std::same_as<std::optional<std::pair<H, ExactLine>>>;
                   // The pieces for slot ranges left and right of h's points.
                   { H::split(std::move(h), s, s, pts) } -> std::same_as<std::pair<H, H>>;
               };

// A Fitter to reuse, reset to bound k.
inline Fitter &scratch_fitter(int64_t k) {
    thread_local Fitter fitter;
    fitter.reset(k);
    return fitter;
}

// Does one line fit the points of r? O(points).
inline bool fits(const PointView &pts, SlotRange r, int64_t k) {
    Fitter &f = scratch_fitter(k);
    return pts.all_of(r, [&](Pt p) { return f.add(p); });
}

// The fewest segments covering the points of r: greedy maximal runs, which is
// optimal because a subset of points that one line fits is fitted by it too.
inline size_t optimal_segments(const PointView &pts, SlotRange r, int64_t k) {
    Fitter &f = scratch_fitter(k);
    size_t segments = 0;
    pts.all_of(r, [&](Pt p) {
        if (f.size() == 0 || !f.add(p)) {
            f.reset();
            f.add(p);
            ++segments;
        }
        return true;
    });
    return segments;
}

// T0: keeps nothing beyond the slot range.
struct ScanHull {
    bool operator==(const ScanHull &) const = default;

    static ScanHull build(const PointView &, SlotRange) { return {}; }

    static std::optional<std::pair<ScanHull, ExactLine>> try_join(const ScanHull &, SlotRange ls, const ScanHull &,
                                                                  SlotRange rs, const PointView &pts, int64_t k) {
        Fitter &f = scratch_fitter(k);
        auto add = [&](Pt p) { return f.add(p); };
        if (!pts.all_of(ls, add) || !pts.all_of(rs, add)) return std::nullopt;
        return std::pair{ScanHull{}, f.line()};
    }

    static std::pair<ScanHull, ScanHull> split(ScanHull &&, SlotRange, SlotRange, const PointView &) { return {}; }
};

// T1: the hull chains, as vectors of vertices in key order. Both chains start
// at the first point and end at the last; collinear points are left out.
struct VectorHull {
    std::vector<Pt> upper, lower;
    bool operator==(const VectorHull &) const = default;

    // Calls f on every vertex in key order while it returns true; false if it
    // returned false. The end points are on both chains, and called once.
    template <class F>
    bool all_vertices(F &&f) const {
        size_t i = 0, j = 0;
        while (i < upper.size() || j < lower.size()) {
            Pt p;
            if (j == lower.size() || (i < upper.size() && upper[i].x < lower[j].x)) {
                p = upper[i++];
            } else if (i == upper.size() || lower[j].x < upper[i].x) {
                p = lower[j++];
            } else {  // the same key: the same point
                p = upper[i++];
                ++j;
            }
            if (!f(p)) return false;
        }
        return true;
    }

    // Monotone chain: adds p, right of every vertex, to the end of a chain.
    static void push_upper(std::vector<Pt> &chain, Pt p) {
        while (chain.size() >= 2 && cross(chain[chain.size() - 2], chain.back(), p) >= 0) chain.pop_back();
        chain.push_back(p);
    }
    static void push_lower(std::vector<Pt> &chain, Pt p) {
        while (chain.size() >= 2 && cross(chain[chain.size() - 2], chain.back(), p) <= 0) chain.pop_back();
        chain.push_back(p);
    }

    static VectorHull build(const PointView &pts, SlotRange s) {
        VectorHull h;
        pts.all_of(s, [&](Pt p) {
            push_upper(h.upper, p);
            push_lower(h.lower, p);
            return true;
        });
        return h;
    }

    static std::optional<std::pair<VectorHull, ExactLine>> try_join(const VectorHull &l, SlotRange, const VectorHull &r,
                                                                    SlotRange, const PointView &, int64_t k) {
        Fitter &f = scratch_fitter(k);
        auto add = [&](Pt p) { return f.add(p); };
        if (!l.all_vertices(add) || !r.all_vertices(add)) return std::nullopt;
        // The hull of the union is the hull of both hulls' vertices.
        VectorHull h{l.upper, l.lower};
        for (Pt p : r.upper) push_upper(h.upper, p);
        for (Pt p : r.lower) push_lower(h.lower, p);
        return std::pair{std::move(h), f.line()};
    }

    static std::pair<VectorHull, VectorHull> split(VectorHull &&, SlotRange left, SlotRange right,
                                                   const PointView &pts) {
        return {build(pts, left), build(pts, right)};
    }
};

static_assert(Hull<ScanHull> && Hull<VectorHull>);

}  // namespace lpma
