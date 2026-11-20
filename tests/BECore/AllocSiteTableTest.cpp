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

}  // namespace BECore::Tests
