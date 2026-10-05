#pragma once

// A segment of the learned PMA: a run of consecutive keys, first..last, in
// slots [slots.begin, slots.end) (both ends occupied), with an exact line
// within k of every point (key, 2*slot) of it, and H (hull.hpp), what it keeps
// to test joins.
//
// A segment stays valid while its points keep their slots. An insert reports
// every slot it rewrites, so the index splits off what lies outside them and
// replaces the rest.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

#include "hull.hpp"

namespace lpma {

template <Hull H>
struct Segment {
    Key first = 0, last = 0;
    SlotRange slots;
    ExactLine line;
    H hull;

    bool operator==(const Segment &) const = default;

    // The points in slots s, which line fits; s.begin and s.end - 1 occupied.
    static Segment build(const PointView &pts, SlotRange s, const ExactLine &line) {
        return {pts.key(s.begin), pts.key(s.end - 1), s, line, H::build(pts, s)};
    }

    // The pieces of s before and after the slots of gap, each nullopt when it
    // has no points; the points in gap are dropped. Both pieces keep s's line.
    // Reads only the slots of s outside gap.
    static std::pair<std::optional<Segment>, std::optional<Segment>> split(Segment &&s, SlotRange gap,
                                                                           const PointView &pts) {
        SlotRange left{s.slots.begin, s.slots.begin}, right{s.slots.end, s.slots.end};
        size_t p = pts.prev_occupied(std::min(gap.begin, s.slots.end), s.slots.begin);
        if (p != NONE) left.end = p + 1;
        size_t q = pts.next_occupied(std::max(gap.end, s.slots.begin), s.slots.end);
        if (q != NONE) right.begin = q;

        auto [left_hull, right_hull] = H::split(std::move(s.hull), left, right, pts);
        std::optional<Segment> l, r;
        if (!left.empty()) l = Segment{s.first, pts.key(left.end - 1), left, s.line, std::move(left_hull)};
        if (!right.empty()) r = Segment{pts.key(right.begin), s.last, right, s.line, std::move(right_hull)};
        return {std::move(l), std::move(r)};
    }

    // l followed by r, if one line fits both; their keys must be consecutive
    // in the PMA, l's first. nullopt otherwise; l and r never change.
    static std::optional<Segment> try_join(const Segment &l, const Segment &r, const PointView &pts, int64_t k) {
        if (!(l.last < r.first)) throw std::invalid_argument("Segment::try_join: l must come before r");
        auto joined = H::try_join(l.hull, l.slots, r.hull, r.slots, pts, k);
        if (!joined) return std::nullopt;
        return Segment{l.first, r.last, {l.slots.begin, r.slots.end}, joined->second, std::move(joined->first)};
    }

    size_t size(const PointView &pts) const {
        size_t n = 0;
        pts.all_of(slots, [&](Pt) {
            ++n;
            return true;
        });
        return n;
    }

    // The ends hold first and last, every point is within k of the line, and
    // H equals a fresh build when it can be compared.
    bool check(const PointView &pts, int64_t k) const {
        if (slots.empty() || slots.end > pts.pma.capacity()) return false;
        if (!pts.occupied(slots.begin) || !pts.occupied(slots.end - 1)) return false;
        if (pts.key(slots.begin) != first || pts.key(slots.end - 1) != last) return false;
        if (!pts.all_of(slots, [&](Pt p) { return line.within(p, k); })) return false;
        if constexpr (std::equality_comparable<H>) return hull == H::build(pts, slots);
        return true;
    }
};

}  // namespace lpma
