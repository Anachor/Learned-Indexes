// Tests for the learned PMA (src/LPMA). Writes nothing.
//
//   g++-11 -std=c++20 -O2 tests/lpma.cpp -o tests/lpma
//   ./tests/lpma [-i iterations] [-n MAXN] [seed]
//
// fitter    lpma::Fitter, the exact O'Rourke everything else relies on, against
//           BruteORourke on random points: the same greedy segments, every line
//           exactly within k, a failed add changes nothing (points skipped
//           instead of starting a new segment), and on tiny cases greedy has
//           the fewest segments (dynamic programming over every split).
// segments  Segment<ScanHull> (T0) and Segment<VectorHull> (T1) on random runs
//           of a PMA's points: try_join agrees with brute force, a failed join
//           changes nothing, split equals building the pieces from scratch, and
//           T0 and T1 find the same line.
// index     LearnedPMA<ScanHull> and LearnedPMA<VectorHull> side by side on
//           random insert sequences, over PMA<>, a sparse PMA, and GapPMA
//           (below: it moves no key when there is a gap, so the segment around
//           the new key is split with nothing in between). After every insert:
//           check(); lower_bound and contains against a std::set, for present,
//           absent and extreme keys; both have the same segments; at most 4
//           join attempts; a duplicate changes nothing. Up to 24 keys, also, by
//           BruteORourke: no two neighbours can be joined, and at most
//           2 * optimal - 1 segments.
//
// -i is the number of cases per part (default 200), -n the most points or
// inserts in a case (default 150).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/LPMA/learned_pma.hpp"
#include "../src/ORourke/brute_orourke.hpp"

using lpma::ExactLine;
using lpma::Fitter;
using lpma::Key;
using lpma::PointView;
using lpma::Pt;
using lpma::ScanHull;
using lpma::SlotRange;
using lpma::VectorHull;

constexpr Key KEY_MIN = std::numeric_limits<Key>::min(), KEY_MAX = std::numeric_limits<Key>::max();

// The bounds tried: delta = 0 .. 512, and the largest allowed.
const std::vector<int64_t> KS = {0, 1, 2, 3, 4, 8, 16, 64, 1024, lpma::MAX_K};

using Sparse = PMA<PMAParams{.leaf_upper = 0.9, .root_upper = 0.5, .initial_capacity = 2}>;

// A PMA that rewrites as little as the Update contract allows: a key goes into
// the middle of the empty slots between its neighbours, if there are any, and
// only that slot is reported. Otherwise every key is spread evenly again, over
// twice the slots when more than half would be full. Linear time per insert.
class GapPMA final : public PackedMemoryArray {
public:
    GapPMA() : keys_(8), used_(8) {}

    bool insert(Key key, Update *update = nullptr) override {
        if (update) {
            *update = {};
            update->old_capacity = update->new_capacity = capacity();
        }
        size_t pred = lpma::NONE, succ = lpma::NONE;
        for (size_t s = 0; s < capacity(); ++s) {
            if (!used_[s]) continue;
            if (keys_[s] == key) return false;
            if (keys_[s] > key) {
                succ = s;
                break;
            }
            pred = s;
        }
        size_t lo = pred == lpma::NONE ? 0 : pred + 1, hi = succ == lpma::NONE ? capacity() : succ;
        if (lo < hi) {
            size_t s = lo + (hi - lo) / 2;
            keys_[s] = key;
            used_[s] = 1;
            ++size_;
            ++stats_.moves;
            if (update) {
                update->inserted = true;
                update->old_slots = update->new_slots = {s, s + 1};
                update->affected_keys = KeyRange{key, key};
            }
            return true;
        }

        std::vector<Key> all;
        for (size_t s = 0; s < capacity(); ++s) {
            if (used_[s]) all.push_back(keys_[s]);
        }
        all.insert(std::lower_bound(all.begin(), all.end(), key), key);
        size_t old_capacity = capacity(), m = all.size();
        size_t capacity = old_capacity;
        while (2 * m > capacity) capacity *= 2;
        keys_.assign(capacity, 0);
        used_.assign(capacity, 0);
        for (size_t j = 0; j < m; ++j) {
            keys_[j * capacity / m] = all[j];
            used_[j * capacity / m] = 1;
        }
        size_ = m;
        stats_.moves += m;
        if (capacity > old_capacity) ++stats_.grows;
        else ++stats_.rebalances;
        if (update) {
            update->inserted = true;
            update->new_capacity = capacity;
            update->old_slots = {0, old_capacity};
            update->new_slots = {0, capacity};
            update->affected_keys = KeyRange{all.front(), all.back()};
        }
        return true;
    }

    size_t size() const override { return size_; }
    size_t capacity() const override { return keys_.size(); }
    bool occupied(size_t slot) const override { return used_[slot]; }
    Key key_at(size_t slot) const override { return keys_[slot]; }
    const Stats &stats() const override { return stats_; }

    using PackedMemoryArray::points;
    void points(std::vector<Key> &keys, std::vector<int64_t> &slots, SlotRange range) const override {
        check_range(range);
        keys.clear();
        slots.clear();
        for (size_t s = range.begin; s < range.end; ++s) {
            if (!used_[s]) continue;
            keys.push_back(keys_[s]);
            slots.push_back(int64_t(s));
        }
    }

    bool check() const override {
        size_t n = 0;
        std::optional<Key> last;
        for (size_t s = 0; s < capacity(); ++s) {
            if (!used_[s]) continue;
            if (last && keys_[s] <= *last) return false;
            last = keys_[s];
            ++n;
        }
        return n == size_;
    }

private:
    std::vector<Key> keys_;
    std::vector<uint8_t> used_;
    size_t size_ = 0;
    Stats stats_;
};

// ---------------------------------------------------------------- brute force

// Does one line fit the points? BruteORourke, independent of lpma.
bool brute_fits(const std::vector<Pt> &points, int64_t k) {
    BruteORourke<int64_t> brute(k);
    for (const Pt &p : points) {
        if (brute.add_point(p.x, p.y)) return false;  // it started a new segment
    }
    return true;
}

// The indices where BruteORourke's greedy segments start.
std::vector<size_t> brute_starts(const std::vector<Pt> &points, int64_t k) {
    BruteORourke<int64_t> brute(k);
    std::vector<size_t> starts;
    for (size_t i = 0; i < points.size(); ++i) {
        if (brute.add_point(points[i].x, points[i].y) || i == 0) starts.push_back(i);
    }
    return starts;
}

std::vector<Pt> points_of(const PointView &pts, SlotRange r) {
    std::vector<Pt> out;
    pts.all_of(r, [&](Pt p) {
        out.push_back(p);
        return true;
    });
    return out;
}

// ---------------------------------------------------------------- fitter

// n points of increasing x and even y (2 * slot), in one of several shapes,
// and a description.
std::pair<std::vector<Pt>, std::string> random_points(std::mt19937_64 &rng, size_t n) {
    std::vector<Key> xs;
    std::string desc;
    switch (rng() % 3) {
        case 0: {  // consecutive
            Key start = Key(rng() % 2000001) - 1000000;
            for (size_t i = 0; i < n; ++i) xs.push_back(start + Key(i));
            desc = "dense keys";
            break;
        }
        case 1: {  // gaps 1..100
            Key x = Key(rng() % 1000);
            for (size_t i = 0; i < n; ++i) xs.push_back(x += Key(1 + rng() % 100));
            desc = "gapped keys";
            break;
        }
        default: {  // anywhere in int64, ends included sometimes
            std::set<Key> s;
            if (rng() % 2) s.insert(KEY_MIN);
            if (rng() % 2) s.insert(KEY_MAX);
            while (s.size() < n) s.insert(Key(rng()));
            xs.assign(s.begin(), s.end());
            xs.resize(n);
            desc = "full-range keys";
        }
    }

    std::vector<int64_t> slots(n);
    int64_t slot = 0;
    switch (rng() % 5) {
        case 0:
            for (size_t i = 0; i < n; ++i) slots[i] = int64_t(i);
            desc += ", ranks";
            break;
        case 1:  // a PMA's slots: gaps of 0..3
            for (size_t i = 0; i < n; ++i) slots[i] = slot += int64_t(i ? 1 + rng() % 4 : 0);
            desc += ", PMA-like slots";
            break;
        case 2:  // steps of 0 or 1: not a PMA's, but valid points
            for (size_t i = 0; i < n; ++i) slots[i] = slot += int64_t(rng() % 2);
            desc += ", flat slots";
            break;
        case 3:  // no order
            for (size_t i = 0; i < n; ++i) slots[i] = int64_t(rng() % (4 * n + 1));
            desc += ", random slots";
            break;
        default:  // huge: up to 2^49 apart, below MAX_CAPACITY for n <= 255
            for (size_t i = 0; i < n; ++i) slots[i] = slot += int64_t(rng() % (uint64_t(1) << 49));
            desc += ", huge slots";
    }
    std::vector<Pt> points(n);
    for (size_t i = 0; i < n; ++i) points[i] = {xs[i], 2 * slots[i]};
    return {points, desc};
}

// The fewest segments, trying every split: O(n^2) brute force fits.
size_t dp_optimal(const std::vector<Pt> &points, int64_t k) {
    size_t n = points.size();
    std::vector<size_t> best(n + 1, n + 1);
    best[0] = 0;
    for (size_t j = 1; j <= n; ++j) {
        for (size_t i = 0; i < j; ++i) {
            std::vector<Pt> run(points.begin() + long(i), points.begin() + long(j));
            if (best[i] + 1 < best[j] && brute_fits(run, k)) best[j] = best[i] + 1;
        }
    }
    return best[n];
}

// What is wrong with Fitter on these points, or nullptr.
const char *fitter_problem(const std::vector<Pt> &points, int64_t k, std::mt19937_64 &rng) {
    // Greedy segments, each line checked on its points.
    Fitter f(k);
    std::vector<size_t> starts;
    auto line_fits = [&](size_t begin, size_t end, const ExactLine &line) {
        for (size_t i = begin; i < end; ++i) {
            if (!line.within(points[i], k)) return false;
        }
        return true;
    };
    for (size_t i = 0; i < points.size(); ++i) {
        if (f.size() > 0 && f.add(points[i])) {
            if (!line_fits(starts.back(), i + 1, f.line())) return "line after an add";
            continue;
        }
        if (f.size() > 0) {
            if (f.size() != i - starts.back()) return "a failed add changed the size";
            if (!line_fits(starts.back(), i, f.line())) return "line after a failed add";
        }
        f.reset();
        if (!f.add(points[i])) return "a first point rejected";
        starts.push_back(i);
    }
    if (starts != brute_starts(points, k)) return "segments differ from BruteORourke";
    if (points.size() <= 10 && starts.size() != dp_optimal(points, k)) return "greedy is not optimal";

    // Skip the points that do not fit instead: a failed add must leave the
    // fitter as it was, so it goes on with the points kept.
    if (points.size() <= 40) {
        f.reset();
        std::vector<Pt> kept;
        for (const Pt &p : points) {
            if (f.add(p)) {
                kept.push_back(p);
                if (rng() % 4 == 0 && !brute_fits(kept, k)) return "kept points do not fit";
            } else {
                kept.push_back(p);
                bool fits = brute_fits(kept, k);
                kept.pop_back();
                if (fits) return "add rejected a point that fits";
            }
        }
        if (!brute_fits(kept, k)) return "kept points do not fit";
        for (const Pt &p : kept) {
            if (!f.line().within(p, k)) return "line of the kept points";
        }
    }
    return nullptr;
}

int test_fitter(int iterations, size_t max_n, std::mt19937_64 &rng) {
    int failures = 0;
    for (int it = 0; it < iterations; ++it) {
        size_t n = 1 + rng() % std::min<size_t>(max_n, 255);
        int64_t k = KS[rng() % KS.size()];
        auto [points, desc] = random_points(rng, n);
        std::string problem;
        try {
            if (const char *p = fitter_problem(points, k, rng)) problem = p;
        } catch (const std::exception &e) {
            problem = std::string("exception: ") + e.what();
        }
        if (!problem.empty()) {
            std::cerr << "fitter case " << it << " (n=" << n << ", k=" << k << ", " << desc << "): " << problem
                      << "\n";
            ++failures;
        }
    }
    std::cout << "fitter:   " << iterations << " cases, " << failures << " failures\n";
    return failures;
}

// ---------------------------------------------------------------- keys

// n keys (duplicates possible) in one of several patterns, and a description.
std::pair<std::vector<Key>, std::string> random_keys(std::mt19937_64 &rng, size_t n) {
    std::vector<Key> keys(n);
    Key range = Key(rng() % 2 ? n : 4 * n + 1);
    switch (rng() % 7) {
        case 0:
            for (Key &k : keys) k = Key(rng() % uint64_t(range));
            return {keys, "random"};
        case 1:
            for (Key &k : keys) k = Key(rng() % uint64_t(range));
            std::sort(keys.begin(), keys.end());
            return {keys, "ascending"};
        case 2:
            for (Key &k : keys) k = Key(rng() % uint64_t(range));
            std::sort(keys.rbegin(), keys.rend());
            return {keys, "descending"};
        case 3:  // most keys near a few centres
            for (Key &k : keys) k = Key(rng() % 4) * range + Key(rng() % 8);
            return {keys, "clustered"};
        case 4:  // anywhere in int64, the ends included
            for (Key &k : keys) {
                switch (rng() % 8) {
                    case 0: k = KEY_MIN + Key(rng() % 3); break;
                    case 1: k = KEY_MAX - Key(rng() % 3); break;
                    default: k = Key(rng());
                }
            }
            return {keys, "full range"};
        case 5: {  // about evenly spaced, random order: long segments
            for (size_t i = 0; i < n; ++i) keys[i] = Key(i) * 1000 + Key(rng() % 3);
            std::shuffle(keys.begin(), keys.end(), rng);
            return {keys, "even"};
        }
        default:  // dense near 0, sparse far away
            for (Key &k : keys) k = rng() % 2 ? Key(rng() % 64) : Key(rng() % (uint64_t(1) << 40));
            return {keys, "two scales"};
    }
}

// ---------------------------------------------------------------- segments

// Splits s around gap and compares the pieces with ones built from scratch.
template <class H>
const char *split_problem(const lpma::Segment<H> &s, SlotRange gap, const PointView &pts, int64_t k) {
    using Seg = lpma::Segment<H>;
    std::optional<SlotRange> left, right;
    for (size_t slot = s.slots.begin; slot < s.slots.end; ++slot) {
        if (!pts.occupied(slot)) continue;
        if (slot < gap.begin) left = SlotRange{left ? left->begin : slot, slot + 1};
        if (slot >= gap.end) right = SlotRange{right ? right->begin : slot, slot + 1};
    }
    Seg copy = s;
    auto [l, r] = Seg::split(std::move(copy), gap, pts);
    for (auto [piece, want] : {std::pair{&l, left}, std::pair{&r, right}}) {
        if (piece->has_value() != want.has_value()) return "split: wrong pieces";
        if (!want) continue;
        const Seg &got = **piece;
        if (got.slots != *want || got.first != pts.key(want->begin) || got.last != pts.key(want->end - 1))
            return "split: a piece's ends";
        if (got.line != s.line) return "split: a piece did not keep the line";
        if (!(got.hull == H::build(pts, *want))) return "split: a piece's hull";
        if (!got.check(pts, k)) return "split: a piece's check";
    }
    return nullptr;
}

// A gap of slots near s, maybe empty, maybe outside it.
SlotRange random_gap(std::mt19937_64 &rng, SlotRange s) {
    size_t lo = s.begin >= 3 ? s.begin - 3 : 0;
    size_t begin = lo + rng() % (s.end + 3 - lo + 1);
    return {begin, begin + rng() % (s.end - s.begin + 4)};
}

// Joins and splits segments with hull H over runs ls and rs of the points,
// lines ll and rl. The joined line goes to joined.
template <class H>
const char *join_problem(const PointView &pts, SlotRange ls, const ExactLine &ll, SlotRange rs, const ExactLine &rl,
                         int64_t k, bool feasible, std::optional<ExactLine> &joined, std::mt19937_64 &rng) {
    using Seg = lpma::Segment<H>;
    Seg l = Seg::build(pts, ls, ll), r = Seg::build(pts, rs, rl);
    if (!l.check(pts, k) || !r.check(pts, k)) return "build";
    Seg l_copy = l, r_copy = r;
    auto j = Seg::try_join(l, r, pts, k);
    if (!(l == l_copy) || !(r == r_copy)) return "try_join changed its inputs";
    if (j.has_value() != feasible) return "try_join disagrees with brute force";
    if (j) {
        if (j->first != l.first || j->last != r.last || j->slots != SlotRange{ls.begin, rs.end})
            return "joined ends";
        if (!j->check(pts, k)) return "joined segment's check";
        joined = j->line;
        if (const char *p = split_problem(*j, random_gap(rng, j->slots), pts, k)) return p;
    }
    return split_problem(l, random_gap(rng, l.slots), pts, k);
}

template <class Storage>
int segments_case(std::mt19937_64 &rng, size_t n, int64_t k, const std::string &desc) {
    Storage pma;
    auto [keys, pattern] = random_keys(rng, n);
    for (Key key : keys) pma.insert(key);
    PointView pts{pma};
    std::vector<size_t> slots;
    for (size_t s = 0; s < pma.capacity(); ++s) {
        if (pma.occupied(s)) slots.push_back(s);
    }

    // A run from slots[i] of at most max_len points that one line fits.
    auto run = [&](size_t i, size_t max_len) {
        Fitter &f = lpma::scratch_fitter(k);
        size_t j = i;
        while (j < slots.size() && j - i < max_len && f.add(pts.at(slots[j]))) ++j;
        return std::pair{j, f.line()};
    };

    int failures = 0;
    for (int trial = 0; trial < 20 && slots.size() >= 2; ++trial) {
        size_t i = rng() % (slots.size() - 1);
        auto [j, ll] = run(i, 1 + rng() % 30);
        if (j == slots.size()) continue;
        auto [end, rl] = run(j, 1 + rng() % 30);
        SlotRange ls{slots[i], slots[j - 1] + 1}, rs{slots[j], slots[end - 1] + 1};
        std::vector<Pt> both = points_of(pts, {ls.begin, rs.end});
        bool feasible = brute_fits(both, k);

        std::optional<ExactLine> line0, line1;
        std::string p;
        try {
            const char *q = join_problem<ScanHull>(pts, ls, ll, rs, rl, k, feasible, line0, rng);
            if (!q) q = join_problem<VectorHull>(pts, ls, ll, rs, rl, k, feasible, line1, rng);
            if (!q && line0.has_value() != line1.has_value()) q = "T0 and T1 disagree";
            if (!q && line0 && !line0->same(*line1)) q = "T0 and T1 found different lines";
            if (q) p = q;
        } catch (const std::exception &e) {
            p = std::string("exception: ") + e.what();
        }
        if (!p.empty()) {
            std::cerr << desc << " (" << pattern << " keys, " << n << " inserts, k=" << k << ") trial " << trial
                      << ": " << p << "\n";
            ++failures;
        }
    }
    return failures;
}

int test_segments(int iterations, size_t max_n, std::mt19937_64 &rng) {
    int failures = 0;
    for (int it = 0; it < iterations; ++it) {
        size_t n = 2 + rng() % max_n;
        int64_t k = KS[rng() % KS.size()];
        std::string desc = "segments case " + std::to_string(it);
        failures += rng() % 2 ? segments_case<PMA<>>(rng, n, k, desc + " PMA")
                              : segments_case<Sparse>(rng, n, k, desc + " sparse PMA");
    }
    std::cout << "segments: " << iterations << " cases, " << failures << " failures\n";
    return failures;
}

// ---------------------------------------------------------------- index

// Totals over the index cases, to show what they exercised.
struct Coverage {
    uint64_t inserts = 0, inside = 0, grows = 0, splits = 0, attempts = 0, joins = 0, brute = 0;
};

template <class A, class B>
bool same_segments(const A &a, const B &b) {
    if (a.segments().size() != b.segments().size()) return false;
    auto i = a.segments().begin();
    for (const auto &[key, s] : b.segments()) {
        const auto &t = (i++)->second;
        if (t.first != s.first || t.last != s.last || t.slots != s.slots || !t.line.same(s.line)) return false;
    }
    return true;
}

// Brute-force versions of check()'s last two conditions, or nullptr.
template <class Index>
const char *brute_problem(const Index &index) {
    PointView pts{index.pma()};
    std::vector<std::vector<Pt>> runs;
    for (const auto &[key, s] : index.segments()) runs.push_back(points_of(pts, s.slots));
    for (size_t i = 0; i + 1 < runs.size(); ++i) {
        std::vector<Pt> both = runs[i];
        both.insert(both.end(), runs[i + 1].begin(), runs[i + 1].end());
        if (brute_fits(both, index.k())) return "brute force: two neighbours can be joined";
    }
    std::vector<Pt> all = points_of(pts, {0, index.pma().capacity()});
    size_t optimal = brute_starts(all, index.k()).size();
    if (optimal > 0 && runs.size() > 2 * optimal - 1) return "brute force: more than 2 * optimal - 1 segments";
    return nullptr;
}

// The keys to query: every key, its neighbours, the extremes, some random ones.
std::vector<Key> probes(const std::set<Key> &keys, std::mt19937_64 &rng) {
    std::vector<Key> out = {KEY_MIN, KEY_MAX, 0};
    for (Key key : keys) {
        out.push_back(key);
        if (key > KEY_MIN) out.push_back(key - 1);
        if (key < KEY_MAX) out.push_back(key + 1);
    }
    for (int i = 0; i < 4; ++i) out.push_back(Key(rng()));
    if (!keys.empty()) {
        uint64_t span = uint64_t(*keys.rbegin()) - uint64_t(*keys.begin());
        for (int i = 0; i < 4; ++i) out.push_back(Key(uint64_t(*keys.begin()) + (span ? rng() % span : 0)));
    }
    return out;
}

template <class Storage>
int index_case(const std::vector<Key> &keys, int64_t k, const std::string &desc, std::mt19937_64 &rng,
               Coverage &coverage) {
    lpma::LearnedPMA<ScanHull, Storage> t0(k);
    lpma::LearnedPMA<VectorHull, Storage> t1(k);
    std::set<Key> expected;

    // Inserts x into both and checks everything: what is wrong, or nullptr.
    auto step = [&](Key x) -> const char * {
        bool fresh = !expected.count(x);
        auto it = t0.segments().upper_bound(x);
        if (fresh && it != t0.segments().begin() && std::prev(it)->second.first < x && x < std::prev(it)->second.last)
            ++coverage.inside;
        auto segments_before = t1.segments();
        auto stats0 = t0.stats(), stats1 = t1.stats();

        bool inserted0 = t0.insert(x), inserted1 = t1.insert(x);
        expected.insert(x);
        if (inserted0 != fresh || inserted1 != fresh) return "insert's return value";
        if (!fresh && (t1.segments() != segments_before || t0.stats() != stats0 || t1.stats() != stats1))
            return "a duplicate changed the index";
        if (const char *p = t0.problem()) return p;
        if (const char *p = t1.problem()) return p;
        if (!same_segments(t0, t1)) return "T0 and T1 have different segments";
        if (t0.stats().join_attempts - stats0.join_attempts > 4) return "more than 4 join attempts";
        if (t0.size() != expected.size()) return "size";
        if (fresh && expected.size() <= 24) {
            if (const char *p = brute_problem(t1)) return p;
            ++coverage.brute;
        }
        for (Key probe : probes(expected, rng)) {
            auto want = expected.lower_bound(probe);
            std::optional<Key> answer;
            if (want != expected.end()) answer = *want;
            if (t0.lower_bound(probe) != answer || t1.lower_bound(probe) != answer) return "lower_bound";
            bool present = expected.count(probe);
            if (t0.contains(probe) != present || t1.contains(probe) != present) return "contains";
        }
        return nullptr;
    };

    for (size_t i = 0; i < keys.size(); ++i) {
        std::string problem;
        try {
            if (const char *p = step(keys[i])) problem = p;
        } catch (const std::exception &e) {
            problem = std::string("exception: ") + e.what();
        }
        if (!problem.empty()) {
            std::cerr << desc << ": " << problem << " after insert " << i << " (key " << keys[i] << ", "
                      << expected.size() << " keys, " << t0.segment_count() << " segments)\n";
            return 1;
        }
    }
    const auto &s = t0.stats();
    coverage.inserts += s.inserts;
    coverage.grows += s.grows;
    coverage.splits += s.splits;
    coverage.attempts += s.join_attempts;
    coverage.joins += s.joins;
    return 0;
}

int test_index(int iterations, size_t max_n, std::mt19937_64 &rng) {
    int failures = 0;
    Coverage coverage;
    for (int it = 0; it < iterations; ++it) {
        size_t n = 1 + rng() % max_n;
        int64_t k = KS[rng() % KS.size()];
        auto [keys, pattern] = random_keys(rng, n);
        std::string desc = "index case " + std::to_string(it) + " (" + pattern + ", n=" + std::to_string(n) +
                           ", k=" + std::to_string(k) + ")";
        failures += index_case<PMA<>>(keys, k, desc + " PMA", rng, coverage);
        failures += index_case<Sparse>(keys, k, desc + " sparse PMA", rng, coverage);
        failures += index_case<GapPMA>(keys, k, desc + " GapPMA", rng, coverage);
    }
    std::cout << "index:    " << iterations << " cases x 3 PMAs, " << failures << " failures\n"
              << "          " << coverage.inserts << " inserts (" << coverage.inside << " inside a segment, "
              << coverage.grows << " grew the PMA, " << coverage.brute << " checked by brute force), "
              << coverage.splits << " splits, " << coverage.joins << " of " << coverage.attempts
              << " join attempts succeeded\n";
    return failures;
}

int main(int argc, char **argv) {
    int iterations = 200;
    size_t max_n = 150;
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-i" && i + 1 < argc) iterations = std::atoi(argv[++i]);
        else if (a == "-n" && i + 1 < argc) max_n = std::strtoull(argv[++i], nullptr, 10);
        else seed = std::strtoull(a.c_str(), nullptr, 10);
    }
    if (max_n < 1) max_n = 1;
    std::cout << "seed " << seed << "\n";
    std::mt19937_64 rng(seed);
    int failures = test_fitter(iterations, max_n, rng);
    failures += test_segments(iterations, max_n, rng);
    failures += test_index(iterations, max_n, rng);
    return failures ? 1 : 0;
}
