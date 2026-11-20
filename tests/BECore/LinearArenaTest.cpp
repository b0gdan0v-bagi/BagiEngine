#include <BECore/Memory/ArenaPoison.h>
#include <BECore/Memory/LinearArena.h>
#include <cstring>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSmallChunk = 256;

        ArenaStatsEntry StatsOf(const char* name) {
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (std::strcmp(entry.name, name) == 0) {
                    return entry;
                }
            }
            ADD_FAILURE() << "no arena named " << name;
            return {};
        }

        bool IsAligned(const void* ptr, size_t alignment) {
            return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
        }

        /// Whether ASan holds the byte at @p ptr as not usable (false in a build without it)
        bool IsPoisoned([[maybe_unused]] const void* ptr) {
#if BE_ARENA_ASAN
            return __asan_address_is_poisoned(ptr) != 0;
#else
            return false;
#endif
        }

        bool Inside(const void* ptr, const std::byte* buffer, size_t size) {
            const auto* bytes = static_cast<const std::byte*>(ptr);
            return bytes >= buffer && bytes < buffer + size;
        }

    }  // namespace

    TEST(LinearArenaTest, EveryAlignmentFromOneToSixtyFourIsHonoured) {
        LinearArena arena("LinearAlign", kSmallChunk);
        for (size_t alignment = 1; alignment <= 64; alignment *= 2) {
            void* odd = arena.Allocate(3, 1);
            void* ptr = arena.Allocate(5, alignment);
            ASSERT_NE(odd, nullptr);
            ASSERT_NE(ptr, nullptr);
            EXPECT_TRUE(IsAligned(ptr, alignment)) << alignment;
            std::memset(ptr, 0x5A, 5);
        }
    }

    TEST(LinearArenaTest, ABlockMoreAlignedAndLargerThanTheChunkFitsInItsOwnChunk) {
        LinearArena arena("LinearHuge", kSmallChunk);
        constexpr size_t kAlignment = 4096;
        constexpr size_t kSize = 3 * kSmallChunk;
        auto* ptr = static_cast<std::byte*>(arena.Allocate(kSize, kAlignment));
        ASSERT_NE(ptr, nullptr);
        EXPECT_TRUE(IsAligned(ptr, kAlignment));
        std::memset(ptr, 0x11, kSize);
        EXPECT_EQ(StatsOf("LinearHuge").overflows, 0u);  // the first chunk is made for it, not an overflow
    }

    TEST(LinearArenaTest, ZeroSizeGivesDistinctNonNullAddresses) {
        LinearArena arena("LinearZero", kSmallChunk);
        void* first = arena.Allocate(0);
        void* second = arena.Allocate(0);
        EXPECT_NE(first, nullptr);
        EXPECT_NE(second, nullptr);
        EXPECT_NE(first, second);
    }

    TEST(LinearArenaTest, ManySmallAllocationsStayInOneChunkAndDoNotOverlap) {
        LinearArena arena("LinearSmall", 4096);
        std::byte* previous = nullptr;
        for (int i = 0; i < 100; ++i) {
            auto* ptr = static_cast<std::byte*>(arena.Allocate(16, 16));
            if (previous != nullptr) {
                EXPECT_EQ(ptr, previous + 16);
            }
            previous = ptr;
        }
        EXPECT_EQ(arena.GetChunkCount(), 1u);
        EXPECT_EQ(arena.GetBytesUsed(), 1600u);
        EXPECT_EQ(StatsOf("LinearSmall").overflows, 0u);
    }

    TEST(LinearArenaTest, ResetKeepsEveryChunkAndReusesTheAddresses) {
        LinearArena arena("LinearReset", kSmallChunk);
        void* a = arena.Allocate(200, 1);
        void* b = arena.Allocate(200, 1);
        void* c = arena.Allocate(1000, 1);
        ASSERT_NE(a, b);
        const size_t capacity = arena.GetCapacity();
        ASSERT_EQ(arena.GetChunkCount(), 3u);

        arena.Reset();
        EXPECT_EQ(arena.GetBytesUsed(), 0u);
        EXPECT_EQ(arena.GetCapacity(), capacity);
        EXPECT_EQ(arena.GetChunkCount(), 3u);
        EXPECT_EQ(arena.Allocate(200, 1), a);
        EXPECT_EQ(arena.Allocate(200, 1), b);
        EXPECT_EQ(arena.Allocate(1000, 1), c);
        const ArenaStatsEntry stats = StatsOf("LinearReset");
        EXPECT_EQ(stats.overflows, 2u);  // the second round reused the chunks
        EXPECT_EQ(stats.resets, 1u);
    }

    TEST(LinearArenaTest, ARequestThatDoesNotFitTakesANewChunkAndCountsOneOverflow) {
        LinearArena arena("LinearOverflow", kSmallChunk);
        ASSERT_NE(arena.Allocate(200, 1), nullptr);
        EXPECT_EQ(StatsOf("LinearOverflow").overflows, 0u);
        EXPECT_NE(arena.Allocate(200, 1), nullptr);
        EXPECT_EQ(StatsOf("LinearOverflow").overflows, 1u);
        EXPECT_EQ(arena.GetChunkCount(), 2u);

        void* big = arena.Allocate(10000, 1);
        ASSERT_NE(big, nullptr);
        std::memset(big, 0x22, 10000);
        const ArenaStatsEntry stats = StatsOf("LinearOverflow");
        EXPECT_EQ(stats.overflows, 2u);
        EXPECT_EQ(stats.chunks, 3u);
        EXPECT_EQ(stats.capacity, arena.GetCapacity());
        EXPECT_GE(stats.capacity, 10000u + 2 * kSmallChunk);
    }

    TEST(LinearArenaTest, ACallersBufferIsUsedFirstAndHeapIsTheFallback) {
        alignas(64) std::byte buffer[512];
        LinearArena arena("LinearBuffer", buffer, sizeof(buffer), kSmallChunk);
        void* inside = arena.Allocate(300, 1);
        EXPECT_TRUE(Inside(inside, buffer, sizeof(buffer)));
        EXPECT_EQ(StatsOf("LinearBuffer").chunks, 1u);
        EXPECT_EQ(StatsOf("LinearBuffer").capacity, sizeof(buffer));
        EXPECT_EQ(StatsOf("LinearBuffer").overflows, 0u);

        void* outside = arena.Allocate(300, 1);
        ASSERT_NE(outside, nullptr);
        EXPECT_FALSE(Inside(outside, buffer, sizeof(buffer)));
        std::memset(outside, 0x33, 300);
        EXPECT_EQ(StatsOf("LinearBuffer").overflows, 1u);

        arena.Reset();
        EXPECT_EQ(arena.Allocate(300, 1), inside);
    }

    TEST(LinearArenaTest, NestedMarksRewindInReverseAndTheAddressIsReused) {
        LinearArena arena("LinearMarks", kSmallChunk);
        void* base = arena.Allocate(40, 8);
        const LinearArena::Mark outer = arena.GetMark();
        void* inner1 = arena.Allocate(40, 8);
        const LinearArena::Mark inner = arena.GetMark();
        void* inner2 = arena.Allocate(300, 8);  // crosses into the next chunk
        const size_t usedWithInner = arena.GetBytesUsed();
        ASSERT_NE(inner2, nullptr);

        arena.Rewind(inner);
        EXPECT_EQ(arena.GetBytesUsed(), usedWithInner - 300);
        EXPECT_EQ(arena.Allocate(300, 8), inner2);
        arena.Rewind(inner);
        arena.Rewind(outer);
        EXPECT_EQ(arena.GetBytesUsed(), 40u);
        EXPECT_EQ(arena.Allocate(40, 8), inner1);
        EXPECT_NE(base, inner1);
        EXPECT_EQ(StatsOf("LinearMarks").used, arena.GetBytesUsed());
    }

    TEST(LinearArenaTest, AMarkOfAnEmptyArenaRewindsToTheStart) {
        LinearArena arena("LinearEmptyMark", kSmallChunk);
        const LinearArena::Mark start = arena.GetMark();
        void* first = arena.Allocate(10, 1);
        arena.Rewind(start);
        EXPECT_EQ(arena.GetBytesUsed(), 0u);
        EXPECT_EQ(arena.Allocate(10, 1), first);
    }

    TEST(LinearArenaTest, ReleaseFreesEveryChunkButTheFirst) {
        LinearArena arena("LinearRelease", kSmallChunk);
        void* first = arena.Allocate(200, 1);
        ASSERT_NE(arena.Allocate(200, 1), nullptr);
        ASSERT_NE(arena.Allocate(5000, 1), nullptr);
        ASSERT_EQ(arena.GetChunkCount(), 3u);

        arena.Release();
        EXPECT_EQ(arena.GetChunkCount(), 1u);
        EXPECT_EQ(arena.GetBytesUsed(), 0u);
        EXPECT_EQ(StatsOf("LinearRelease").chunks, 1u);
        EXPECT_EQ(StatsOf("LinearRelease").capacity, arena.GetCapacity());
        EXPECT_EQ(arena.Allocate(200, 1), first);
    }

    TEST(LinearArenaTest, TheStatsFollowTheOperationsAndTheEntryEndsWithTheArena) {
        const uint32_t before = ArenaStats::GetLiveCount();
        {
            LinearArena arena("LinearStats", 1024);
            EXPECT_EQ(ArenaStats::GetLiveCount(), before);  // registered by the first Allocate
            ASSERT_NE(arena.Allocate(100, 1), nullptr);
            EXPECT_EQ(ArenaStats::GetLiveCount(), before + 1);
            ASSERT_NE(arena.Allocate(50, 1), nullptr);
            ArenaStatsEntry stats = StatsOf("LinearStats");
            EXPECT_EQ(stats.used, 150u);
            EXPECT_EQ(stats.peak, 150u);
            EXPECT_EQ(stats.capacity, 1024u);
            EXPECT_EQ(stats.chunks, 1u);

            arena.Reset();
            stats = StatsOf("LinearStats");
            EXPECT_EQ(stats.used, 0u);
            EXPECT_EQ(stats.peak, 150u);
            EXPECT_EQ(stats.resets, 1u);
        }
        EXPECT_EQ(ArenaStats::GetLiveCount(), before);
    }

    TEST(LinearArenaTest, AnArenaRunsUncountedWhenTheTableIsFull) {
        Vector<ArenaStatsSlot> fillers;
        for (uint32_t i = 0; i < ArenaStats::kMaxArenas; ++i) {
            ArenaStatsSlot slot = ArenaStatsSlot::Claim("LinearFiller");
            if (!slot.IsValid()) {
                break;
            }
            fillers.push_back(slot);
        }
        {
            LinearArena arena("LinearUncounted", kSmallChunk);
            void* ptr = arena.Allocate(100, 1);
            EXPECT_NE(ptr, nullptr);
            EXPECT_NE(arena.Allocate(400, 1), nullptr);
            arena.Reset();
            EXPECT_EQ(arena.Allocate(100, 1), ptr);
        }
        for (ArenaStatsSlot& slot : fillers) {
            slot.Release();
        }
    }

    TEST(LinearArenaTest, AllocateOfATypeGivesStorageAlignedForIt) {
        struct alignas(32) Wide {
            float values[8];
        };
        LinearArena arena("LinearTyped", kSmallChunk);
        Wide* wide = arena.Allocate<Wide>(3);
        ASSERT_NE(wide, nullptr);
        EXPECT_TRUE(IsAligned(wide, alignof(Wide)));
        for (int i = 0; i < 3; ++i) {
            std::memset(&wide[i], 0x44, sizeof(Wide));
        }
        EXPECT_GE(arena.GetBytesUsed(), 3 * sizeof(Wide));
    }

#if ENGINE_ENABLE_ASSERTS
    TEST(LinearArenaTest, AMarkTakenBeforeResetAssertsAndChangesNothing) {
        LinearArena arena("LinearStaleMark", kSmallChunk);
        ASSERT_NE(arena.Allocate(10, 1), nullptr);
        const LinearArena::Mark stale = arena.GetMark();
        arena.Reset();
        void* after = arena.Allocate(20, 1);
        EXPECT_NONFATAL_FAILURE(arena.Rewind(stale), "invalid");
        EXPECT_EQ(arena.GetBytesUsed(), 20u);
        EXPECT_NE(after, nullptr);
    }
#endif

    TEST(LinearArenaTest, FreedMemoryIsMarkedAfterResetAndRewind) {
        if (!ArenaPoison::kFillsFreed && !ArenaPoison::kMarksFreed) {
            GTEST_SKIP() << "Release without a sanitizer marks nothing";
        }
        LinearArena arena("LinearPoison", kSmallChunk);
        auto* kept = static_cast<std::byte*>(arena.Allocate(16, 16));
        const LinearArena::Mark mark = arena.GetMark();
        auto* rewound = static_cast<std::byte*>(arena.Allocate(32, 1));
        std::memset(rewound, 0x01, 32);

        arena.Rewind(mark);
        auto* reset = static_cast<std::byte*>(arena.Allocate(32, 1));
        EXPECT_EQ(reset, rewound);
        std::memset(kept, 0x02, 16);
        std::memset(reset, 0x03, 32);
        arena.Reset();

#if BE_ARENA_ASAN
        EXPECT_TRUE(IsPoisoned(kept));
        EXPECT_TRUE(IsPoisoned(kept + 15));
        EXPECT_TRUE(IsPoisoned(reset));
        EXPECT_TRUE(IsPoisoned(reset + 31));
#else
        for (size_t i = 0; i < 16; ++i) {
            EXPECT_EQ(kept[i], std::byte{ArenaPoison::kFreedPattern}) << i;
        }
        for (size_t i = 0; i < 32; ++i) {
            EXPECT_EQ(reset[i], std::byte{ArenaPoison::kFreedPattern}) << i;
        }
#endif
    }

#if BE_ARENA_ASAN
    TEST(LinearArenaTest, ANewAllocationUnpoisonsExactlyItsBytes) {
        LinearArena arena("LinearUnpoison", kSmallChunk);
        auto* first = static_cast<std::byte*>(arena.Allocate(10, 1));
        arena.Reset();
        auto* again = static_cast<std::byte*>(arena.Allocate(10, 1));
        ASSERT_EQ(again, first);
        EXPECT_FALSE(IsPoisoned(again));
        EXPECT_FALSE(IsPoisoned(again + 9));
        EXPECT_TRUE(IsPoisoned(again + 10));

        auto* aligned = static_cast<std::byte*>(arena.Allocate(8, 64));
        ASSERT_GT(aligned, again + 10);
        EXPECT_FALSE(IsPoisoned(aligned + 7));
        EXPECT_TRUE(IsPoisoned(aligned + 8));
        EXPECT_TRUE(IsPoisoned(aligned - 1));  // the gap before it stays poisoned
        EXPECT_TRUE(IsPoisoned(again + 10));
    }
#endif

}  // namespace BECore::Tests
