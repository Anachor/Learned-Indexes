#pragma once

// A PMA for tests that rewrites as little as the Update contract allows: a key
// goes into the middle of the empty slots between its neighbours, and only
// that slot is reported, so the segment around it is cut with nothing moved.
// When there is no gap, every key is spread out again (doubling the slots when
// more than half full). Linear time per insert.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "../src/PMA/packed_memory_array.hpp"

class GapPMA final : public PackedMemoryArray {
public:
    GapPMA() : keys_(8), used_(8) {}

    bool insert(Key key, Update *update = nullptr) override {
        Update u;
        u.old_capacity = u.new_capacity = capacity();
        if (update) *update = u;

        // The empty slots between key's neighbours: [lo, hi).
        size_t lo = 0, hi = capacity();
        for (size_t s = 0; s < capacity(); ++s) {
            if (!used_[s]) continue;
            if (keys_[s] == key) return false;
            if (keys_[s] > key) {
                hi = s;
                break;
            }
            lo = s + 1;
        }
        if (lo < hi) {
            size_t s = lo + (hi - lo) / 2;
            keys_[s] = key;
            used_[s] = 1;
            ++size_;
            u.old_slots = u.new_slots = {s, s + 1};
            u.affected_keys = KeyRange{key, key};
        } else {
            std::vector<Key> all;
            for (size_t s = 0; s < capacity(); ++s) {
                if (used_[s]) all.push_back(keys_[s]);
            }
            all.insert(std::lower_bound(all.begin(), all.end(), key), key);
            size_t m = all.size(), old = capacity(), cap = old;
            while (2 * m > cap) cap *= 2;
            keys_.assign(cap, 0);
            used_.assign(cap, 0);
            for (size_t j = 0; j < m; ++j) {
                keys_[j * cap / m] = all[j];
                used_[j * cap / m] = 1;
            }
            size_ = m;
            u.new_capacity = cap;
            u.old_slots = {0, old};
            u.new_slots = {0, cap};
            u.affected_keys = KeyRange{all.front(), all.back()};
        }
        u.inserted = true;
        if (update) *update = u;
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
        std::optional<Key> last;
        size_t n = 0;
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
