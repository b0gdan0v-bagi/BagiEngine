#include <BECore/Control/Json.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
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
            eastl::vector<Wide> wide(16);
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
        for (const Json& event : *FindJsonMember(*parsed, "traceEvents")) {
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

    TEST(ProfilerCostTest, DISABLED_NewAndDelete) {
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
