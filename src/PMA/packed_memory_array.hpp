#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

// Common interface for insertion-only PMAs with distinct int64_t keys.
// Occupied slots are strictly increasing in key order. Gaps have no key value;
// callers must check occupied(slot) before reading key_at(slot).
//
// Implementations choose their own layout, density policy and capacity growth.
// Slot indices, rather than pointers into storage, are exposed. An insertion
// may change existing keys' slots, so cached positions need the Update report.
class PackedMemoryArray {
public:
    using Key = int64_t;

    // Half-open physical slot interval [begin, end).
    struct SlotRange {
        size_t begin = 0, end = 0;
        bool empty() const { return begin == end; }
        bool contains(size_t slot) const { return begin <= slot && slot < end; }
        bool operator==(const SlotRange &) const = default;
    };

    // Inclusive key interval. Endpoints are keys, never last_key + 1.
    struct KeyRange {
        Key first, last;
        bool operator==(const KeyRange &) const = default;
    };

    // Describes a completed insertion; all fields are owned values.
    //
    // On success, old_slots and new_slots cover the entire region that callers
    // must rebuild, including keys that kept their positions. Outside these
    // regions, occupied keys and their slots are unchanged. The region's old
    // keys and the inserted key form one consecutive range in the new key set;
    // affected_keys gives its inclusive endpoints. points(..., new_slots)
    // enumerates all of those keys at their new, absolute physical positions.
    //
    // Growth reports [0, old_capacity) and [0, new_capacity), even when some
    // positions happen to stay the same. Reports describe the layout only
    // until the next successful insertion.
    //
    // A duplicate returns false, leaves the PMA and statistics unchanged, and
    // reports equal capacities, empty slot ranges, and no affected_keys.
    struct Update {
        bool inserted = false;
        size_t old_capacity = 0, new_capacity = 0;
        SlotRange old_slots, new_slots;
        std::optional<KeyRange> affected_keys;
        bool grew() const { return new_capacity > old_capacity; }
        bool operator==(const Update &) const = default;
    };

    // Diagnostics shared by the workloads. moves counts occupied-key writes,
    // including the new key; rebalances counts redistributions without growth;
    // grows counts capacity-growth operations. Policy details are not in this
    // interface. These counters do not bound total running time.
    struct Stats {
        uint64_t moves = 0, rebalances = 0, grows = 0;
        bool operator==(const Stats &) const = default;
    };

    virtual ~PackedMemoryArray() = default;

    // Optional reporting must not change insertion's result or resulting layout.
    // The report is replaced on every normal return. nullptr avoids reporting
    // work; a successful insertion returns true, a duplicate returns false.
    virtual bool insert(Key key, Update *update = nullptr) = 0;

    virtual size_t size() const = 0;
    virtual size_t capacity() const = 0;
    bool empty() const { return size() == 0; }

    // Preconditions: slot < capacity(); key_at also requires occupied(slot).
    virtual bool occupied(size_t slot) const = 0;
    virtual Key key_at(size_t slot) const = 0;

    // Clears distinct output vectors and enumerates occupied slots in physical
    // (and key) order. Positions include gaps and are absolute, not relative
    // to the range or dense ranks. Implementations must keep occupied slot
    // indices representable in int64_t for the existing line-fitting API.
    void points(std::vector<Key> &keys, std::vector<int64_t> &slots) const {
        points(keys, slots, {0, capacity()});
    }

    // Invalid ranges throw std::out_of_range before modifying the outputs.
    virtual void points(std::vector<Key> &keys, std::vector<int64_t> &slots,
                        SlotRange range) const = 0;

    virtual const Stats &stats() const = 0;
    // Checks both this interface's ordering/size contract and implementation-
    // specific invariants. Intended for validation, not the insertion hot path.
    virtual bool check() const = 0;

protected:
    void check_range(SlotRange range) const {
        if (range.begin > range.end || range.end > capacity())
            throw std::out_of_range("PMA slot range outside capacity");
    }
};
