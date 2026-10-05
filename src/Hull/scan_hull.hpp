#pragma once

// T0: keeps nothing. A join streams every point of both segments through
// O'Rourke; a split is free.

#include <optional>
#include <utility>

#include "hull.hpp"

namespace lpma {

struct ScanHull {
    bool operator==(const ScanHull &) const = default;

    static ScanHull build(const PointView &, SlotRange) { return {}; }

    static std::optional<std::pair<ScanHull, ExactLine>> try_join(ScanHull &, SlotRange ls, ScanHull &, SlotRange rs,
                                                                  const PointView &pts, int64_t k) {
        Fitter &f = scratch_fitter(k);
        auto add = [&](Pt p) { return f.add(p); };
        if (!pts.all_of(ls, add) || !pts.all_of(rs, add)) return std::nullopt;
        return std::pair{ScanHull{}, f.line()};
    }

    static std::pair<ScanHull, ScanHull> split(ScanHull &&, SlotRange, SlotRange, const PointView &) { return {}; }
};

static_assert(Hull<ScanHull>);

}  // namespace lpma
