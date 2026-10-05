// The learned PMA (src/LPMA) with all three hulls side by side - T2 twice, with
// TreeHull's leaves and SmallTreeHull's - on random insert sequences over three
// PMAs: the default, a sparse one, and GapPMA (gap_pma.hpp), which cuts
// segments without moving keys. After every insert: each index passes
// problem(); all hold the same structure and made the same decisions;
// lower_bound and contains agree with a std::set; at most 4 join attempts; a
// duplicate changes nothing. Up to 24 keys, BruteORourke also
// checks that no two neighbours can be joined and at most 2 * optimal - 1
// segments.
//
//   g++-11 -std=c++20 -O2 tests/lpma.cpp -o tests/lpma
//   ./tests/lpma [-i iterations] [-n MAXN] [seed]

#include <iterator>

#include "gap_pma.hpp"
#include "lpma_common.hpp"

template <class Storage>
struct Indexes {
    lpma::LearnedPMA<ScanHull, Storage> t0;
    lpma::LearnedPMA<VectorHull, Storage> t1;
    lpma::LearnedPMA<TreeHull, Storage> t2;
    lpma::LearnedPMA<SmallTreeHull, Storage> t3;
    explicit Indexes(int64_t k) : t0(k), t1(k), t2(k), t3(k) {}
};

template <class A, class B>
bool same_stats(const A &a, const B &b) {
    return a.inserts == b.inserts && a.grows == b.grows && a.rebuilt_points == b.rebuilt_points &&
           a.rebuilt_segments == b.rebuilt_segments && a.splits == b.splits && a.join_attempts == b.join_attempts &&
           a.joins == b.joins;
}

// The three hold the same segments, lines and hulls, and made the same decisions.
template <class Storage>
const char *same_structure(const Indexes<Storage> &x) {
    if (!same_stats(x.t0.stats(), x.t1.stats()) || !same_stats(x.t0.stats(), x.t2.stats()) ||
        !same_stats(x.t0.stats(), x.t3.stats()))
        return "the hulls made different decisions";
    if (x.t0.segment_count() != x.t1.segment_count() || x.t0.segment_count() != x.t2.segment_count() ||
        x.t0.segment_count() != x.t3.segment_count())
        return "the hulls give different numbers of segments";
    auto s1 = x.t1.segments().begin();
    auto s2 = x.t2.segments().begin();
    auto s3 = x.t3.segments().begin();
    for (const auto &[key, s0] : x.t0.segments()) {
        const auto &v = (s1++)->second;
        if (const char *p = structure_problem(s0, v, (s2++)->second)) return p;
        if (const char *p = structure_problem(s0, v, (s3++)->second)) return p;
    }
    return nullptr;
}

// check()'s last two conditions again, by BruteORourke.
template <class Index>
const char *brute_problem(const Index &index) {
    PointView pts{index.pma()};
    std::vector<std::vector<Pt>> segments;
    for (const auto &[key, s] : index.segments()) segments.push_back(points_of(pts, s.slots));
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        std::vector<Pt> both = segments[i];
        both.insert(both.end(), segments[i + 1].begin(), segments[i + 1].end());
        if (brute_fits(both, index.k())) return "brute force: two neighbours can be joined";
    }
    size_t optimal = brute_starts(points_of(pts, {0, index.pma().capacity()}), index.k()).size();
    if (optimal > 0 && segments.size() > 2 * optimal - 1) return "brute force: more than 2 * optimal - 1 segments";
    return nullptr;
}

// Every key, its neighbours, the int64 ends, and some random keys.
std::vector<Key> probes(const std::set<Key> &keys, std::mt19937_64 &rng) {
    std::vector<Key> out = {KEY_MIN, KEY_MAX, 0};
    for (Key key : keys) {
        out.push_back(key);
        if (key > KEY_MIN) out.push_back(key - 1);
        if (key < KEY_MAX) out.push_back(key + 1);
    }
    for (int i = 0; i < 8; ++i) out.push_back(Key(rng()));
    return out;
}

// Inserts x into all three and checks them.
template <class Storage>
const char *insert_problem(Indexes<Storage> &x, std::set<Key> &expected, Key key, std::mt19937_64 &rng) {
    bool fresh = expected.insert(key).second;
    auto before = x.t2.segments();
    auto stats = x.t0.stats();
    bool i0 = x.t0.insert(key), i1 = x.t1.insert(key), i2 = x.t2.insert(key), i3 = x.t3.insert(key);
    if (i0 != fresh || i1 != fresh || i2 != fresh || i3 != fresh) return "insert's return value";
    if (!fresh && (x.t2.segments() != before || x.t0.stats() != stats)) return "a duplicate changed the index";
    if (x.t0.stats().join_attempts - stats.join_attempts > 4) return "more than 4 join attempts";

    for (const char *p : {x.t0.problem(), x.t1.problem(), x.t2.problem(), x.t3.problem(), same_structure(x)}) {
        if (p) return p;
    }
    if (fresh && expected.size() <= 24) {
        if (const char *p = brute_problem(x.t1)) return p;
    }
    for (Key probe : probes(expected, rng)) {
        auto it = expected.lower_bound(probe);
        std::optional<Key> want = it == expected.end() ? std::nullopt : std::optional<Key>(*it);
        if (x.t0.lower_bound(probe) != want || x.t1.lower_bound(probe) != want || x.t2.lower_bound(probe) != want ||
            x.t3.lower_bound(probe) != want)
            return "lower_bound";
        bool present = expected.count(probe);
        if (x.t0.contains(probe) != present || x.t1.contains(probe) != present || x.t2.contains(probe) != present ||
            x.t3.contains(probe) != present)
            return "contains";
    }
    return nullptr;
}

template <class Storage>
int index_case(const std::vector<Key> &keys, int64_t k, const std::string &where, std::mt19937_64 &rng) {
    Indexes<Storage> x(k);
    std::set<Key> expected;
    for (size_t i = 0; i < keys.size(); ++i) {
        std::string at = where + ", insert " + std::to_string(i) + " (key " + std::to_string(keys[i]) + ")";
        if (failed(at, [&] { return insert_problem(x, expected, keys[i], rng); })) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Args args = parse_args(argc, argv);
    std::mt19937_64 rng(args.seed);
    int failures = 0;
    for (int it = 0; it < args.iterations; ++it) {
        size_t n = 1 + rng() % args.max_n;
        int64_t k = KS[rng() % KS.size()];
        auto [keys, order] = random_keys(rng, n);
        std::string where = "case " + std::to_string(it) + " (" + order + ", n=" + std::to_string(n) +
                            ", k=" + std::to_string(k) + ")";
        failures += index_case<PMA<>>(keys, k, where + " PMA", rng);
        failures += index_case<SparsePMA>(keys, k, where + " sparse PMA", rng);
        failures += index_case<GapPMA>(keys, k, where + " GapPMA", rng);
    }
    std::cout << "lpma: " << args.iterations << " cases x 3 PMAs, " << failures << " failures\n";
    return failures ? 1 : 0;
}
