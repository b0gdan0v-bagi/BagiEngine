#include <BECore/Control/Json.h>
#include <BECore/Profiler/Alloc/AllocRouting.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <Common/Require.h>
#include <chrono>
#include <cstdio>
#include <gtest/gtest.h>
#include <new>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// Keeps the optimizer from removing an allocation whose result is unused
        void Use(void* ptr) {
            static std::atomic<void*> sink;
            sink.store(ptr, std::memory_order_relaxed);
        }

    }  // namespace

#if BE_PROFILER_ALLOC

    namespace {

        constexpr size_t kBlock = 1000;

        /// The statistics of the zone named @p name (no calls when it has none)
        ZoneAllocStats StatsOf(const char* name) {
            for (const ZoneAllocStats& stats : AllocTracker::GetZoneStats()) {
                if (std::strcmp(stats.site->name, name) == 0) {
                    return stats;
                }
            }
            return {};
        }

        struct alignas(64) Wide {
            char bytes[64];
        };

    }  // namespace

    TEST(AllocTrackerTest, AnAllocationIsCountedInItsZoneAndItsParent) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("AllocParent");
            void* outer = ::operator new(kBlock);
            {
                BE_PROFILE_SCOPE("AllocChild");
                void* inner = ::operator new(kBlock * 2);
                ::operator delete(inner);
            }
            ::operator delete(outer);
        }
        const ZoneAllocStats parent = StatsOf("AllocParent");
        const ZoneAllocStats child = StatsOf("AllocChild");
        EXPECT_EQ(parent.calls, 1u);
        EXPECT_EQ(parent.count, 2u);
        EXPECT_EQ(parent.bytes, kBlock * 3);
        EXPECT_EQ(child.count, 1u);
        EXPECT_EQ(child.bytes, kBlock * 2);
    }

    TEST(AllocTrackerTest, AFreeLowersTheLiveBytesAndCountsTheTotals) {
        const AllocTotals before = AllocTracker::GetTotals();
        void* ptr = ::operator new(kBlock * 100);
        Use(ptr);
        const AllocTotals during = AllocTracker::GetTotals();
        ::operator delete(ptr);
        const AllocTotals after = AllocTracker::GetTotals();

        EXPECT_GE(during.count, before.count + 1);
        EXPECT_GE(during.bytes, before.bytes + kBlock * 100);
        EXPECT_GE(during.liveBytes, before.liveBytes + kBlock * 100);
        EXPECT_GE(during.largest, kBlock * 100);
        EXPECT_GE(during.peakLiveBytes, during.liveBytes);
        EXPECT_GE(after.frees, during.frees + 1);
        EXPECT_LE(after.liveBytes + kBlock * 100, during.liveBytes);
    }

    TEST(AllocTrackerTest, EveryFormOfNewAndDeleteIsCounted) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("AllocForms");
            ::operator delete(::operator new(8));
            ::operator delete[](::operator new[](8));
            ::operator delete(::operator new(8, std::nothrow), std::nothrow);
            ::operator delete[](::operator new[](8, std::nothrow), std::nothrow);
            ::operator delete(::operator new(8), 8);
            ::operator delete[](::operator new[](8), 8);
            ::operator delete(::operator new(8, std::align_val_t{64}), std::align_val_t{64});
            ::operator delete[](::operator new[](8, std::align_val_t{64}), std::align_val_t{64});
            ::operator delete(::operator new(8, std::align_val_t{64}), 8, std::align_val_t{64});
            ::operator delete[](::operator new[](8, std::align_val_t{64}), 8, std::align_val_t{64});
            ::operator delete(::operator new(8, std::align_val_t{64}, std::nothrow), std::align_val_t{64}, std::nothrow);
            ::operator delete[](::operator new[](8, std::align_val_t{64}, std::nothrow), std::align_val_t{64}, std::nothrow);
        }
        EXPECT_EQ(StatsOf("AllocForms").count, 12u);
        EXPECT_EQ(StatsOf("AllocForms").bytes, 12u * 8);
    }

    TEST(AllocTrackerTest, AnOverAlignedEastlVectorIsCountedAndFreed) {
        const AllocTotals before = AllocTracker::GetTotals();
        {
            Vector<Wide> wide(16);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(wide.data()) % alignof(Wide), 0u);
        }
        const AllocTotals after = AllocTracker::GetTotals();
        EXPECT_GE(after.count, before.count + 1);
        EXPECT_GE(after.frees, before.frees + 1);
    }

    TEST(AllocTrackerTest, AnotherThreadDoesNotCountInThisZone) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("AllocThisThread");
            std::thread other([] { ::operator delete(::operator new(kBlock)); });
            other.join();
        }
        // std::thread itself may allocate its state here; the other thread's 1000 bytes are not in the zone
        EXPECT_LT(StatsOf("AllocThisThread").bytes, kBlock);
    }

    TEST(AllocTrackerTest, AnAllocationInANoAllocScopeIsReported) {
        const uint64_t before = AllocTracker::GetTotals().noAllocViolations;
        {
            BE_PROFILE_NO_ALLOC("Allocates");
            ::operator delete(::operator new(kBlock));
        }
        EXPECT_EQ(AllocTracker::GetTotals().noAllocViolations, before + 1);
    }

    TEST(AllocTrackerTest, ACleanNoAllocScopeReportsNothing) {
        const uint64_t before = AllocTracker::GetTotals().noAllocViolations;
        int sum = 0;
        {
            BE_PROFILE_NO_ALLOC("Clean");
            for (int i = 0; i < 10; ++i) {
                sum += i;
            }
        }
        EXPECT_EQ(sum, 45);
        EXPECT_EQ(AllocTracker::GetTotals().noAllocViolations, before);
    }

    TEST(AllocTrackerTest, AnAllocationAndItsFreeInAPauseMoveNothing) {
        AllocTracker::ResetZoneStats();
        const AllocTracker::ThreadCounters threadBefore = AllocTracker::GetThreadCounters();
        const uint64_t violationsBefore = AllocTracker::GetTotals().noAllocViolations;
        {
            BE_PROFILE_NO_ALLOC("PausedAllocates");
            BE_PROFILE_SCOPE("PausedZone");
            const ScopedAllocCountersPause pause;
            ::operator delete(::operator new(kBlock));
            void* kept = ::operator new(kBlock);
            Use(kept);
            ::operator delete(kept);
        }
        const AllocTracker::ThreadCounters threadAfter = AllocTracker::GetThreadCounters();
        EXPECT_EQ(threadAfter.count, threadBefore.count);
        EXPECT_EQ(threadAfter.bytes, threadBefore.bytes);
        EXPECT_EQ(StatsOf("PausedZone").count, 0u);
        EXPECT_EQ(AllocTracker::GetTotals().noAllocViolations, violationsBefore);
    }

    TEST(AllocTrackerTest, PausesNestAndCountingComesBackAfterTheLastOne) {
        const uint64_t start = AllocTracker::GetThreadCounters().count;
        {
            const ScopedAllocCountersPause outer;
            {
                const ScopedAllocCountersPause inner;
                ::operator delete(::operator new(kBlock));
            }
            ::operator delete(::operator new(kBlock));
            EXPECT_EQ(AllocTracker::GetThreadCounters().count, start);
        }
        ::operator delete(::operator new(kBlock));
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, start + 1);
    }

    TEST(AllocTrackerTest, APauseOfOneThreadLeavesAnotherThreadCounting) {
        const ScopedAllocCountersPause pause;
        uint64_t counted = 0;
        std::thread other([&counted] {
            const uint64_t before = AllocTracker::GetThreadCounters().count;
            ::operator delete(::operator new(kBlock));
            counted = AllocTracker::GetThreadCounters().count - before;
        });
        other.join();
        EXPECT_EQ(counted, 1u);
    }

    TEST(AllocTrackerTest, ABlockMadeInAPauseAndFreedAfterItDoesNotWrapTheLiveBytes) {
        void* block = nullptr;
        {
            const ScopedAllocCountersPause pause;
            block = ::operator new(kBlock);
        }
        const AllocTotals before = AllocTracker::GetTotals();
        ::operator delete(block);
        const AllocTotals after = AllocTracker::GetTotals();
        EXPECT_GE(after.frees, before.frees + 1);
        EXPECT_LT(after.liveBytes, before.liveBytes + kBlock * 16);
    }

    TEST(AllocTrackerTest, ARoutedAllocationMovesTheTotalsTheSourceTheThreadAndTheZone) {
        AllocTracker::ResetZoneStats();
        const AllocTotals totalsBefore = AllocTracker::GetTotals();
        const AllocSourceTotals sourceBefore = AllocTracker::GetSourceTotals(AllocSource::Stb);
        const uint64_t threadBefore = AllocTracker::GetThreadCounters().count;
        AllocSourceTotals sourceDuring;
        {
            BE_PROFILE_SCOPE("AllocRouted");
            void* ptr = AllocRouting::Malloc(AllocSource::Stb, kBlock);
            ASSERT_NE(ptr, nullptr);
            sourceDuring = AllocTracker::GetSourceTotals(AllocSource::Stb);
            AllocRouting::Free(AllocSource::Stb, ptr);
        }
        const AllocTotals totalsAfter = AllocTracker::GetTotals();
        const AllocSourceTotals sourceAfter = AllocTracker::GetSourceTotals(AllocSource::Stb);

        EXPECT_EQ(sourceDuring.count, sourceBefore.count + 1);
        EXPECT_EQ(sourceDuring.bytes, sourceBefore.bytes + kBlock);
        EXPECT_GE(sourceDuring.liveBytes, sourceBefore.liveBytes + kBlock);
        EXPECT_EQ(sourceAfter.frees, sourceBefore.frees + 1);
        EXPECT_EQ(sourceAfter.liveBytes, sourceBefore.liveBytes);
        EXPECT_EQ(totalsAfter.count, totalsBefore.count + 1);
        EXPECT_EQ(totalsAfter.frees, totalsBefore.frees + 1);
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, threadBefore + 1);
        EXPECT_EQ(StatsOf("AllocRouted").count, 1u);
        EXPECT_EQ(StatsOf("AllocRouted").bytes, kBlock);
    }

    TEST(AllocTrackerTest, ASourceDoesNotCountTheAllocationsOfAnother) {
        const AllocSourceTotals stbBefore = AllocTracker::GetSourceTotals(AllocSource::Stb);
        const AllocSourceTotals globalBefore = AllocTracker::GetSourceTotals(AllocSource::GlobalNew);
        void* ptr = ::operator new(kBlock);
        Use(ptr);
        ::operator delete(ptr);
        EXPECT_EQ(AllocTracker::GetSourceTotals(AllocSource::GlobalNew).count, globalBefore.count + 1);
        EXPECT_EQ(AllocTracker::GetSourceTotals(AllocSource::Stb).count, stbBefore.count);

        void* routed = AllocRouting::Malloc(AllocSource::Stb, kBlock);
        AllocRouting::Free(AllocSource::Stb, routed);
        EXPECT_EQ(AllocTracker::GetSourceTotals(AllocSource::GlobalNew).count, globalBefore.count + 1);
        EXPECT_EQ(AllocTracker::GetSourceTotals(AllocSource::Stb).count, stbBefore.count + 1);
    }

    TEST(AllocTrackerTest, ACallocCountsTheBytesOfAllItsObjects) {
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::DrLibs);
        void* ptr = AllocRouting::Calloc(AllocSource::DrLibs, 10, kBlock);
        ASSERT_NE(ptr, nullptr);
        AllocRouting::Free(AllocSource::DrLibs, ptr);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::DrLibs);
        EXPECT_EQ(after.count, before.count + 1);
        EXPECT_EQ(after.bytes, before.bytes + kBlock * 10);
    }

    TEST(AllocTrackerTest, AReallocIsOneFreeAndOneAlloc) {
        void* ptr = AllocRouting::Malloc(AllocSource::Stb, kBlock);
        ASSERT_NE(ptr, nullptr);
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::Stb);
        ptr = AllocRouting::Realloc(AllocSource::Stb, ptr, kBlock * 50);
        ASSERT_NE(ptr, nullptr);
        const AllocSourceTotals during = AllocTracker::GetSourceTotals(AllocSource::Stb);
        EXPECT_EQ(during.count, before.count + 1);
        EXPECT_EQ(during.bytes, before.bytes + kBlock * 50);
        EXPECT_EQ(during.frees, before.frees + 1);
        EXPECT_GE(during.liveBytes, before.liveBytes + kBlock * 40);

        AllocRouting::Free(AllocSource::Stb, ptr);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::Stb);
        EXPECT_EQ(after.frees, before.frees + 2);
        EXPECT_LE(after.liveBytes + kBlock, before.liveBytes);
    }

    TEST(AllocTrackerTest, AReallocOfNothingIsAMalloc) {
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::Stb);
        void* ptr = AllocRouting::Realloc(AllocSource::Stb, nullptr, kBlock);
        ASSERT_NE(ptr, nullptr);
        const AllocSourceTotals during = AllocTracker::GetSourceTotals(AllocSource::Stb);
        AllocRouting::Free(AllocSource::Stb, ptr);
        EXPECT_EQ(during.count, before.count + 1);
        EXPECT_EQ(during.frees, before.frees);
    }

    TEST(AllocTrackerTest, AFreeOfABlockMadeBeforeTheRoutingDoesNotWrapTheLiveBytes) {
        constexpr size_t kBig = size_t{1} << 20;
        void* foreign = std::malloc(kBig);
        ASSERT_NE(foreign, nullptr);
        const AllocTotals totalsBefore = AllocTracker::GetTotals();
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::DrLibs);
        AllocRouting::Free(AllocSource::DrLibs, foreign);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::DrLibs);
        EXPECT_GE(after.frees, before.frees + 1);
        EXPECT_LT(after.liveBytes, before.liveBytes + kBlock * 16);
        EXPECT_LE(AllocTracker::GetTotals().liveBytes, totalsBefore.liveBytes + kBig);
        EXPECT_EQ(after.liveBytes, before.liveBytes > kBig ? before.liveBytes - kBig : 0u);
    }

    TEST(AllocTrackerTest, ARoutedAllocationInANoAllocScopeIsReported) {
        const uint64_t before = AllocTracker::GetTotals().noAllocViolations;
        {
            BE_PROFILE_NO_ALLOC("RoutedAllocates");
            AllocRouting::Free(AllocSource::Pugixml, AllocRouting::Malloc(AllocSource::Pugixml, kBlock));
        }
        EXPECT_EQ(AllocTracker::GetTotals().noAllocViolations, before + 1);
    }

    TEST(AllocTrackerTest, TheChromeTraceCarriesTheAllocationsOfAZone) {
        ChromeTraceBackend recorder;
        Profiler::SetBackend(&recorder);
        {
            BE_PROFILE_SCOPE("Traced");
            ::operator delete(::operator new(kBlock));
        }
        Profiler::SetBackend(nullptr);
        const auto parsed = ParseJson(recorder.WriteJson());
        ASSERT_TRUE(parsed.has_value());
        bool found = false;
        for (const Json& event : Require(FindJsonMember(*parsed, "traceEvents"))) {
            if (GetJsonString(event, "ph") == String("E")) {
                const Json* args = FindJsonMember(event, "args");
                ASSERT_NE(args, nullptr);
                EXPECT_EQ(GetJsonInt(*args, "allocs"), 1);
                EXPECT_EQ(GetJsonInt(*args, "bytes"), static_cast<int64_t>(kBlock));
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

#else

    TEST(AllocTrackerTest, WithoutTheOptionNothingIsCounted) {
        void* ptr = ::operator new(1000);
        ::operator delete(ptr);
        const AllocTotals totals = AllocTracker::GetTotals();
        EXPECT_EQ(totals.count, 0u);
        EXPECT_EQ(totals.liveBytes, 0u);
        EXPECT_TRUE(AllocTracker::GetZoneStats().empty());
    }

#endif

    // ---- Cost (run by hand: --gtest_also_run_disabled_tests --gtest_filter='ProfilerCost*') ----

    TEST(AllocTrackerTest, DISABLED_TheCostOfNewAndDelete) {
        constexpr int kCalls = 1000000;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kCalls; ++i) {
            void* ptr = ::operator new(64);
            Use(ptr);
            ::operator delete(ptr);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        std::printf("new + delete of 64 bytes: %.1f ns (allocation tracking %s)\n", static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) / kCalls,
                    AllocTracker::kEnabled ? "on" : "off");
    }

}  // namespace BECore::Tests
