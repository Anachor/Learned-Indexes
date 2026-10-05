// Backend-independent PMA contract tests, including a separate dense backend.
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/PMA/pma.hpp"

using Key = PackedMemoryArray::Key;
using Range = PackedMemoryArray::SlotRange;
using Update = PackedMemoryArray::Update;

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

// A test-only implementation with packed keys followed by gaps, no leaves,
// and full-array rebuilding. It exercises clients without the current PMA's
// density hierarchy, shifting policy, or power-of-two leaf assumptions.
class DensePMA final : public PackedMemoryArray {
    std::vector<Key> keys_;
    size_t capacity_ = 3;
    Stats stats_;

public:
    bool insert(Key key, Update *update = nullptr) override {
        if (update) {
            *update = {};
            update->old_capacity = update->new_capacity = capacity_;
        }
        auto at = std::lower_bound(keys_.begin(), keys_.end(), key);
        if (at != keys_.end() && *at == key) return false;

        size_t old_capacity = capacity_;
        std::vector<Key> next;
        next.reserve(keys_.size() + 1);
        next.insert(next.end(), keys_.begin(), at);
        next.push_back(key);
        next.insert(next.end(), at, keys_.end());
        keys_.swap(next);
        if (keys_.size() > capacity_) {
            capacity_ *= 2;
            ++stats_.grows;
        } else {
            ++stats_.rebalances;
        }
        stats_.moves += keys_.size();
        if (update) {
            update->inserted = true;
            update->new_capacity = capacity_;
            update->old_slots = {0, old_capacity};
            update->new_slots = {0, capacity_};
            update->affected_keys = KeyRange{keys_.front(), keys_.back()};
        }
        return true;
    }

    size_t size() const override { return keys_.size(); }
    size_t capacity() const override { return capacity_; }
    bool occupied(size_t slot) const override { return slot < keys_.size(); }
    Key key_at(size_t slot) const override { return keys_[slot]; }
    const Stats &stats() const override { return stats_; }
    bool check() const override {
        return keys_.size() <= capacity_ &&
               std::adjacent_find(keys_.begin(), keys_.end(), std::greater_equal<Key>()) == keys_.end();
    }
    using PackedMemoryArray::points;
    void points(std::vector<Key> &keys, std::vector<int64_t> &slots, Range range) const override {
        check_range(range);
        keys.clear();
        slots.clear();
        for (size_t slot = range.begin; slot < std::min(range.end, keys_.size()); ++slot) {
            keys.push_back(keys_[slot]);
            slots.push_back(int64_t(slot));
        }
    }
};

std::vector<std::optional<Key>> snapshot(const PackedMemoryArray &pma) {
    std::vector<std::optional<Key>> result(pma.capacity());
    for (size_t slot = 0; slot < result.size(); ++slot)
        if (pma.occupied(slot)) result[slot] = pma.key_at(slot);
    return result;
}

void check_points(const PackedMemoryArray &pma, Range range) {
    std::vector<Key> keys{99};
    std::vector<int64_t> slots{-1};
    pma.points(keys, slots, range);
    std::vector<Key> expected_keys;
    std::vector<int64_t> expected_slots;
    for (size_t slot = range.begin; slot < range.end; ++slot) {
        if (!pma.occupied(slot)) continue;
        expected_keys.push_back(pma.key_at(slot));
        expected_slots.push_back(int64_t(slot));
    }
    require(keys == expected_keys && slots == expected_slots, "range enumeration or absolute slots");
}

void check_invalid_range(const PackedMemoryArray &pma, Range range) {
    std::vector<Key> keys{7, 8};
    std::vector<int64_t> slots{2, 3};
    bool threw = false;
    try {
        pma.points(keys, slots, range);
    } catch (const std::out_of_range &) {
        threw = true;
    }
    require(threw && keys == std::vector<Key>({7, 8}) && slots == std::vector<int64_t>({2, 3}),
            "invalid range must preserve outputs");
}

void exercise(PackedMemoryArray &reported, PackedMemoryArray &silent, const std::vector<Key> &trace) {
    require(reported.empty() && reported.check(), "empty PMA");
    check_points(reported, {0, reported.capacity()});
    check_points(reported, {reported.capacity(), reported.capacity()});
    check_invalid_range(reported, {1, 0});
    check_invalid_range(reported, {0, reported.capacity() + 1});

    std::set<Key> expected;
    for (Key key : trace) {
        auto before = snapshot(reported);
        auto stats_before = reported.stats();
        Update update{true, 99, 88, {9, 10}, {8, 9}, PackedMemoryArray::KeyRange{7, 8}};
        bool fresh = expected.insert(key).second;
        require(reported.insert(key, &update) == fresh, "reported insert result");
        require(silent.insert(key) == fresh, "unreported insert result");
        auto after = snapshot(reported);
        require(reported.check() && silent.check(), "implementation invariants");
        require(after == snapshot(silent) && reported.stats() == silent.stats(),
                "reporting must preserve layout and statistics");
        require(reported.size() == expected.size() && !reported.empty(), "size");

        std::vector<Key> keys;
        std::vector<int64_t> slots;
        reported.points(keys, slots);
        require(keys == std::vector<Key>(expected.begin(), expected.end()) && keys.size() == slots.size(),
                "contents and full enumeration");
        check_points(reported, {0, reported.capacity()});
        check_points(reported, {reported.capacity() / 3, reported.capacity() * 2 / 3});
        check_points(reported, {reported.capacity(), reported.capacity()});

        require(update.inserted == fresh && update.old_capacity == before.size() &&
                    update.new_capacity == after.size(), "update status and capacities");
        if (!fresh) {
            require(before == after && stats_before == reported.stats(), "duplicate must preserve PMA");
            require(update.old_slots == Range{} && update.new_slots == Range{} && !update.affected_keys,
                    "duplicate must clear report");
            continue;
        }

        require(update.old_slots.begin <= update.old_slots.end && update.old_slots.end <= before.size() &&
                    update.new_slots.begin < update.new_slots.end && update.new_slots.end <= after.size(),
                "reported slot ranges");
        if (update.grew()) {
            require(update.old_slots == Range{0, before.size()} && update.new_slots == Range{0, after.size()},
                    "growth must report complete old and new arrays");
        }

        std::vector<Key> expected_region;
        for (size_t slot = 0; slot < before.size(); ++slot) {
            if (update.old_slots.contains(slot)) {
                if (before[slot]) expected_region.push_back(*before[slot]);
            } else {
                require(slot < after.size() && before[slot] == after[slot], "old outside slots must be unchanged");
            }
        }
        for (size_t slot = 0; slot < after.size(); ++slot) {
            if (!update.new_slots.contains(slot))
                require(slot < before.size() && before[slot] == after[slot], "new outside slots must be unchanged");
        }
        expected_region.insert(std::lower_bound(expected_region.begin(), expected_region.end(), key), key);
        reported.points(keys, slots, update.new_slots);
        require(keys == expected_region, "region must contain all old affected points and the new key");
        require(update.affected_keys == PackedMemoryArray::KeyRange{keys.front(), keys.back()},
                "inclusive affected key bounds");
        for (size_t slot = 0; slot < after.size(); ++slot) {
            if (!after[slot] || update.new_slots.contains(slot)) continue;
            require(*after[slot] < keys.front() || *after[slot] > keys.back(), "affected keys must be consecutive");
        }
        check_points(reported, update.new_slots);
    }
}

template <class Backend>
void run(const std::vector<Key> &trace) {
    Backend reported, silent;
    exercise(reported, silent, trace);
}

int main() {
    try {
        std::vector<std::vector<Key>> traces{
            {},
            {0, std::numeric_limits<Key>::min(), std::numeric_limits<Key>::max(), -1, 1,
             std::numeric_limits<Key>::max(), std::numeric_limits<Key>::min(), 0,
             std::numeric_limits<Key>::min() + 1, std::numeric_limits<Key>::max() - 1},
            {}, {}, {}, {}
        };
        for (Key key = 0; key < 512; ++key) traces[2].push_back(key);
        traces[3] = traces[2];
        std::reverse(traces[3].begin(), traces[3].end());
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<Key> full(std::numeric_limits<Key>::min(), std::numeric_limits<Key>::max());
        for (size_t i = 0; i < 512; ++i) {
            traces[4].push_back(full(rng));
            if (i % 8 == 0) traces[4].push_back(traces[4].front());
            traces[5].push_back(Key(rng() % 257) - 128);
        }
        for (const auto &trace : traces) {
            run<PMA<>>(trace);
            run<PMA<PMAParams{.leaf_upper = 0.9, .root_upper = 0.5, .initial_capacity = 2}>>(trace);
            run<DensePMA>(trace);
        }
        std::cout << "PMA API: all contract checks passed for 3 backends/configurations\n";
    } catch (const std::exception &error) {
        std::cerr << "PMA API: " << error.what() << '\n';
        return 1;
    }
}
