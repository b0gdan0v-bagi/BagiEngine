#include <BECore/Profiler/Alloc/AllocSiteTable.h>
#include <cstdio>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kZoneA{"A", "f", "a.cpp", 1, 0};
        constexpr ProfileSite kZoneB{"B", "g", "b.cpp", 2, 0};

        // Two arrays with equal text: the same literal as two translation units hold it
        constexpr char kNameX[] = "X";
        constexpr char kNameXCopy[] = "X";
        constexpr char kNameY[] = "Y";

        /// The row of the pair, or an empty one
        AllocSiteStats Find(const Vector<AllocSiteStats>& rows, const ProfileSite* zone, const char* name) {
            for (const AllocSiteStats& row : rows) {
                if (row.zone == zone && std::strcmp(row.name != nullptr ? row.name : "", name != nullptr ? name : "") == 0) {
                    return row;
                }
            }
            return {};
        }

        // The table is a few hundred KB: on the heap, not on the stack
        UniquePtr<AllocSiteTable> MakeTable() {
            return MakeUnique<AllocSiteTable>();
        }

        /// The size row of @p zone, or an empty one
        AllocSizeStats FindSizes(const Vector<AllocSizeStats>& rows, const ProfileSite* zone) {
            for (const AllocSizeStats& row : rows) {
                if (row.zone == zone) {
                    return row;
                }
            }
            return {};
        }

    }  // namespace

    TEST(AllocSiteTableTest, TheSamePairAddsUp) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 100);
        table->Add(&kZoneA, kNameX, 50);
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].zone, &kZoneA);
        EXPECT_STREQ(rows[0].name, "X");
        EXPECT_EQ(rows[0].count, 2u);
        EXPECT_EQ(rows[0].bytes, 150u);
    }

    TEST(AllocSiteTableTest, TheSameNameInTwoZonesAndTwoNamesInOneZoneAreSeparateRows) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 10);
        table->Add(&kZoneB, kNameX, 20);
        table->Add(&kZoneA, kNameY, 30);
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(Find(rows, &kZoneA, "X").bytes, 10u);
        EXPECT_EQ(Find(rows, &kZoneB, "X").bytes, 20u);
        EXPECT_EQ(Find(rows, &kZoneA, "Y").bytes, 30u);
    }

    TEST(AllocSiteTableTest, ANullZoneAndANullNameAreTheirOwnKeys) {
        auto table = MakeTable();
        table->Add(nullptr, kNameX, 1);
        table->Add(&kZoneA, nullptr, 2);
        table->Add(nullptr, nullptr, 4);
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(Find(rows, nullptr, "X").bytes, 1u);
        EXPECT_EQ(Find(rows, &kZoneA, nullptr).bytes, 2u);
        EXPECT_EQ(Find(rows, nullptr, nullptr).bytes, 4u);
    }

    TEST(AllocSiteTableTest, TwoPointersWithEqualTextMergeOnRead) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 10);
        table->Add(&kZoneA, kNameXCopy, 5);
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].count, 2u);
        EXPECT_EQ(rows[0].bytes, 15u);
    }

    TEST(AllocSiteTableTest, TheMostBytesComeFirst) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 10);
        table->Add(&kZoneA, kNameY, 300);
        table->Add(&kZoneB, kNameX, 20);
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(rows[0].bytes, 300u);
        EXPECT_EQ(rows[1].bytes, 20u);
        EXPECT_EQ(rows[2].bytes, 10u);
    }

    TEST(AllocSiteTableTest, AFullNamePointerTableDropsAndCounts) {
        auto table = MakeTable();
        constexpr uint32_t kExtra = 76;
        constexpr uint32_t kNames = AllocSiteTable::kNamePointers + kExtra;
        // Every name is its own pointer
        Vector<String> names;
        names.reserve(kNames);
        for (uint32_t i = 0; i < kNames; ++i) {
            names.push_back(Format("{}", i));
        }
        for (const String& name : names) {
            table->Add(&kZoneA, name.c_str(), 1);
        }
        EXPECT_EQ(table->GetOverflow(), kExtra);
        EXPECT_EQ(table->GetStats().size(), AllocSiteTable::kNamePointers);
        // A pointer that is already in the table still counts
        table->Add(&kZoneA, names[0].c_str(), 1);
        EXPECT_EQ(table->GetOverflow(), kExtra);
    }

    TEST(AllocSiteTableTest, ResetZeroesTheCountersAndKeepsThePointers) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 10);
        table->Reset();
        EXPECT_TRUE(table->GetStats().empty());
        EXPECT_EQ(table->GetOverflow(), 0u);
        table->Add(&kZoneA, kNameX, 7);
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].count, 1u);
        EXPECT_EQ(rows[0].bytes, 7u);
    }

    TEST(AllocSiteTableTest, ThreadsAddingToTheSamePairsLoseNothing) {
        auto table = MakeTable();
        constexpr uint32_t kThreads = 4;
        constexpr uint32_t kAddsPerThread = 20000;
        Vector<std::thread> threads;
        for (uint32_t t = 0; t < kThreads; ++t) {
            threads.emplace_back([&table] {
                for (uint32_t i = 0; i < kAddsPerThread; ++i) {
                    table->Add(&kZoneA, kNameX, 2);
                    table->Add(&kZoneB, kNameY, 3);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        const Vector<AllocSiteStats> rows = table->GetStats();
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(Find(rows, &kZoneA, "X").count, kThreads * kAddsPerThread);
        EXPECT_EQ(Find(rows, &kZoneA, "X").bytes, 2ull * kThreads * kAddsPerThread);
        EXPECT_EQ(Find(rows, &kZoneB, "Y").bytes, 3ull * kThreads * kAddsPerThread);
        EXPECT_EQ(table->GetOverflow(), 0u);
    }

    // ---- The size histogram ----

    TEST(AllocSiteTableTest, SizeBucketsHoldTheirUpperBound) {
        EXPECT_EQ(SizeBucket(0), 0u);
        EXPECT_EQ(SizeBucket(1), 0u);
        EXPECT_EQ(SizeBucket(2), 1u);
        EXPECT_EQ(SizeBucket(3), 2u);
        EXPECT_EQ(SizeBucket(4), 2u);
        EXPECT_EQ(SizeBucket(5), 3u);
        for (uint32_t k = 1; k + 1 < kSizeBuckets; ++k) {
            const uint64_t bound = uint64_t{1} << k;
            EXPECT_EQ(SizeBucket(bound), k) << "2^" << k;
            EXPECT_EQ(SizeBucket(bound + 1), k + 1) << "2^" << k << " + 1";
            EXPECT_EQ(SizeBucketBound(k), bound);
        }
        EXPECT_EQ(SizeBucketBound(0), 1u);
    }

    TEST(AllocSiteTableTest, TheLastSizeBucketTakesEverythingAboveItsPredecessor) {
        constexpr uint32_t kLast = kSizeBuckets - 1;
        const uint64_t lastBound = SizeBucketBound(kLast - 1);
        EXPECT_EQ(SizeBucket(lastBound), kLast - 1);
        EXPECT_EQ(SizeBucket(lastBound + 1), kLast);
        EXPECT_EQ(SizeBucket(uint64_t{1} << 40), kLast);
        EXPECT_EQ(SizeBucket(UINT64_MAX), kLast);
        EXPECT_EQ(SizeBucketBound(kLast), 0u);
    }

    TEST(AllocSiteTableTest, TheHistogramCountsPerZoneAndTheNullZoneIsItsOwnRow) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 3);
        table->Add(&kZoneA, nullptr, 4);
        table->Add(&kZoneA, kNameY, 5);
        table->Add(&kZoneB, kNameX, 0);
        table->Add(nullptr, nullptr, uint64_t{1} << 30);
        const Vector<AllocSizeStats> rows = table->GetSizeStats();
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(rows[0].zone, &kZoneA);  // most allocations first
        const AllocSizeStats a = FindSizes(rows, &kZoneA);
        EXPECT_EQ(a.allocs, 3u);
        EXPECT_EQ(a.buckets[2], 2u);
        EXPECT_EQ(a.buckets[3], 1u);
        const AllocSizeStats b = FindSizes(rows, &kZoneB);
        EXPECT_EQ(b.allocs, 1u);
        EXPECT_EQ(b.buckets[0], 1u);
        const AllocSizeStats none = FindSizes(rows, nullptr);
        EXPECT_EQ(none.allocs, 1u);
        EXPECT_EQ(none.buckets[kSizeBuckets - 1], 1u);
    }

    TEST(AllocSiteTableTest, ResetZeroesTheHistogram) {
        auto table = MakeTable();
        table->Add(&kZoneA, kNameX, 100);
        table->Reset();
        EXPECT_TRUE(table->GetSizeStats().empty());
        table->Add(&kZoneA, kNameX, 100);
        EXPECT_EQ(FindSizes(table->GetSizeStats(), &kZoneA).allocs, 1u);
    }

    TEST(AllocSiteTableTest, AFullZoneTableCountsTheDropAndLeavesTheOtherZonesBuckets) {
        auto table = MakeTable();
        // Every zone is its own pointer; sites must outlive the table
        Vector<ProfileSite> sites;
        sites.resize(AllocSiteTable::kZonePointers + 1, kZoneA);
        table->Add(&kZoneB, kNameX, 8);
        for (uint32_t i = 0; i < AllocSiteTable::kZonePointers - 1; ++i) {
            table->Add(&sites[i], kNameX, 8);
        }
        EXPECT_EQ(table->GetOverflow(), 0u);
        table->Add(&sites.back(), kNameX, 8);
        EXPECT_EQ(table->GetOverflow(), 1u);
        const Vector<AllocSizeStats> rows = table->GetSizeStats();
        EXPECT_EQ(rows.size(), AllocSiteTable::kZonePointers);
        EXPECT_EQ(FindSizes(rows, &sites.back()).allocs, 0u);
        EXPECT_EQ(FindSizes(rows, &kZoneB).allocs, 1u);
    }

    TEST(AllocSiteTableTest, ThreadsAddingToOneZoneSumTheBucketsExactly) {
        auto table = MakeTable();
        constexpr uint32_t kThreads = 4;
        constexpr uint32_t kAddsPerThread = 20000;
        Vector<std::thread> threads;
        for (uint32_t t = 0; t < kThreads; ++t) {
            threads.emplace_back([&table] {
                for (uint32_t i = 0; i < kAddsPerThread; ++i) {
                    table->Add(&kZoneA, kNameX, 16);
                    table->Add(&kZoneA, kNameY, 17);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        const AllocSizeStats row = FindSizes(table->GetSizeStats(), &kZoneA);
        EXPECT_EQ(row.buckets[4], kThreads * kAddsPerThread);
        EXPECT_EQ(row.buckets[5], kThreads * kAddsPerThread);
        EXPECT_EQ(row.allocs, 2ull * kThreads * kAddsPerThread);
    }

}  // namespace BECore::Tests
