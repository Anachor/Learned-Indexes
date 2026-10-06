#pragma once

// A learned index on a PMA: the keys live in the PMA, and segments of
// consecutive keys each carry a line within delta of their slots, so a lookup
// evaluates one line and searches about 2*delta slots. Insert only.
//
// After an insert, the segments the PMA moved are rebuilt with O'Rourke, the
// ones it cut keep their unmoved parts, and neighbours are joined where one line
// fits. No two neighbours can then be joined, which leaves at most 2 * optimal - 1
// segments. H (src/Hull) is what a segment keeps to test joins. Less orders
// the keys of the segment map and of a lookup's search (tests count with it);
// the PMA's own search does not use it.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "../PMA/pma.hpp"
#include "segment.hpp"

namespace gpla {

template <Hull H, class Storage = PMA<>, class Less = std::less<Key>>
class GPLA {
public:
    using Seg = Segment<H>;

    struct Stats {
        uint64_t inserts = 0, grows = 0;
        uint64_t rebuilt_points = 0, rebuilt_segments = 0;  // segmented again by O'Rourke
        uint64_t splits = 0, join_attempts = 0, joins = 0;
        bool operator==(const Stats &) const = default;
    };

    // k = 2 * delta: the points are (key, 2 * slot).
    explicit GPLA(int64_t k) : k_(k), fitter_(k) {
        if (k < 0 || k > MAX_K) throw std::invalid_argument("GPLA: k must be in [0, 2^58]");
    }

    // Inserts x; false if it is already there.
    bool insert(Key x);

    // The slot of the smallest key >= x.
    std::optional<size_t> lower_bound_slot(Key x) const;

    std::optional<Key> lower_bound(Key x) const {
        auto slot = lower_bound_slot(x);
        if (!slot) return std::nullopt;
        return pma_.key_at(*slot);
    }

    bool contains(Key x) const { return lower_bound(x) == x; }

    size_t size() const { return pma_.size(); }
    size_t segment_count() const { return segments_.size(); }
    int64_t k() const { return k_; }
    const Storage &pma() const { return pma_; }
    const std::map<Key, Seg, Less> &segments() const { return segments_; }  // by first key
    const Stats &stats() const { return stats_; }

    // What is wrong, or nullptr: checks the PMA, every segment, that no two
    // neighbours can be joined, and at most 2 * optimal - 1 segments. For tests.
    const char *problem() const;

private:
    Storage pma_;
    std::map<Key, Seg, Less> segments_;
    int64_t k_;
    Fitter fitter_;
    Stats stats_;

    PointView view() const { return {pma_}; }
    std::vector<Seg> segment(SlotRange r);
    void repair(const PackedMemoryArray::Update &update);
    std::vector<Seg> join_neighbours(std::vector<Seg> &pieces, const std::vector<bool> &rebuilt);
};

template <Hull H, class Storage, class Less>
bool GPLA<H, Storage, Less>::insert(Key x) {
    PackedMemoryArray::Update update;
    if (!pma_.insert(x, &update)) return false;
    if (pma_.capacity() > MAX_CAPACITY) throw std::length_error("GPLA: capacity above 2^57");
    ++stats_.inserts;
    if (update.grew()) {
        ++stats_.grows;
        segments_.clear();
        for (Seg &s : segment({0, pma_.capacity()})) segments_.emplace_hint(segments_.end(), s.first, std::move(s));
    } else {
        repair(update);
    }
    return true;
}

// Replaces the segments around the slots an insert rewrote.
template <Hull H, class Storage, class Less>
void GPLA<H, Storage, Less>::repair(const PackedMemoryArray::Update &update) {
    if (update.old_slots != update.new_slots) throw std::logic_error("GPLA: old and new slots differ");
    PointView pts = view();
    auto [first, last] = *update.affected_keys;
    Less less = segments_.key_comp();

    // [lo, hi): the segments with keys in first..last. [begin, end): also one
    // neighbour on each side, A and B.
    auto lo = segments_.upper_bound(first);
    if (lo != segments_.begin() && !less(std::prev(lo)->second.last, first)) --lo;
    auto hi = segments_.upper_bound(last);
    auto begin = lo == segments_.begin() ? lo : std::prev(lo);
    auto end = hi == segments_.end() ? hi : std::next(hi);

    // L and R: the parts of those segments outside the rewritten slots.
    std::optional<Seg> left, right;
    if (lo != hi) {
        auto back = std::prev(hi);
        bool cut_front = less(lo->second.first, first), cut_back = less(last, back->second.last);
        if (lo == back && (cut_front || cut_back)) {
            std::tie(left, right) = Seg::split(std::move(lo->second), update.old_slots, pts);
            ++stats_.splits;
        } else {
            if (cut_front) {
                left = Seg::split(std::move(lo->second), update.old_slots, pts).first;
                ++stats_.splits;
            }
            if (cut_back) {
                right = Seg::split(std::move(back->second), update.old_slots, pts).second;
                ++stats_.splits;
            }
        }
    }

    // A, L, the rewritten slots segmented again, R, B.
    std::vector<Seg> pieces;
    std::vector<bool> rebuilt;
    auto add = [&](Seg &&s, bool fresh) {
        pieces.push_back(std::move(s));
        rebuilt.push_back(fresh);
    };
    if (begin != lo) add(std::move(begin->second), false);
    if (left) add(std::move(*left), false);
    for (Seg &s : segment(update.new_slots)) add(std::move(s), true);
    if (right) add(std::move(*right), false);
    if (hi != end) add(std::move(hi->second), false);

    std::vector<Seg> joined = join_neighbours(pieces, rebuilt);
    segments_.erase(begin, end);
    for (Seg &s : joined) segments_.emplace_hint(end, s.first, std::move(s));
}

// Joins each piece onto the one before when one line fits. Two rebuilt pieces
// are not tried: greedy made the first one maximal. A pair that fails stays
// apart, as later joins only add to its right side. At most 4 attempts.
template <Hull H, class Storage, class Less>
std::vector<Segment<H>> GPLA<H, Storage, Less>::join_neighbours(std::vector<Seg> &pieces,
                                                                       const std::vector<bool> &rebuilt) {
    std::vector<Seg> joined;
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (i > 0 && !(rebuilt[i - 1] && rebuilt[i])) {
            ++stats_.join_attempts;
            if (auto j = Seg::try_join(joined.back(), pieces[i], view(), k_)) {
                ++stats_.joins;
                joined.back() = std::move(*j);
                continue;
            }
        }
        joined.push_back(std::move(pieces[i]));
    }
    return joined;
}

// Greedy O'Rourke over the points in slots r.
template <Hull H, class Storage, class Less>
std::vector<Segment<H>> GPLA<H, Storage, Less>::segment(SlotRange r) {
    PointView pts = view();
    std::vector<Seg> out;
    size_t begin = NONE, last = NONE;
    fitter_.reset();
    for (size_t s = r.begin; s < r.end; ++s) {
        if (!pma_.occupied(s)) continue;
        Pt p = pts.at(s);
        if (!fitter_.add(p)) {
            out.push_back(Seg::build(pts, {begin, last + 1}, fitter_.line()));
            fitter_.reset();
            fitter_.add(p);
            begin = s;
        }
        if (begin == NONE) begin = s;
        last = s;
        ++stats_.rebuilt_points;
    }
    if (begin != NONE) out.push_back(Seg::build(pts, {begin, last + 1}, fitter_.line()));
    stats_.rebuilt_segments += out.size();
    return out;
}

// The segment around x, then its line: x's successor is at a slot in
// [ceil((line(x) - k) / 2), floor((line(x) + k) / 2)], even when x is absent.
template <Hull H, class Storage, class Less>
std::optional<size_t> GPLA<H, Storage, Less>::lower_bound_slot(Key x) const {
    if (segments_.empty()) return std::nullopt;
    auto next = segments_.upper_bound(x);
    if (next == segments_.begin()) return next->second.slots.begin;  // below every key
    const Seg &s = std::prev(next)->second;
    Less less = segments_.key_comp();
    if (less(s.last, x)) {  // between two segments, or above every key
        if (next == segments_.end()) return std::nullopt;
        return next->second.slots.begin;
    }

    auto [lo_slot, hi_slot] = s.line.slot_window(x, k_);
    auto clamp = [](i128 v, size_t a, size_t b) { return v < i128(a) ? a : v > i128(b) ? b : size_t(v); };
    size_t a = clamp(lo_slot, s.slots.begin, s.slots.end), b = clamp(hi_slot + 1, a, s.slots.end);
    // Binary search, gaps and all: occupied slots before a hold keys < x, from b on keys >= x.
    while (a < b) {
        size_t mid = a + (b - a) / 2, t = mid;
        while (t > a && !pma_.occupied(t)) --t;
        if (!pma_.occupied(t) || less(pma_.key_at(t), x)) a = mid + 1;
        else b = t;
    }
    size_t slot = view().next_occupied(a, s.slots.end);
    if (slot == NONE) throw std::logic_error("GPLA: the successor is not where the line puts it");
    return slot;
}

template <Hull H, class Storage, class Less>
const char *GPLA<H, Storage, Less>::problem() const {
    if (!pma_.check()) return "PMA invariants";
    PointView pts = view();
    size_t covered = 0, next = 0;
    const Seg *prev = nullptr;
    for (const auto &[key, s] : segments_) {
        if (key != s.first) return "a map key is not its segment's first key";
        if (!s.check(pts, k_)) return "a segment's ends, line or hull";
        if (s.slots.begin < next) return "segments overlap";
        if (pts.next_occupied(next, s.slots.begin) != NONE) return "a key outside every segment";
        if (prev && fits(pts, {prev->slots.begin, s.slots.end}, k_)) return "two neighbours can be joined";
        covered += s.size(pts);
        next = s.slots.end;
        prev = &s;
    }
    if (pts.next_occupied(next, pma_.capacity()) != NONE) return "a key outside every segment";
    if (covered != pma_.size()) return "the segments do not cover the keys";
    size_t optimal = optimal_segments(pts, {0, pma_.capacity()}, k_);
    if (optimal > 0 && segments_.size() > 2 * optimal - 1) return "more than 2 * optimal - 1 segments";
    return nullptr;
}

}  // namespace gpla
