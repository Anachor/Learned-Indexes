// The three hulls (src/Hull) build the same structure.
//
// On random runs of a PMA's points, with each hull (T2 with TreeHull's leaves
// and with SmallTreeHull's): a join succeeds exactly when brute force fits one
// line, a failed join changes nothing, and a split equals building the pieces
// from scratch; then the joined segments must be the same, line and hull. And
// T2 alone, with leaves of 1, 2, 3 and TreeHull's points, on points with many
// hull edges at one slope: joins against brute force and O'Rourke's line,
// splits, every tree node and leaf checked.
//
//   g++-11 -std=c++20 -O2 tests/hulls.cpp -o tests/hulls
//   ./tests/hulls [-i iterations] [-n MAXN] [seed]

#include "gpla_common.hpp"

// Splits s around gap and compares the pieces with pieces built from scratch.
template <class H>
const char *split_problem(const gpla::Segment<H> &s, SlotRange gap, const PointView &pts, int64_t k) {
    using Seg = gpla::Segment<H>;
    std::optional<SlotRange> left, right;  // the expected pieces
    for (size_t slot = s.slots.begin; slot < s.slots.end; ++slot) {
        if (!pts.occupied(slot)) continue;
        if (slot < gap.begin) left = SlotRange{left ? left->begin : slot, slot + 1};
        if (slot >= gap.end) right = SlotRange{right ? right->begin : slot, slot + 1};
    }
    auto [l, r] = Seg::split(Seg(s), gap, pts);
    for (auto [got, want] : {std::pair{&l, left}, std::pair{&r, right}}) {
        if (got->has_value() != want.has_value()) return "split: wrong pieces";
        if (!want) continue;
        if ((*got)->slots != *want || (*got)->line != s.line) return "split: a piece's slots or line";
        if (!((*got)->hull == H::build(pts, *want)) || !(*got)->check(pts, k)) return "split: a piece's hull";
    }
    return nullptr;
}

// A gap of slots near s: maybe empty, maybe outside it.
SlotRange random_gap(std::mt19937_64 &rng, SlotRange s) {
    size_t lo = s.begin >= 3 ? s.begin - 3 : 0;
    size_t begin = lo + rng() % (s.end + 4 - lo);
    return {begin, begin + rng() % (s.end - s.begin + 4)};
}

// Builds segments with hull H on slots ls and rs, joins and splits them; the
// joined segment, if any, goes to joined.
template <class H>
const char *join_problem(const PointView &pts, SlotRange ls, ExactLine ll, SlotRange rs, ExactLine rl, int64_t k,
                         bool fits, std::optional<gpla::Segment<H>> &joined, std::mt19937_64 &rng) {
    using Seg = gpla::Segment<H>;
    Seg l = Seg::build(pts, ls, ll), r = Seg::build(pts, rs, rl);
    const Seg l_before = l, r_before = r;
    auto j = Seg::try_join(l, r, pts, k);
    if (j.has_value() != fits) return "try_join disagrees with brute force";
    if (!j && !(l == l_before && r == r_before)) return "a failed try_join changed its inputs";
    if (j) {
        if (j->slots != SlotRange{ls.begin, rs.end} || !j->check(pts, k)) return "the joined segment";
        if (const char *p = split_problem(*j, random_gap(rng, j->slots), pts, k)) return p;
        joined = std::move(j);
    }
    return split_problem(l_before, random_gap(rng, ls), pts, k);
}

// Random neighbouring runs of a PMA's points, with all three hulls.
template <class Storage>
const char *runs_problem(std::mt19937_64 &rng, size_t n, int64_t k) {
    Storage pma;
    for (Key key : random_keys(rng, n).first) pma.insert(key);
    PointView pts{pma};
    std::vector<size_t> slots;
    for (size_t s = 0; s < pma.capacity(); ++s) {
        if (pma.occupied(s)) slots.push_back(s);
    }
    // From slots[i], at most max_len points that one line fits: (end, line).
    auto run = [&](size_t i, size_t max_len) {
        Fitter &f = gpla::scratch_fitter(k);
        size_t j = i;
        while (j < slots.size() && j - i < max_len && f.add(pts.at(slots[j]))) ++j;
        return std::pair{j, f.line()};
    };

    for (int trial = 0; trial < 20 && slots.size() >= 2; ++trial) {
        size_t i = rng() % (slots.size() - 1);
        auto [j, ll] = run(i, 1 + rng() % 30);
        if (j == slots.size()) continue;
        auto [end, rl] = run(j, 1 + rng() % 30);
        SlotRange ls{slots[i], slots[j - 1] + 1}, rs{slots[j], slots[end - 1] + 1};
        bool fits = brute_fits(points_of(pts, {ls.begin, rs.end}), k);

        std::optional<gpla::Segment<ScanHull>> j0;
        std::optional<gpla::Segment<VectorHull>> j1;
        std::optional<gpla::Segment<TreeHull>> j2;
        std::optional<gpla::Segment<SmallTreeHull>> j3;
        if (const char *p = join_problem(pts, ls, ll, rs, rl, k, fits, j0, rng)) return p;
        if (const char *p = join_problem(pts, ls, ll, rs, rl, k, fits, j1, rng)) return p;
        if (const char *p = join_problem(pts, ls, ll, rs, rl, k, fits, j2, rng)) return p;
        if (const char *p = join_problem(pts, ls, ll, rs, rl, k, fits, j3, rng)) return p;
        if (j0) {
            if (const char *p = structure_problem(*j0, *j1, *j2)) return p;
            if (const char *p = structure_problem(*j0, *j1, *j3)) return p;
        }
    }
    return nullptr;
}

// T2 alone, on points with y increasing: two trees cut at random, joined
// exactly when brute force fits one line and with O'Rourke's line, then split
// with some points dropped. Every tree must be valid and hold the right points.
template <class T2>
const char *tree_problem(const std::vector<Pt> &points, int64_t k, std::mt19937_64 &rng) {
    static PMA<> unused;  // TreeHull does not read the PMA
    PointView pts{unused};
    size_t n = points.size();
    for (size_t i = 1; i < n; ++i) {
        if (points[i].y <= points[i - 1].y) return nullptr;  // not a PMA's layout
    }
    if (n < 2) return nullptr;
    size_t cut = 1 + rng() % (n - 1);
    T2 l = T2::from_points({points.begin(), points.begin() + long(cut)});
    T2 r = T2::from_points({points.begin() + long(cut), points.end()});
    if (!l.valid() || !r.valid()) return "T2: a built tree";
    const T2 l_before = l, r_before = r;
    auto joined = T2::try_join(l, {}, r, {}, pts, k);
    if (joined.has_value() != brute_fits(points, k)) return "T2: try_join disagrees with brute force";
    if (!joined) return l == l_before && r == r_before ? nullptr : "T2: a failed try_join changed a tree";

    Fitter f(k);
    for (const Pt &p : points) f.add(p);
    if (!joined->second.same(f.line())) return "T2: not O'Rourke's line";
    if (!joined->first.valid() || joined->first.points() != points) return "T2: the joined tree";

    // Keep points [0, a) and [b, n): a point's slot is y / 2.
    size_t a = rng() % (n + 1), b = a + rng() % (n - a + 1);
    SlotRange left{0, a ? size_t(points[a - 1].y / 2) + 1 : 0};
    SlotRange right{b < n ? size_t(points[b].y / 2) : size_t(points.back().y / 2) + 1, size_t(-1)};
    auto [x, y] = T2::split(std::move(joined->first), left, right, pts);
    if (!x.valid() || !y.valid()) return "T2: a split piece";
    if (x.points() != std::vector<Pt>(points.begin(), points.begin() + long(a)) ||
        y.points() != std::vector<Pt>(points.begin() + long(b), points.end()))
        return "T2: the split pieces' points";
    return nullptr;
}

int main(int argc, char **argv) {
    Args args = parse_args(argc, argv);
    std::mt19937_64 rng(args.seed);
    int failures = 0;
    for (int it = 0; it < args.iterations; ++it) {
        size_t n = 2 + rng() % args.max_n;
        int64_t k = KS[rng() % KS.size()];
        std::string where = "case " + std::to_string(it) + " (n=" + std::to_string(n) + ", k=" + std::to_string(k) + ")";
        failures += failed(where + " PMA", [&] { return runs_problem<PMA<>>(rng, n, k); });
        failures += failed(where + " sparse PMA", [&] { return runs_problem<SparsePMA>(rng, n, k); });
        std::vector<Pt> points = random_points(rng, std::min<size_t>(n, 255));
        failures += failed(where + " T2 alone", [&] { return tree_problem<TreeHull>(points, k, rng); });
        failures += failed(where + " T2 alone, leaves of 1", [&] { return tree_problem<BasicTreeHull<1>>(points, k, rng); });
        failures += failed(where + " T2 alone, leaves of 2", [&] { return tree_problem<BasicTreeHull<2>>(points, k, rng); });
        failures += failed(where + " T2 alone, leaves of 3", [&] { return tree_problem<BasicTreeHull<3>>(points, k, rng); });
    }
    std::cout << "hulls: " << args.iterations << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
