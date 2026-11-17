#pragma once

#include <BECore/PoolString/PoolStringEntry.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>
#include <cstddef>
#include <cstdint>
#include <expected>

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

        PoolSourceStats runtime;        ///< Copied into the pool by `Intern` and `InternBatch`
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

    class PoolBlock;
    struct PoolAdoptResult;

    /// Here and not in `PoolBlock.h`: `AdoptBlock` takes it with a default argument, which needs the complete type
    struct PoolAdoptOptions {
        /// Check `hash == StringUtils::GetHash(text)` of every entry. Turn it off only for bytes that were checked before
        /// (a cook's output the build verified): a wrong hash puts the text where a lookup of it never looks.
        bool verifyHashes = true;
    };

    /// Why `PoolString::AdoptBlock` refused a block; the enumerators and `EnumUtils` are in `PoolBlock.h` (a `CORE_ENUM`
    /// needs `EnumUtils.h`, which includes this header, so only the opaque declaration can be here). A caller of
    /// `AdoptBlock` includes `PoolBlock.h`.
    enum class PoolAdoptError : uint8_t;

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

        /// For names that come from data (a content key, a type, a field): the text is hashed and copied at run time.
        static PoolString Intern(StringView str);

        /// A literal is a constant: write `"text"_intern` (a ready entry, no hashing at run time) instead. A `char` buffer
        /// whose text is only known at run time passes as `StringView(buffer)`. Only an array argument lands here; a
        /// `const char*`, a `StringView` and a `String` still call the overload above.
        template <size_t N>
        static PoolString Intern(const char (&use_the_intern_literal_or_StringView)[N]) = delete;

        /// Returns the pooled string equal to `str`, or nullopt when no such string was interned. Never adds to the pool.
        [[nodiscard]] static Optional<PoolString> Find(StringView str);

        /// For a literal ask `Find(StringView{"text"})`: `"text"_intern` would intern it, which is the opposite of the
        /// question. A runtime `char` buffer passes as `StringView(buffer)`.
        template <size_t N>
        [[nodiscard]] static Optional<PoolString> Find(const char (&use_StringView_for_a_literal_or_a_buffer)[N]) = delete;

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
        /// growth as usual. Meant for a loader that knows the size of what it is about to read, so the numbers may come from
        /// a file header and a part that is not sane is **ignored**, not clamped: `strings` when the table would have to hold
        /// more than `PoolEntryTable::kMaxReservedEntries` (or the sum overflows), `bytes` above `kDefaultReservedBytes`
        /// (the reservation of the default block source).
        static void Reserve(size_t strings, size_t bytes);

        /// Memory for a loader to write ready entries into, `bytes` rounded up to a multiple of 8 (at least 8). Never null: the
        /// pool takes a heap block when its source has none, so a size that is out of reach aborts: a loader that takes `bytes` from a
        /// header must check it first (at most `Details::kPoolEntryMaxTextBytes`, `ASSERT`ed). The pool owns it from here on. Until it is adopted it counts in
        /// `blockCount` and `blockBytes` but not in `PoolStringStats::adopted`. The content is unspecified.
        [[nodiscard]] static PoolBlock AllocateBlock(size_t bytes);

        /// Makes the entries a loader wrote into `block` the entries of the pool, without copying them. The first `usedBytes`
        /// bytes hold entries one after another from `block.Data()`, each `Details::PoolEntryBytes(size)` long in the layout
        /// of `PoolStringEntry.h` (`hash`, `size`, the text, a NUL, padding), and nothing else: `usedBytes` is a multiple of 8
        /// and at most the capacity.
        ///
        /// The block is **checked first, linked after**: the walk reads no byte at or past `usedBytes`, checks that every
        /// entry ends inside it, that the walk ends exactly at it, every NUL and, with `options.verifyHashes`, every hash.
        /// A refusal links no entry, leaves the pool as it was and leaves `block` as it was, adoptable. An empty token and a
        /// token of another storage are refused too (`NotOwned`).
        ///
        /// On success **the memory is the pool's and read-only for everyone** from the call on: `block` is emptied (a second
        /// call with it is `NotOwned`), so write through no pointer taken from it earlier. `out` is how a loader gets its
        /// strings. A text that is already in the pool (from `Intern`, `RegisterStatic`, or earlier in this block) keeps its
        /// older entry and the one in the block is **dead**: it stays in its block, unlinked, counted in `deadStrings` /
        /// `deadBytes`. An entry of an empty text is dead as well. So a caller must take the strings from `out`, never point at
        /// its own entry: with a non-empty `out` (its size must be the number of entries, else `OutSizeMismatch`) `out[i]` is,
        /// for the i-th entry in order, the `PoolString` that won: the adopted entry, the older one, or the empty `PoolString`.
        /// Takes the unique lock once, after the checks. The block is the caller's until the call returns: it must not change
        /// it during the call. With `verifyHashes` off a hash that is not the text's is the caller's contract. Include
        /// `PoolBlock.h` to call it.
        static std::expected<PoolAdoptResult, PoolAdoptError> AdoptBlock(PoolBlock& block, size_t usedBytes, Span<PoolString> out = {}, PoolAdoptOptions options = {});

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
