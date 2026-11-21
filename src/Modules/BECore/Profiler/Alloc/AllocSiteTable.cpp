#include "AllocSiteTable.h"

namespace BECore {

    namespace {

        constexpr uint32_t kNullIndex = 0;                                  // a null pointer is index 0; interned pointers are slot + 1
        constexpr uint32_t kFullIndex = UINT32_MAX;                         // the pointer table has no room
        constexpr uint32_t kIndexBits = 16;                                 // zone and name indexes each fit 16 bits (the tables hold 1024)
        constexpr uint64_t kPointerHashMultiplier = 0x9E3779B97F4A7C15ull;  // 2^64 / golden ratio: sites and literals sit at any stride
        constexpr uint32_t kPointerHashShift = 40;                          // the top 24 bits of the product pick the first slot
        constexpr uint32_t kPairHashMultiplier = 2654435761u;               // Knuth's multiplicative hash
        constexpr uint32_t kPairHashShift = 20;                             // keeps the top bits of the product: the zone sits in the high bits of the key
        static_assert((1u << (32 - kPairHashShift)) == AllocSiteTable::kPairSlots, "the shift must index exactly kPairSlots");

        /// The index of @p pointer in @p table: claimed on first use (open addressing, never removed); kFullIndex when the table is full
        template <size_t N>
        uint32_t Intern(std::atomic<const void*> (&table)[N], const void* pointer) {
            if (pointer == nullptr) {
                return kNullIndex;
            }
            const uint64_t hash = (reinterpret_cast<uintptr_t>(pointer) * kPointerHashMultiplier) >> kPointerHashShift;
            for (size_t probe = 0; probe < N; ++probe) {
                const size_t index = (hash + probe) % N;
                const void* key = table[index].load(std::memory_order_acquire);
                if (key == pointer) {
                    return static_cast<uint32_t>(index) + 1;
                }
                if (key == nullptr) {
                    if (table[index].compare_exchange_strong(key, pointer, std::memory_order_acq_rel) || key == pointer) {
                        return static_cast<uint32_t>(index) + 1;
                    }
                }
            }
            return kFullIndex;
        }

        template <size_t N>
        const void* PointerOf(const std::atomic<const void*> (&table)[N], uint32_t index) {
            return index == kNullIndex ? nullptr : table[index - 1].load(std::memory_order_acquire);
        }

        const char* TextOf(const char* name) {
            return name != nullptr ? name : "";
        }

    }  // namespace

    void AllocSiteTable::Add(const ProfileSite* zone, const char* name, uint64_t bytes) {
        const uint32_t zoneIndex = Intern(_zones, zone);
        const uint32_t nameIndex = Intern(_names, name);
        if (zoneIndex == kFullIndex || nameIndex == kFullIndex) {
            _overflow.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const uint32_t key = ((zoneIndex << kIndexBits) | nameIndex) + 1;  // + 1: key 0 marks a free slot
        const uint32_t hash = (key * kPairHashMultiplier) >> kPairHashShift;
        for (uint32_t probe = 0; probe < kPairSlots; ++probe) {
            PairSlot& slot = _pairs[(hash + probe) % kPairSlots];
            uint32_t current = slot.key.load(std::memory_order_acquire);
            if (current == 0 && slot.key.compare_exchange_strong(current, key, std::memory_order_acq_rel)) {
                current = key;
            }
            // a lost claim leaves the winner's key in current: it may be this very pair
            if (current == key) {
                slot.count.fetch_add(1, std::memory_order_relaxed);
                slot.bytes.fetch_add(bytes, std::memory_order_relaxed);
                return;
            }
        }
        _overflow.fetch_add(1, std::memory_order_relaxed);
    }

    Vector<AllocSiteStats> AllocSiteTable::GetStats() const {
        Vector<AllocSiteStats> rows;
        for (const PairSlot& slot : _pairs) {
            const uint32_t key = slot.key.load(std::memory_order_acquire);
            const uint64_t count = slot.count.load(std::memory_order_relaxed);
            if (key == 0 || count == 0) {
                continue;
            }
            const uint32_t packed = key - 1;
            const uint32_t zoneIndex = packed >> kIndexBits;
            const uint32_t nameIndex = packed & ((1u << kIndexBits) - 1);
            rows.push_back({static_cast<const ProfileSite*>(PointerOf(_zones, zoneIndex)), static_cast<const char*>(PointerOf(_names, nameIndex)), count, slot.bytes.load(std::memory_order_relaxed)});
        }

        // Equal text under two pointers (a literal in two translation units) is one row
        std::sort(rows.begin(), rows.end(), [](const AllocSiteStats& a, const AllocSiteStats& b) {
            return a.zone != b.zone ? std::less<const ProfileSite*>{}(a.zone, b.zone) : std::strcmp(TextOf(a.name), TextOf(b.name)) < 0;
        });
        Vector<AllocSiteStats> merged;
        for (const AllocSiteStats& row : rows) {
            if (!merged.empty() && merged.back().zone == row.zone && std::strcmp(TextOf(merged.back().name), TextOf(row.name)) == 0) {
                merged.back().count += row.count;
                merged.back().bytes += row.bytes;
            } else {
                merged.push_back(row);
            }
        }
        std::sort(merged.begin(), merged.end(), [](const AllocSiteStats& a, const AllocSiteStats& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.count > b.count; });
        return merged;
    }

    uint64_t AllocSiteTable::GetOverflow() const {
        return _overflow.load(std::memory_order_relaxed);
    }

    void AllocSiteTable::Reset() {
        for (PairSlot& slot : _pairs) {
            slot.count.store(0, std::memory_order_relaxed);
            slot.bytes.store(0, std::memory_order_relaxed);
        }
        _overflow.store(0, std::memory_order_relaxed);
    }

}  // namespace BECore
