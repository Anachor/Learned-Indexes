#pragma once

// Packed memory array: a sorted array with gaps, so an insert moves only the
// keys near it. Insert only, keys distinct.
//
// The capacity N is a power of two, split into leaves of B slots, B the smallest
// power of two >= log2(N). Above the leaves sits an implicit binary tree: the
// window at height h is 2^h leaves aligned on a multiple of 2^h, the root
// (height H = log2(N / B)) the whole array. A window at height h may hold up to
// tau_h of its slots, tau_h going linearly from leaf_upper at the leaves to
// root_upper at the root.
//
// insert(key):
//   1. find the predecessor; the key goes in the predecessor's leaf (leaf 0 if
//      there is none);
//   2. if the leaf stays within leaf_upper, put the key right after its
//      predecessor, shifting the keys up to the nearest empty slot of the leaf;
//   3. otherwise take the lowest window above it that stays within its bound
//      with the key, and spread its keys and the new one evenly over it;
//   4. if not even the root does, grow: double N and spread every key evenly.
//
// Growing is lazy: the root bound is only checked when a leaf overflows, so the
// array can be denser than root_upper until one does.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "packed_memory_array.hpp"

struct PMAParams {
    double leaf_upper = 1.0;       // max density of a leaf
    double root_upper = 0.75;      // max density of the whole array
    size_t initial_capacity = 16;  // a power of two, >= 2
};

template <PMAParams P = PMAParams{}>
class PMA : public PackedMemoryArray {
    static_assert(0 < P.root_upper && P.root_upper <= P.leaf_upper && P.leaf_upper <= 1);
    static_assert(P.initial_capacity >= 2 && std::has_single_bit(P.initial_capacity));

public:
    PMA() { resize(P.initial_capacity); }

    // Inserts key; false if it is already there.
    bool insert(int64_t key, Update *update = nullptr) override {
        if (update) {
            *update = {};
            update->old_capacity = update->new_capacity = capacity();
        }
        size_t p = find_le(key);
        if (p != NONE && keys_[p] == key) return false;
        size_t leaf = p == NONE ? 0 : p / leaf_size_;

        if (fits(count_[leaf] + 1, 0, leaf_size_)) {
            if (update) describe_update(*update, leaf * leaf_size_, leaf_size_, key);
            insert_in_leaf(leaf, p, key);
        } else {
            size_t count = count_[leaf];
            size_t h = 1;
            for (; h <= height_; ++h) {
                size_t first = leaf >> h << h, last = first + (size_t(1) << h);
                count = 0;
                for (size_t l = first; l < last; ++l) count += count_[l];
                if (fits(count + 1, h, leaf_size_ << h)) break;
            }
            if (h <= height_) {
                size_t first = leaf >> h << h;
                if (update) describe_update(*update, first * leaf_size_, leaf_size_ << h, key);
                rebalance(first * leaf_size_, leaf_size_ << h, key);
            } else {
                if (update) describe_update(*update, 0, capacity(), key);
                grow(key);
                if (update) update->new_slots = {0, capacity()};
            }
        }
        ++size_;
        if (update) {
            update->inserted = true;
            update->new_capacity = capacity();
        }
        return true;
    }

    size_t size() const override { return size_; }
    size_t capacity() const override { return keys_.size(); }
    size_t leaf_size() const { return leaf_size_; }
    const Stats &stats() const override { return stats_; }

    // The array itself: slot i is empty or holds a key, keys increasing.
    bool occupied(size_t slot) const override { return used_[slot]; }
    int64_t key_at(size_t slot) const override { return keys_[slot]; }

    using PackedMemoryArray::points;

    // The keys of a physical range in order, with absolute slots.
    void points(std::vector<int64_t> &keys, std::vector<int64_t> &slots,
                SlotRange range) const override {
        check_range(range);
        keys.clear();
        slots.clear();
        for (size_t i = range.begin; i < range.end; ++i) {
            if (!used_[i]) continue;
            keys.push_back(keys_[i]);
            slots.push_back(int64_t(i));
        }
    }

    // Checks the invariants: keys increasing, the leaf counts and size match the
    // slots, no leaf over leaf_upper (rounded up, as a spread can leave it), and
    // the capacity and leaf size follow the rules.
    bool check() const override {
        size_t n = keys_.size();
        if (!std::has_single_bit(n) || leaf_size_ != leaf_size_for(n)) return false;
        if (count_.size() != n / leaf_size_) return false;
        size_t total = 0;
        bool any = false;
        int64_t last = 0;
        for (size_t l = 0; l < count_.size(); ++l) {
            size_t count = 0;
            for (size_t i = l * leaf_size_; i < (l + 1) * leaf_size_; ++i) {
                if (!used_[i]) continue;
                if (any && keys_[i] <= last) return false;
                any = true;
                last = keys_[i];
                ++count;
            }
            if (count != count_[l] || count > leaf_size_) return false;
            total += count;
        }
        return total == size_;
    }

private:
    static constexpr size_t NONE = size_t(-1);

    std::vector<int64_t> keys_;
    std::vector<uint8_t> used_;
    std::vector<size_t> count_;  // keys per leaf
    size_t size_ = 0;
    size_t leaf_size_ = 0;
    size_t height_ = 0;  // H, the root's height
    Stats stats_;
    std::vector<int64_t> buffer_;  // a window's keys during a rebalance

    // Capture key boundaries before overwriting slots. Reporting an entire
    // leaf for a local shift avoids coupling clients to the shifting policy.
    // This scan is skipped when the caller does not request a report.
    void describe_update(Update &update, size_t begin, size_t slots, int64_t key) const {
        update.old_slots = update.new_slots = {begin, begin + slots};
        KeyRange keys{key, key};
        for (size_t i = begin; i < begin + slots; ++i) {
            if (!used_[i]) continue;
            keys.first = std::min(keys.first, keys_[i]);
            keys.last = std::max(keys.last, keys_[i]);
        }
        update.affected_keys = keys;
    }

    // The smallest power of two >= log2(capacity), at most the capacity.
    static size_t leaf_size_for(size_t capacity) {
        size_t log = size_t(std::countr_zero(capacity));
        return std::min(capacity, std::bit_ceil(log));
    }

    // tau_h: the root's bound when the root is a single leaf.
    double bound(size_t h) const {
        if (height_ == 0) return P.root_upper;
        return P.leaf_upper - (P.leaf_upper - P.root_upper) * double(h) / double(height_);
    }

    bool fits(size_t count, size_t h, size_t slots) const { return double(count) <= bound(h) * double(slots); }

    // An empty array of this capacity.
    void resize(size_t capacity) {
        keys_.assign(capacity, 0);
        used_.assign(capacity, 0);
        leaf_size_ = leaf_size_for(capacity);
        count_.assign(capacity / leaf_size_, 0);
        height_ = size_t(std::countr_zero(capacity / leaf_size_));
    }

    // The last slot whose key is <= key, or NONE.
    size_t find_le(int64_t key) const {
        size_t lo = 0, hi = keys_.size(), best = NONE;
        // occupied slots below lo hold keys <= key, those from hi on keys > key
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2, s = mid;
            while (s > lo && !used_[s]) --s;
            if (!used_[s]) {
                lo = mid + 1;  // lo..mid all empty
            } else if (keys_[s] <= key) {
                best = s;
                lo = mid + 1;  // s+1..mid empty
            } else {
                hi = s;
            }
        }
        return best;
    }

    // Puts key right after slot p (at the leaf's start when p is NONE),
    // shifting keys toward the nearest empty slot of the leaf, which has one.
    void insert_in_leaf(size_t leaf, size_t p, int64_t key) {
        size_t begin = leaf * leaf_size_, end = begin + leaf_size_;
        size_t q = p == NONE ? begin : p + 1;  // the first slot after p

        size_t right = q;
        while (right < end && used_[right]) ++right;
        size_t left = NONE;
        if (p != NONE) {
            for (size_t s = p + 1; s-- > begin;) {
                if (!used_[s]) {
                    left = s;
                    break;
                }
            }
        }

        if (right < end && (left == NONE || right - q <= p - left)) {
            for (size_t s = right; s > q; --s) place(s, keys_[s - 1]);
            stats_.moves += right - q;
            place(q, key);
        } else {
            for (size_t s = left; s < p; ++s) place(s, keys_[s + 1]);
            stats_.moves += p - left;
            place(p, key);
        }
        ++stats_.moves;
        ++count_[leaf];
    }

    void place(size_t slot, int64_t key) {
        keys_[slot] = key;
        used_[slot] = 1;
    }

    // Spreads the keys of slots begin..begin+slots-1 and key evenly over them:
    // the j-th of m at begin + floor(j * slots / m).
    void rebalance(size_t begin, size_t slots, int64_t key) {
        buffer_.clear();
        bool added = false;
        for (size_t i = begin; i < begin + slots; ++i) {
            if (!used_[i]) continue;
            if (!added && key < keys_[i]) {
                buffer_.push_back(key);
                added = true;
            }
            buffer_.push_back(keys_[i]);
            used_[i] = 0;
        }
        if (!added) buffer_.push_back(key);
        spread(begin, slots);
        ++stats_.rebalances;
    }

    // Doubles the capacity until the keys and key fit within root_upper, and
    // spreads them all evenly.
    void grow(int64_t key) {
        buffer_.clear();
        bool added = false;
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (!used_[i]) continue;
            if (!added && key < keys_[i]) {
                buffer_.push_back(key);
                added = true;
            }
            buffer_.push_back(keys_[i]);
        }
        if (!added) buffer_.push_back(key);

        size_t capacity = 2 * keys_.size();
        while (double(buffer_.size()) > P.root_upper * double(capacity)) capacity *= 2;
        resize(capacity);
        spread(0, capacity);
        ++stats_.grows;
    }

    // Writes buffer_ evenly over slots begin..begin+slots-1, which are empty,
    // and recounts their leaves.
    void spread(size_t begin, size_t slots) {
        size_t m = buffer_.size();
        for (size_t l = begin / leaf_size_; l < (begin + slots) / leaf_size_; ++l) count_[l] = 0;
        for (size_t j = 0; j < m; ++j) {
            size_t slot = begin + j * slots / m;
            place(slot, buffer_[j]);
            ++count_[slot / leaf_size_];
        }
        stats_.moves += m;
    }
};
