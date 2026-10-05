#pragma once

// T1: keeps the upper and lower hull as vectors of vertices in key order. A
// join streams only the vertices through O'Rourke (a line fits the points
// exactly when it fits their hull); a split rebuilds both pieces from the PMA.

#include <optional>
#include <utility>
#include <vector>

#include "hull.hpp"

namespace gpla {

struct VectorHull {
    std::vector<Pt> upper, lower;  // both from the first point to the last, no three collinear
    bool operator==(const VectorHull &) const = default;

    // Monotone chain: appends p, right of every vertex.
    static void push_upper(std::vector<Pt> &chain, Pt p) {
        while (chain.size() >= 2 && cross(chain[chain.size() - 2], chain.back(), p) >= 0) chain.pop_back();
        chain.push_back(p);
    }
    static void push_lower(std::vector<Pt> &chain, Pt p) {
        while (chain.size() >= 2 && cross(chain[chain.size() - 2], chain.back(), p) <= 0) chain.pop_back();
        chain.push_back(p);
    }

    // Calls f on every vertex, in key order, while it returns true.
    template <class F>
    bool all_vertices(F &&f) const {
        size_t i = 0, j = 0;
        while (i < upper.size() || j < lower.size()) {
            Pt p;
            if (j == lower.size() || (i < upper.size() && upper[i].x < lower[j].x)) {
                p = upper[i++];
            } else if (i == upper.size() || lower[j].x < upper[i].x) {
                p = lower[j++];
            } else {  // on both chains
                p = upper[i++];
                ++j;
            }
            if (!f(p)) return false;
        }
        return true;
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

    static std::optional<std::pair<VectorHull, ExactLine>> try_join(VectorHull &l, SlotRange, VectorHull &r, SlotRange,
                                                                    const PointView &, int64_t k) {
        Fitter &f = scratch_fitter(k);
        auto add = [&](Pt p) { return f.add(p); };
        if (!l.all_vertices(add) || !r.all_vertices(add)) return std::nullopt;
        VectorHull h{std::move(l.upper), std::move(l.lower)};
        for (Pt p : r.upper) push_upper(h.upper, p);
        for (Pt p : r.lower) push_lower(h.lower, p);
        return std::pair{std::move(h), f.line()};
    }

    static std::pair<VectorHull, VectorHull> split(VectorHull &&, SlotRange left, SlotRange right,
                                                   const PointView &pts) {
        return {build(pts, left), build(pts, right)};
    }
};

static_assert(Hull<VectorHull>);

}  // namespace gpla
