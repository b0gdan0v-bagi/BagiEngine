#include <BECore/Memory/PoolStats.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/Wait.h>
#include <atomic>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// The entry called @p name; `found` is false when the registry has none
        struct Found {
            bool found = false;
            PoolStatsEntry entry;
        };

        Found Find(const char* name) {
            for (const PoolStatsEntry& entry : GetPoolStats()) {
                if (std::strcmp(entry.name, name) == 0) {
                    return {true, entry};
                }
            }
            return {};
        }

        /// Claims every free slot and returns the handles, so a test can see the table full
        Vector<PoolStatsSlot> ClaimAll() {
            Vector<PoolStatsSlot> slots;
            for (uint32_t i = 0; i < PoolStats::kMaxPools; ++i) {
                PoolStatsSlot slot = PoolStatsSlot::Claim("FillerPool", 8, false);
                if (!slot.IsValid()) {
                    break;
                }
                slots.push_back(slot);
            }
            return slots;
        }

    }  // namespace

    TEST(PoolStatsTest, ASnapshotHoldsWhatTheOwnerPublished) {
        PoolStatsSlot slot = PoolStatsSlot::Claim("PublishedPool", 48, false);
        ASSERT_TRUE(slot.IsValid());
        slot.OnPage(4096);
        slot.OnPage(4096);
        slot.OnAllocate();
        slot.OnAllocate();
        slot.OnAllocate();
        slot.OnFree();
        slot.OnFree();

        const Found found = Find("PublishedPool");
        slot.Release();
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.blockSize, 48u);
        EXPECT_EQ(found.entry.live, 1u);
        EXPECT_EQ(found.entry.peak, 3u);
        EXPECT_EQ(found.entry.pages, 2u);
        EXPECT_EQ(found.entry.pageBytes, 8192u);
        EXPECT_EQ(found.entry.allocations, 3u);
    }

    TEST(PoolStatsTest, AConcurrentSlotCountsLikeASingleThreadOne) {
        PoolStatsSlot slot = PoolStatsSlot::Claim("ConcurrentSlot", 16, true);
        ASSERT_TRUE(slot.IsValid());
        slot.OnPage(100);
        slot.OnAllocate();
        slot.OnAllocate();
        slot.OnFree();
        const Found found = Find("ConcurrentSlot");
        slot.Release();
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.live, 1u);
        EXPECT_EQ(found.entry.peak, 2u);
        EXPECT_EQ(found.entry.pages, 1u);
        EXPECT_EQ(found.entry.pageBytes, 100u);
        EXPECT_EQ(found.entry.allocations, 2u);
    }

    TEST(PoolStatsTest, SetCountsPublishesTheTotalsAndKeepsTheLargestLiveAsThePeak) {
        PoolStatsSlot slot = PoolStatsSlot::Claim("SetCountsPool", 32, true);
        ASSERT_TRUE(slot.IsValid());
        slot.SetCounts(7, 10);
        slot.SetCounts(3, 14);
        const Found found = Find("SetCountsPool");
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.live, 3u);
        EXPECT_EQ(found.entry.allocations, 14u);
        EXPECT_EQ(found.entry.peak, 7u);
        slot.Release();
        PoolStatsSlot none;
        none.SetCounts(1, 1);
    }

    TEST(PoolStatsTest, AReleasedSlotLeavesTheSnapshotAndIsReusedClean) {
        const uint32_t before = PoolStats::GetLiveCount();
        PoolStatsSlot first = PoolStatsSlot::Claim("FirstUse", 32, false);
        first.OnAllocate();
        first.OnPage(64);
        EXPECT_EQ(PoolStats::GetLiveCount(), before + 1);
        first.Release();
        EXPECT_FALSE(first.IsValid());
        EXPECT_FALSE(Find("FirstUse").found);
        EXPECT_EQ(PoolStats::GetLiveCount(), before);

        PoolStatsSlot second = PoolStatsSlot::Claim("SecondUse", 8, true);
        const Found found = Find("SecondUse");
        second.Release();
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.blockSize, 8u);
        EXPECT_EQ(found.entry.live, 0u);
        EXPECT_EQ(found.entry.peak, 0u);
        EXPECT_EQ(found.entry.pages, 0u);
        EXPECT_EQ(found.entry.allocations, 0u);
    }

    TEST(PoolStatsTest, AFullTableAnswersNoneAndTheNoneHandleIsInert) {
        Vector<PoolStatsSlot> slots = ClaimAll();
        const PoolStatsSlot none = PoolStatsSlot::Claim("OneTooMany", 8, false);
        EXPECT_FALSE(none.IsValid());

        PoolStatsSlot inert;
        inert.OnAllocate();
        inert.OnFree();
        inert.OnPage(1);
        inert.Release();
        EXPECT_FALSE(Find("OneTooMany").found);

        ASSERT_FALSE(slots.empty());
        slots.back().Release();
        PoolStatsSlot again = PoolStatsSlot::Claim("FitsAgain", 8, false);
        EXPECT_TRUE(again.IsValid());
        again.Release();
        slots.pop_back();
        for (PoolStatsSlot& slot : slots) {
            slot.Release();
        }
    }

    TEST(PoolStatsTest, TotalsSumThePoolsAlive) {
        const PoolTotals before = GetPoolTotals();
        PoolStatsSlot a = PoolStatsSlot::Claim("TotalsA", 10, false);
        PoolStatsSlot b = PoolStatsSlot::Claim("TotalsB", 100, true);
        a.OnPage(1000);
        a.OnAllocate();
        a.OnAllocate();
        b.OnPage(2000);
        b.OnPage(2000);
        b.OnAllocate();

        const PoolTotals totals = GetPoolTotals();
        a.Release();
        b.Release();
        EXPECT_EQ(totals.liveBlocks - before.liveBlocks, 3u);
        EXPECT_EQ(totals.liveBytes - before.liveBytes, 2u * 10 + 100);
        EXPECT_EQ(totals.pages - before.pages, 3u);
        EXPECT_EQ(totals.pageBytes - before.pageBytes, 5000u);

        const PoolTotals after = GetPoolTotals();
        EXPECT_EQ(after.liveBlocks, before.liveBlocks);
        EXPECT_EQ(after.pageBytes, before.pageBytes);
    }

    TEST(PoolStatsTest, ConcurrentWritersEndAtTheExactLiveCount) {
        constexpr uint32_t kThreads = 8;
        constexpr uint32_t kRounds = 20000;
        PoolStatsSlot slot = PoolStatsSlot::Claim("ManyWriters", 24, true);
        ASSERT_TRUE(slot.IsValid());

        Vector<std::thread> threads;
        for (uint32_t i = 0; i < kThreads; ++i) {
            threads.emplace_back([&slot] {
                for (uint32_t round = 0; round < kRounds; ++round) {
                    slot.OnAllocate();
                    slot.OnAllocate();
                    slot.OnFree();
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        const Found found = Find("ManyWriters");
        slot.Release();
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.live, uint64_t{kThreads} * kRounds);
        EXPECT_EQ(found.entry.allocations, uint64_t{kThreads} * kRounds * 2);
        EXPECT_GE(found.entry.peak, found.entry.live);
        EXPECT_LE(found.entry.peak, found.entry.allocations);
    }

    TEST(PoolStatsTest, ASlotReusedWhileItIsCopiedNeverShowsAsAMixOfTwoPools) {
        constexpr uint32_t kCycles = 20000;
        constexpr uint64_t kSizeA = 64;
        constexpr uint64_t kSizeB = 128;
        constexpr uint64_t kAllocationsA = 3;
        constexpr uint64_t kAllocationsB = 5;
        std::atomic<uint32_t> finished{0};

        // Both churners reuse the same few free slots, so a reader copying one slot meets the other pool in it
        auto churn = [&finished](const char* name, uint64_t size, uint64_t allocations) {
            for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
                PoolStatsSlot slot = PoolStatsSlot::Claim(name, size, false);
                for (uint64_t i = 0; i < allocations; ++i) {
                    slot.OnAllocate();
                }
                slot.Release();
            }
            finished.fetch_add(1, std::memory_order_release);
        };
        std::thread a(churn, "ChurnA", kSizeA, kAllocationsA);
        std::thread b(churn, "ChurnB", kSizeB, kAllocationsB);

        uint32_t torn = 0;
        while (finished.load(std::memory_order_acquire) < 2) {
            for (const PoolStatsEntry& entry : GetPoolStats()) {
                const bool isA = std::strcmp(entry.name, "ChurnA") == 0;
                const bool isB = std::strcmp(entry.name, "ChurnB") == 0;
                if ((isA && (entry.blockSize != kSizeA || entry.allocations > kAllocationsA || entry.live > kAllocationsA)) ||
                    (isB && (entry.blockSize != kSizeB || entry.allocations > kAllocationsB || entry.live > kAllocationsB))) {
                    ++torn;
                }
            }
        }
        a.join();
        b.join();
        EXPECT_EQ(torn, 0u);
    }

    TEST(PoolStatsTest, TheAllocTrackerForwardsTheRegistry) {
        constexpr uint64_t kSize = 72;
        constexpr uint64_t kBlocks = 5;
        PoolStatsSlot slot = PoolStatsSlot::Claim("ForwardedPool", kSize, false);
        for (uint64_t i = 0; i < kBlocks; ++i) {
            slot.OnAllocate();
        }

        const Vector<PoolStatsEntry> viaTracker = AllocTracker::GetPoolStats();
        const Vector<PoolStatsEntry> direct = GetPoolStats();
        ASSERT_EQ(viaTracker.size(), direct.size());
        bool seen = false;
        for (const PoolStatsEntry& entry : viaTracker) {
            if (std::strcmp(entry.name, "ForwardedPool") == 0) {
                seen = true;
                EXPECT_EQ(entry.blockSize, kSize);
                EXPECT_EQ(entry.live, kBlocks);
            }
        }
        EXPECT_TRUE(seen);
        slot.Release();
    }

}  // namespace BECore::Tests
