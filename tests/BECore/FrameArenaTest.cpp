#include <BECore/Memory/ArenaPoison.h>
#include <BECore/Memory/FrameAllocator.h>
#include <BECore/Memory/FrameArena.h>
#include <BECore/Threading/Thread.h>
#include <Common/Wait.h>
#include <atomic>
#include <cstring>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBlock = 128;

        /// The counters summed over every arena named Frame (one per thread that used it)
        ArenaStatsEntry FrameTotals() {
            ArenaStatsEntry total;
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (std::strcmp(entry.name, "Frame") == 0) {
                    total.used += entry.used;
                    total.overflows += entry.overflows;
                    total.chunks += entry.chunks;
                    total.resets += entry.resets;
                }
            }
            return total;
        }

        /// Whether ASan holds the byte at @p ptr as not usable (false in a build without it)
        bool IsPoisoned([[maybe_unused]] const void* ptr) {
#if BE_ARENA_ASAN
            return __asan_address_is_poisoned(ptr) != 0;
#else
            return false;
#endif
        }

        /// Waits for @p flag to reach @p value
        bool WaitFor(const std::atomic<int>& flag, int value) {
            return WaitUntil([&] { return flag.load() == value; });
        }

    }  // namespace

    TEST(FrameArenaTest, EachThreadHasItsOwnArena) {
        if (!Thread::kAvailable) {
            GTEST_SKIP() << "a single-thread build has no second thread";
        }
        // The thread stays alive while the addresses are compared: a finished thread's storage can be reused by the next one
        std::atomic<int> phase{0};
        LinearArena* other = nullptr;
        Thread thread("Test frame arena", [&] {
            other = &FrameArena::Get();
            phase = 1;
            WaitFor(phase, 2);
        });
        const bool started = WaitFor(phase, 1);
        EXPECT_NE(other, nullptr);
        EXPECT_NE(other, &FrameArena::Get());
        phase = 2;
        thread.join();
        EXPECT_TRUE(started);
        EXPECT_EQ(&FrameArena::Get(), &FrameArena::Get());
    }

    TEST(FrameArenaTest, AdvanceResetsTheCallingThreadsArenaAndKeepsItsChunk) {
        LinearArena& arena = FrameArena::Get();
        EXPECT_NE(arena.Allocate(kBlock), nullptr);
        const size_t chunks = arena.GetChunkCount();
        EXPECT_GE(arena.GetBytesUsed(), kBlock);
        const ArenaStatsEntry before = FrameTotals();
        const uint64_t frame = FrameArena::GetFrameNumber();

        FrameArena::Advance();

        EXPECT_EQ(FrameArena::GetFrameNumber(), frame + 1);
        EXPECT_EQ(arena.GetBytesUsed(), 0u);
        EXPECT_EQ(arena.GetChunkCount(), chunks);
        EXPECT_NE(arena.Allocate(kBlock), nullptr);
        const ArenaStatsEntry after = FrameTotals();
        EXPECT_EQ(after.overflows, before.overflows);
        EXPECT_GT(after.resets, before.resets);
    }

    TEST(FrameArenaTest, AnotherThreadResetsAtItsFirstGetOfTheNewFrameAndNotBefore) {
        if (!Thread::kAvailable) {
            GTEST_SKIP() << "a single-thread build has no second thread";
        }
        std::atomic<int> phase{0};
        size_t usedAfterAdvance = 0;
        size_t usedAfterGet = 1;
        Thread thread("Test frame arena", [&] {
            LinearArena& arena = FrameArena::Get();
            EXPECT_NE(arena.Allocate(kBlock), nullptr);
            phase = 1;
            ASSERT_TRUE(WaitFor(phase, 2));
            usedAfterAdvance = arena.GetBytesUsed();
            usedAfterGet = FrameArena::Get().GetBytesUsed();
            phase = 3;
        });
        const bool allocated = WaitFor(phase, 1);
        FrameArena::Advance();
        phase = 2;
        const bool done = WaitFor(phase, 3);
        thread.join();

        ASSERT_TRUE(allocated);
        ASSERT_TRUE(done);
        EXPECT_GE(usedAfterAdvance, kBlock);
        EXPECT_EQ(usedAfterGet, 0u);
    }

    TEST(FrameArenaTest, AnOverflowIsCountedAndTheAllocationSucceeds) {
        if (!Thread::kAvailable) {
            GTEST_SKIP() << "a single-thread build has no second thread";
        }
        // A thread of its own: its arena has no chunk yet, so the first allocation is no overflow
        Thread thread("Test frame arena", [] {
            LinearArena& arena = FrameArena::Get();
            EXPECT_NE(arena.Allocate(kBlock), nullptr);
            const uint64_t before = FrameTotals().overflows;

            auto* big = static_cast<std::byte*>(arena.Allocate(FrameArena::kChunkSize));
            ASSERT_NE(big, nullptr);
            std::memset(big, 0x11, FrameArena::kChunkSize);

            EXPECT_EQ(FrameTotals().overflows, before + 1);
            EXPECT_EQ(arena.GetChunkCount(), 2u);
        });
        thread.join();
    }

    TEST(FrameArenaTest, TheArenaIsCountedAsFrame) {
        if (!Thread::kAvailable) {
            GTEST_SKIP() << "a single-thread build has no second thread";
        }
        Thread thread("Test frame arena", [] {
            EXPECT_NE(FrameArena::Get().Allocate(kBlock), nullptr);
            bool found = false;
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                found = found || (std::strcmp(entry.name, "Frame") == 0 && entry.chunks == 1 && entry.used >= kBlock);
            }
            EXPECT_TRUE(found);
        });
        thread.join();
    }

    TEST(FrameArenaTest, AFrameVectorGrowsInsideTheFrameArena) {
        const size_t before = FrameArena::Get().GetBytesUsed();
        FrameVector<int> values;
        values.reserve(64);
        for (int i = 0; i < 64; ++i) {
            values.push_back(i);
        }
        EXPECT_EQ(values[63], 63);
        EXPECT_GE(FrameArena::Get().GetBytesUsed(), before + 64 * sizeof(int));
        FrameArena::Advance();
    }

    TEST(FrameArenaTest, MemoryOfTheEndedFrameIsMarked) {
        if (!ArenaPoison::kFillsFreed && !ArenaPoison::kMarksFreed) {
            GTEST_SKIP() << "Release without a sanitizer marks nothing";
        }
        auto* data = static_cast<std::byte*>(FrameArena::Get().Allocate(kBlock));
        std::memset(data, 0x01, kBlock);
        FrameArena::Advance();
#if BE_ARENA_ASAN
        EXPECT_TRUE(IsPoisoned(data));
        EXPECT_TRUE(IsPoisoned(data + kBlock - 1));
#else
        EXPECT_EQ(data[0], std::byte{ArenaPoison::kFreedPattern});
        EXPECT_EQ(data[kBlock - 1], std::byte{ArenaPoison::kFreedPattern});
#endif
    }

#if ENGINE_ENABLE_ASSERTS
    TEST(FrameArenaTest, AFrameVectorGrownAfterItsFrameEndedAsserts) {
        FrameVector<int> kept;
        kept.reserve(2);
        FrameArena::Advance();
        EXPECT_NONFATAL_FAILURE(kept.reserve(64), "frame container");
    }

    TEST(FrameArenaTest, AFrameVectorGrownOnAnotherThreadAsserts) {
        if (!Thread::kAvailable) {
            GTEST_SKIP() << "a single-thread build has no second thread";
        }
        FrameVector<int> kept;
        kept.reserve(2);
        EXPECT_NONFATAL_FAILURE_ON_ALL_THREADS(
            {
                Thread thread("Test frame arena", [&] { kept.reserve(64); });
                thread.join();
            },
            "frame container");
        FrameArena::Advance();
    }
#endif

}  // namespace BECore::Tests
