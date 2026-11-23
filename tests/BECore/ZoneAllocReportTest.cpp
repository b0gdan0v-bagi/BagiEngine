#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Profile.h>
#include <Common/AllocCheck.h>
#include <Common/Require.h>
#include <Common/ZoneAllocReport.h>
#include <atomic>
#include <gtest/gtest.h>
#include <new>
#include <sstream>  //-NoStd: PrintZoneReport writes to a std::ostream (gtest's printer)

namespace BECore::Tests {

    namespace {

        constexpr size_t kOuterBlock = 100;
        constexpr size_t kInnerBlock = 5000;
        constexpr const char* kOuterZone = "Report outer";
        constexpr const char* kInnerZone = "Report inner";
        constexpr const char* kBlockName = "Report blocks";

        /// One block of 100 bytes named kBlockName in the outer zone, one of 5000 bytes in the zone inside it
        void AllocateInZones() {
            AllocTracker::ResetZoneStats();
            BE_PROFILE_SCOPE("Report outer");
            Vector<char> named;
            named.get_allocator().set_name(kBlockName);
            named.reserve(kOuterBlock);
            {
                BE_PROFILE_SCOPE("Report inner");
                // A global new carries no allocation name; the sink keeps the optimizer from dropping the pair
                static std::atomic<void*> sink;
                void* block = ::operator new(kInnerBlock);
                sink.store(block, std::memory_order_relaxed);
                ::operator delete(block);
            }
        }

        String Printed(StringView caseName, size_t topNames) {
            std::ostringstream out;  //-NoStd: PrintZoneReport takes a std::ostream
            PrintZoneReport(out, caseName, topNames);
            return String(out.str().c_str());
        }

    }  // namespace

    TEST(ZoneAllocReportTest, WithoutTheTrackerTheReportIsOneSkippedLine) {
        if constexpr (AllocTracker::kEnabled) {
            GTEST_SKIP() << "the tracker is on: the other cases check the report";
        }
        EXPECT_TRUE(CollectZoneReport().empty());
        EXPECT_EQ(Printed("none", 3), "AssetLoadZone case=none skipped=\"allocations are counted only with BE_PROFILER_ALLOC\"\n");
    }

    TEST(ZoneAllocReportTest, AZoneRowHoldsItsInclusiveAndItsOwnFigures) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        AllocateInZones();
        const Vector<ZoneReportRow> rows = CollectZoneReport();
        const ZoneReportRow& outer = Require(FindZoneRow(rows, kOuterZone));
        const ZoneReportRow& inner = Require(FindZoneRow(rows, kInnerZone));

        EXPECT_EQ(outer.calls, 1u);
        EXPECT_EQ(outer.allocs, 2u);
        EXPECT_EQ(outer.bytes, kOuterBlock + kInnerBlock);
        EXPECT_EQ(outer.selfAllocs, 1u);
        EXPECT_EQ(inner.allocs, 1u);
        EXPECT_EQ(inner.bytes, kInnerBlock);
        EXPECT_EQ(inner.selfAllocs, 1u);

        ASSERT_EQ(outer.sizes.size(), 1u);
        EXPECT_EQ(At(outer.sizes, 0).upTo, 128u);
        EXPECT_EQ(At(outer.sizes, 0).count, 1u);
        ASSERT_EQ(inner.sizes.size(), 1u);
        EXPECT_EQ(At(inner.sizes, 0).upTo, 8192u);

        ASSERT_EQ(inner.names.size(), 1u);
        EXPECT_EQ(At(inner.names, 0).name, "(unnamed)");
        ASSERT_EQ(outer.names.size(), 1u);
        EXPECT_EQ(At(outer.names, 0).name, kBlockName);
        EXPECT_EQ(At(outer.names, 0).bytes, kOuterBlock);
    }

    TEST(ZoneAllocReportTest, TheRowsAreInTheOrderOfBytesAndTheNullZoneIsLabelled) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        AllocateInZones();
        const Vector<ZoneReportRow> rows = CollectZoneReport();
        for (size_t i = 1; i < rows.size(); ++i) {
            EXPECT_GE(rows[i - 1].bytes, rows[i].bytes) << i;
        }
        // An allocation made by the case outside both zones is the null zone's
        AllocTracker::ResetZoneStats();
        Vector<char> outside;
        outside.reserve(kOuterBlock);
        const Vector<ZoneReportRow> outsideRows = CollectZoneReport();
        const ZoneReportRow& none = Require(FindZoneRow(outsideRows, AllocSiteStats::kNoZone));
        EXPECT_EQ(none.calls, 0u);
        EXPECT_GE(none.bytes, kOuterBlock);
    }

    TEST(ZoneAllocReportTest, ThePrintedLinesHaveTheDocumentedFormat) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        AllocateInZones();
        const Vector<ZoneReportRow> rows = CollectZoneReport();
        const ZoneReportRow& outer = Require(FindZoneRow(rows, kOuterZone));

        ASSERT_EQ(outer.sizes.size(), 1u);
        ASSERT_EQ(outer.names.size(), 1u);
        const String printed = Printed("probe", 1);
        const String zoneLine = Format("AssetLoadZone case=probe zone=\"Report outer\" calls={} allocs={} bytes={} selfAllocs={}\n", outer.calls, outer.allocs, outer.bytes, outer.selfAllocs);
        const String sizesLine = Format("AssetLoadZoneSizes case=probe zone=\"Report outer\" sizes={}:{}\n", At(outer.sizes, 0).upTo, At(outer.sizes, 0).count);
        const String namesLine = Format("AssetLoadZoneNames case=probe zone=\"Report outer\" name=\"Report blocks\" allocs={} bytes={}\n", At(outer.names, 0).allocs, At(outer.names, 0).bytes);
        EXPECT_NE(printed.find(zoneLine), String::npos) << printed.c_str();
        EXPECT_NE(printed.find(sizesLine), String::npos) << printed.c_str();
        EXPECT_NE(printed.find(namesLine), String::npos) << printed.c_str();
        EXPECT_NE(printed.find("AssetLoadZone case=probe zone=\"Report inner\" calls=1 allocs=1 bytes=5000 selfAllocs=1\n"), String::npos);
    }

    TEST(ZoneAllocReportTest, OnlyTheFirstTopNamesOfAZoneArePrinted) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        AllocateInZones();
        EXPECT_EQ(Printed("probe", 0).find("AssetLoadZoneNames case=probe zone=\"Report outer\""), String::npos);
    }

}  // namespace BECore::Tests
