#pragma once

#include <BECore/PoolString/PoolStringEntry.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    namespace Details {
        class PoolStringStorage;
    }

    /// What one source of strings put in the pool (see `PoolStringStats`)
    struct PoolSourceStats {
        size_t strings = 0;     ///< Distinct strings that came from this source and are in the table
        size_t bytes = 0;       ///< Their aligned entry sizes
        size_t blocks = 0;      ///< Blocks of pool memory held for this source; 0 for static entries (program image)
        size_t blockBytes = 0;  ///< Their total size
    };

    /// A snapshot of the pool for tests, the profiler and tuning. The table fields are read under the shared lock of the
    /// pool, the call counters under none: they need not agree with the table fields or with each other to the last call.
    /// The totals (`stringCount`, `stringBytes`, `blockCount`, `blockBytes`) add up all the sources.
    struct PoolStringStats {
        size_t stringCount = 0;    ///< Distinct strings in the pool, the empty one not counted
        size_t stringBytes = 0;    ///< Sum of the aligned entry sizes (header, text, NUL)
        size_t blockCount = 0;     ///< Blocks held: the standard ones and the ones of their own
        size_t blockBytes = 0;     ///< Their total size
        size_t tableCapacity = 0;  ///< Slots of the table
        size_t tableUsed = 0;      ///< Slots in use
        size_t longestProbe = 0;   ///< Largest distance of an entry from its home slot, as of now
        uint64_t internCalls = 0;  ///< `hits + misses`
        uint64_t hits = 0;         ///< `Intern` calls that found the text (the empty text always does)
        uint64_t misses = 0;       ///< `Intern` calls that added a string: a text raced for by two threads is one miss

        PoolSourceStats runtime;        ///< Copied into the pool by `Intern`
        PoolSourceStats staticEntries;  ///< Ready entries registered by `RegisterStatic`
        PoolSourceStats adopted;        ///< Entries of adopted blocks

        size_t deadStrings = 0;         ///< Adopted entries whose text was already in the pool: kept in their block, not linked
        size_t deadBytes = 0;           ///< Their aligned sizes
        size_t staticDuplicates = 0;    ///< Registered static entries that lost to an entry of the same text
        uint64_t writeLocks = 0;        ///< Calls that took the unique lock of the pool
        uint64_t batchCalls = 0;        ///< `InternBatch` calls
        uint64_t batchStrings = 0;      ///< Texts they were given
        size_t heapFallbackBlocks = 0;  ///< Blocks the heap gave because the block source returned none
    };

    class PoolString {
    public:
        /// Size of a standard block of the pool; a string whose entry does not fit one gets a block of its own.
        static constexpr size_t kBlockBytes = 128 * 1024;

        constexpr PoolString() noexcept : _entry(&Details::g_EmptyEntryStore.header) {}

        static PoolString Intern(StringView str);

        /// Returns the pooled string equal to `str`, or nullopt when no such string was interned. Never adds to the pool.
        [[nodiscard]] static Optional<PoolString> Find(StringView str);

        /// Counters of the pool; `Find` is not an `Intern` call. A test compares two snapshots: the pool is shared.
        [[nodiscard]] static PoolStringStats GetStats();

        [[nodiscard]] constexpr uint64_t HashValue() const noexcept {
            return _entry->hash;
        }
        [[nodiscard]] constexpr StringView ToStringView() const noexcept {
            return _entry->ToStringView();
        }
        [[nodiscard]] constexpr const char* CStr() const noexcept {
            return _entry->data;
        }

        [[nodiscard]] constexpr bool Empty() const noexcept {
            return _entry->size == 0;
        }

        [[nodiscard]] constexpr size_t Length() const noexcept {
            return _entry->size;
        }

        [[nodiscard]] constexpr bool operator==(const PoolString& other) const noexcept {
            if consteval {
                return _entry->hash == other._entry->hash;
            }
            return _entry == other._entry;
        }
        [[nodiscard]] constexpr bool operator!=(const PoolString& other) const noexcept {
            return !(*this == other);
        }

    private:
        friend class Details::PoolStringStorage;

        using Entry = Details::PoolStringEntry;
        constexpr explicit PoolString(const Entry* entry) noexcept : _entry(entry) {}

        const Entry* _entry;
    };

    // The length and the hash live in the entry, so a PoolString stays one pointer and equality is a pointer compare.
    static_assert(sizeof(PoolString) == sizeof(void*));

}  // namespace BECore
