#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Types/Types.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BECore {

    /// Open-addressing set of entries: slots of `{hash, entry}`, a power-of-two capacity, linear probing, an empty slot has
    /// a null entry. The stored hash lets a probe skip a slot without touching its entry. The owner (`PoolStringStorage`) holds the
    /// shared lock around `Find` and the unique lock around `Insert`, which also grows the table. Only the slots move on
    /// growth: entries live in blocks and never move, so a `const T*` taken out of the table stays valid.
    template <typename T>
    class PoolEntryTable {
        struct Slot {
            uint64_t hash = 0;
            const T* entry = nullptr;
        };

        /// The table doubles when it would hold more than capacity / kMaxLoadDivisor entries
        static constexpr size_t kMaxLoadDivisor = 2;

        Vector<Slot> _slots;
        size_t _used = 0;
        size_t _longestProbe = 0;

        [[nodiscard]] size_t Mask() const noexcept {
            return _slots.size() - 1;
        }

        /// Returns how many slots past its home slot the entry landed in
        static size_t Place(Vector<Slot>& slots, uint64_t hash, const T* entry) noexcept {
            const size_t mask = slots.size() - 1;
            size_t idx = static_cast<size_t>(hash) & mask;
            size_t probe = 0;
            while (slots[idx].entry != nullptr) {
                idx = (idx + 1) & mask;
                ++probe;
            }
            slots[idx] = Slot{hash, entry};
            return probe;
        }

        /// Re-places every slot from its stored hash into `capacity` slots (a power of two)
        void Rehash(size_t capacity) {
            Vector<Slot> grown(capacity);
            size_t longest = 0;
            for (const Slot& slot : _slots) {
                if (slot.entry != nullptr) {
                    longest = std::max(longest, Place(grown, slot.hash, slot.entry));
                }
            }
            _slots = eastl::move(grown);
            _longestProbe = longest;
        }

    public:
        /// The most entries `Reserve` prepares for: at the load limit that is a table of 2^23 slots, 128 MB with 16-byte
        /// slots. A count above it is a hint that came from a file header or a bug, so it is ignored instead of taking
        /// that much memory (or wrapping the arithmetic). `Insert` is not limited by it: a real load grows the table as needed.
        static constexpr size_t kMaxReservedEntries = size_t{1} << 22;

        explicit PoolEntryTable(size_t initialCapacity) : _slots(initialCapacity) {
            ASSERT(initialCapacity >= 2 && (initialCapacity & (initialCapacity - 1)) == 0);
        }

        /// The entry must not be in the table yet (the caller has just looked it up under the same unique lock)
        void Insert(uint64_t hash, const T* entry) {
            if ((_used + 1) * kMaxLoadDivisor > _slots.size()) {
                Rehash(_slots.size() * 2);
            }
            _longestProbe = std::max(_longestProbe, Place(_slots, hash, entry));
            ++_used;
        }

        /// Grows the table once so that `entries` in all fit under the load limit; does nothing when they already do or
        /// when `entries` is above `kMaxReservedEntries`. A batch calls it before its inserts, so the table is re-placed
        /// once however many entries the batch adds.
        void Reserve(size_t entries) {
            if (entries > kMaxReservedEntries) {
                return;
            }
            // `entries * kMaxLoadDivisor` cannot overflow, so the doubling ends
            size_t capacity = _slots.size();
            while (entries * kMaxLoadDivisor > capacity) {
                capacity *= 2;
            }
            if (capacity != _slots.size()) {
                Rehash(capacity);
            }
        }

        [[nodiscard]] size_t Used() const noexcept {
            return _used;
        }
        [[nodiscard]] size_t Capacity() const noexcept {
            return _slots.size();
        }
        /// The largest distance of a slot of the table from its home slot, as of now (growth recomputes it)
        [[nodiscard]] size_t LongestProbe() const noexcept {
            return _longestProbe;
        }

        [[nodiscard]] const T* Find(uint64_t hash, StringView str) const noexcept {
            const size_t mask = Mask();
            for (size_t idx = static_cast<size_t>(hash) & mask;; idx = (idx + 1) & mask) {
                const Slot& slot = _slots[idx];
                if (slot.entry == nullptr) {
                    return nullptr;
                }
                if (slot.hash == hash && slot.entry->size == str.size() && std::memcmp(slot.entry->data, str.data(), str.size()) == 0) {
                    return slot.entry;
                }
            }
        }
    };

}  // namespace BECore
