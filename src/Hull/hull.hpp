#pragma once

// The interface a segment's hull implements, Hull<H>, and what the three share.
// A segment's points are the occupied slots of a slot range, as (key, 2*slot).
//
//   ScanHull   (T0, scan_hull.hpp)    keeps nothing; a join reads every point
//   VectorHull (T1, vector_hull.hpp)  keeps the hull as vectors; a split rebuilds it
//   TreeHull   (T2, tree_hull.hpp)    keeps a tree of hulls; join and split are polylog
//
// All three build the same segments with the same lines.

#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>

#include "../PMA/packed_memory_array.hpp"
#include "geometry.hpp"

namespace gpla {

using SlotRange = PackedMemoryArray::SlotRange;

inline constexpr size_t NONE = size_t(-1);

// The PMA's current layout, read as points.
struct PointView {
    const PackedMemoryArray &pma;

    bool occupied(size_t slot) const { return pma.occupied(slot); }
    Key key(size_t slot) const { return pma.key_at(slot); }
    Pt at(size_t slot) const { return {pma.key_at(slot), 2 * int64_t(slot)}; }

    // Calls f on each point of r while it returns true; false if it stopped.
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

template <class H>
concept Hull = std::movable<H> && requires(H &l, H &r, H h, SlotRange s, const PointView &pts, int64_t k) {
    // For the points in slots s.
    { H::build(pts, s) } -> std::same_as<H>;
    // If one line fits l's and r's points (l's keys all below r's): the joined
    // hull, which may use up l and r, and the lowest-slope such line. If not:
    // nullopt, and l and r are unchanged.
    { H::try_join(l, s, r, s, pts, k) } -> std::same_as<std::optional<std::pair<H, ExactLine>>>;
    // The hulls of h's points in slot ranges left and right; the rest is dropped.
    { H::split(std::move(h), s, s, pts) } -> std::same_as<std::pair<H, H>>;
};

// A Fitter to reuse, reset to bound k.
inline Fitter &scratch_fitter(int64_t k) {
    thread_local Fitter fitter;
    fitter.reset(k);
    return fitter;
}

// Does one line fit the points of r?
inline bool fits(const PointView &pts, SlotRange r, int64_t k) {
    Fitter &f = scratch_fitter(k);
    return pts.all_of(r, [&](Pt p) { return f.add(p); });
}

// The fewest segments for the points of r: greedy, which is optimal.
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

}  // namespace gpla
