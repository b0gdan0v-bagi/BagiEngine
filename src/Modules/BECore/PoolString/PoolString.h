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

    class PoolString;

    /// A ready entry for `PoolString::RegisterStatic`, and where to put the `PoolString` of its text
    struct PoolStaticRegistration {
        const Details::PoolStringEntry* entry = nullptr;
        PoolString* value = nullptr;
    };

    class PoolString {
    public:
        /// Size of a standard block of the pool; a string whose entry does not fit one gets a block of its own.
        static constexpr size_t kBlockBytes = 128 * 1024;

        constexpr PoolString() noexcept : _entry(&Details::g_EmptyEntryStore.header) {}

        static PoolString Intern(StringView str);

        /// Returns the pooled string equal to `str`, or nullopt when no such string was interned. Never adds to the pool.
        [[nodiscard]] static Optional<PoolString> Find(StringView str);

        /// Interns every text of `texts` and writes the result of `texts[i]` to `out[i]`; the same as `Intern` one by one
        /// (an empty text gives the empty `PoolString`, a text twice in one batch gives one string), with fewer locks: one
        /// pass under the shared lock for the texts already in the pool, then, when some are missing, one unique lock, one
        /// growth of the table and one allocation of memory for all the new strings. A batch of texts that are all in the pool
        /// takes no unique lock. `out.size()` must equal `texts.size()` (asserted); the views are read during the call only.
        /// Hashes the texts itself, so it is the form for data that is not trusted.
        static void InternBatch(Span<const StringView> texts, Span<PoolString> out);

        /// The same with the hashes of the texts given, which saves hashing them again when a cook or a file stored them.
        /// `hashes[i]` **must** be `StringUtils::GetHash(texts[i])` (checked by `ASSERT` in Debug): a wrong hash puts the text
        /// where a lookup of it never looks, and a text whose hash belongs to another one can be taken for that one. Never
        /// pass hashes read from untrusted bytes: use the form above.
        static void InternBatch(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out);

        /// Makes ready entries, built before the program ran or by a cook, the entries of their texts, so that nothing is copied
        /// and nothing hashed for them. `entry` is `const`, **lives for the whole process** (the pool keeps its address and never
        /// writes to it), and has its `hash` right and a NUL after its `size` characters (`ASSERT` in Debug). Per registration
        /// `*value` is set to the `PoolString` of the text: the entry itself when its text was not in the pool, otherwise the
        /// entry the pool already had: the first one wins, whether it came from a `Intern`, from another ready entry or from an
        /// adopted block, and the loser stays unused where it is (`PoolStringStats::staticDuplicates`). So a caller must always use
        /// `*value`, never its own entry's address. Registering the same entry again changes nothing. An entry of an empty text
        /// gives the empty `PoolString`. A batch takes the unique lock once, and not at all when every text is in the pool.
        static void RegisterStatic(Span<const PoolStaticRegistration> registrations);

        /// One entry: the `PoolString` of its text, with the rules of the batch form
        [[nodiscard]] static PoolString RegisterStatic(const Details::PoolStringEntry& entry);

        /// A hint that about `strings` more strings of `bytes` aligned entry bytes in all will be interned: grows the table once
        /// for them and, when the current block has less than `bytes` free, takes one block of that size that becomes current
        /// (what is left of the old one is lost). It never fails and limits nothing: strings beyond the hint take blocks and
        /// growth as usual. Meant for a loader that knows the size of what it is about to read.
        static void Reserve(size_t strings, size_t bytes);

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
