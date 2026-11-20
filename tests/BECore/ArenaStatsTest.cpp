#include <BECore/Memory/ArenaStats.h>
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
            ArenaStatsEntry entry;
        };

        Found Find(const char* name) {
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (std::strcmp(entry.name, name) == 0) {
                    return {true, entry};
                }
            }
            return {};
        }

        /// Claims every free slot and returns the handles, so a test can see the table full
        Vector<ArenaStatsSlot> ClaimAll() {
            Vector<ArenaStatsSlot> slots;
            for (uint32_t i = 0; i < ArenaStats::kMaxArenas; ++i) {
                ArenaStatsSlot slot = ArenaStatsSlot::Claim("FillerArena");
                if (!slot.IsValid()) {
                    break;
                }
                slots.push_back(slot);
            }
            return slots;
        }

    }  // namespace

    TEST(ArenaStatsTest, ASnapshotHoldsWhatTheOwnerPublished) {
        ArenaStatsSlot slot = ArenaStatsSlot::Claim("PublishedArena");
        ASSERT_TRUE(slot.IsValid());
        slot.PublishCapacity(4096, 2);
        slot.PublishUsed(300);
        slot.PublishUsed(100);
        slot.CountOverflow();
        slot.CountOverflow();
        slot.CountReset();

        const Found found = Find("PublishedArena");
        slot.Release();
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.capacity, 4096u);
        EXPECT_EQ(found.entry.chunks, 2u);
        EXPECT_EQ(found.entry.used, 100u);
        EXPECT_EQ(found.entry.peak, 300u);
        EXPECT_EQ(found.entry.overflows, 2u);
        EXPECT_EQ(found.entry.resets, 1u);
    }

    TEST(ArenaStatsTest, AReleasedSlotLeavesTheSnapshotAndIsReusedClean) {
        const uint32_t before = ArenaStats::GetLiveCount();
        ArenaStatsSlot first = ArenaStatsSlot::Claim("FirstUse");
        first.PublishUsed(777);
        first.CountOverflow();
        EXPECT_EQ(ArenaStats::GetLiveCount(), before + 1);
        first.Release();
        EXPECT_FALSE(first.IsValid());
        EXPECT_FALSE(Find("FirstUse").found);
        EXPECT_EQ(ArenaStats::GetLiveCount(), before);

        ArenaStatsSlot second = ArenaStatsSlot::Claim("SecondUse");
        const Found found = Find("SecondUse");
        second.Release();
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.used, 0u);
        EXPECT_EQ(found.entry.peak, 0u);
        EXPECT_EQ(found.entry.overflows, 0u);
    }

    TEST(ArenaStatsTest, AFullTableAnswersNoneAndTheNoneHandleIsInert) {
        Vector<ArenaStatsSlot> slots = ClaimAll();
        const ArenaStatsSlot none = ArenaStatsSlot::Claim("OneTooMany");
        EXPECT_FALSE(none.IsValid());

        ArenaStatsSlot inert;
        inert.PublishCapacity(1, 1);
        inert.PublishUsed(1);
        inert.CountOverflow();
        inert.CountReset();
        inert.Release();
        EXPECT_FALSE(Find("OneTooMany").found);

        ASSERT_FALSE(slots.empty());
        slots.back().Release();
        ArenaStatsSlot again = ArenaStatsSlot::Claim("FitsAgain");
        EXPECT_TRUE(again.IsValid());
        again.Release();
        slots.pop_back();
        for (ArenaStatsSlot& slot : slots) {
            slot.Release();
        }
    }

    TEST(ArenaStatsTest, TotalsSumTheArenasAlive) {
        const ArenaTotals before = GetArenaTotals();
        ArenaStatsSlot a = ArenaStatsSlot::Claim("TotalsA");
        ArenaStatsSlot b = ArenaStatsSlot::Claim("TotalsB");
        a.PublishCapacity(1000, 1);
        a.PublishUsed(40);
        a.CountOverflow();
        b.PublishCapacity(2000, 1);
        b.PublishUsed(60);
        b.PublishUsed(10);
        b.CountOverflow();
        b.CountOverflow();

        const ArenaTotals totals = GetArenaTotals();
        a.Release();
        b.Release();
        EXPECT_EQ(totals.capacity - before.capacity, 3000u);
        EXPECT_EQ(totals.used - before.used, 50u);
        EXPECT_EQ(totals.peak - before.peak, 100u);
        EXPECT_EQ(totals.overflows - before.overflows, 3u);

        const ArenaTotals after = GetArenaTotals();
        EXPECT_EQ(after.capacity, before.capacity);
        EXPECT_EQ(after.used, before.used);
    }

    TEST(ArenaStatsTest, AThreadThatClaimsAndEndsLeavesNoEntry) {
        const uint32_t before = ArenaStats::GetLiveCount();
        std::atomic<bool> counted{false};
        std::thread thread([&] {
            ArenaStatsSlot slot = ArenaStatsSlot::Claim("ThreadArena");
            slot.PublishUsed(5);
            counted.store(Find("ThreadArena").found, std::memory_order_relaxed);
            slot.Release();
        });
        thread.join();
        EXPECT_TRUE(counted.load(std::memory_order_relaxed));
        EXPECT_FALSE(Find("ThreadArena").found);
        EXPECT_EQ(ArenaStats::GetLiveCount(), before);
    }

    TEST(ArenaStatsTest, ConcurrentClaimsGetDistinctSlots) {
        constexpr uint32_t kThreads = 8;
        const uint32_t before = ArenaStats::GetLiveCount();
        std::atomic<uint32_t> claimed{0};
        std::atomic<bool> release{false};
        std::atomic<uint32_t> valid{0};

        Vector<std::thread> threads;
        for (uint32_t i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                ArenaStatsSlot slot = ArenaStatsSlot::Claim("ConcurrentArena");
                slot.PublishUsed(i + 1);
                if (slot.IsValid()) {
                    valid.fetch_add(1, std::memory_order_relaxed);
                }
                claimed.fetch_add(1, std::memory_order_release);
                WaitUntil([&] { return release.load(std::memory_order_acquire); });
                slot.Release();
            });
        }
        const bool allClaimed = WaitUntil([&] { return claimed.load(std::memory_order_acquire) == kThreads; });

        uint32_t sumOfUsed = 0;
        uint32_t entries = 0;
        for (const ArenaStatsEntry& entry : GetArenaStats()) {
            if (std::strcmp(entry.name, "ConcurrentArena") == 0) {
                ++entries;
                sumOfUsed += static_cast<uint32_t>(entry.used);
            }
        }
        const uint32_t live = ArenaStats::GetLiveCount();

        release.store(true, std::memory_order_release);
        for (std::thread& thread : threads) {
            thread.join();
        }
        ASSERT_TRUE(allClaimed);
        EXPECT_EQ(valid.load(), kThreads);
        EXPECT_EQ(live, before + kThreads);
        EXPECT_EQ(entries, kThreads);
        // Each thread published its own number into its own slot: two threads on one slot would lose one
        EXPECT_EQ(sumOfUsed, kThreads * (kThreads + 1) / 2);
        EXPECT_EQ(ArenaStats::GetLiveCount(), before);
    }

}  // namespace BECore::Tests
