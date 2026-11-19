#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Utils/ScopeGuard.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        int EarlyReturn(int& runs, bool leaveEarly) {
            ScopeGuard guard([&runs] { ++runs; });
            if (leaveEarly) {
                return 1;
            }
            return 2;
        }

        static_assert(!std::is_copy_constructible_v<ScopeGuard<void (*)()>>);
        static_assert(!std::is_copy_assignable_v<ScopeGuard<void (*)()>>);

    }  // namespace

    TEST(ScopeGuardTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST(ScopeGuardTest, ItRunsOnceAtTheEndOfTheScopeNotBefore) {
        int runs = 0;
        {
            ScopeGuard guard([&runs] { ++runs; });
            EXPECT_EQ(runs, 0);
        }
        EXPECT_EQ(runs, 1);
    }

    TEST(ScopeGuardTest, ItRunsAtAnEarlyReturnAndAtTheNormalOne) {
        int runs = 0;
        EXPECT_EQ(EarlyReturn(runs, true), 1);
        EXPECT_EQ(runs, 1);
        EXPECT_EQ(EarlyReturn(runs, false), 2);
        EXPECT_EQ(runs, 2);
    }

    TEST(ScopeGuardTest, AMoveOnlyCaptureIsUsedAtTheEndOfTheScope) {
        int runs = 0;
        {
            ScopeGuard guard([&runs, owned = MakeUnique<int>(5)]() { runs += *owned; });
            EXPECT_EQ(runs, 0);
        }
        EXPECT_EQ(runs, 5);
    }

    TEST(ScopeGuardTest, GuardsRunInTheReverseOrderOfTheirConstruction) {
        String order;
        {
            ScopeGuard first([&order] { order += "a"; });
            ScopeGuard second([&order] { order += "b"; });
        }
        EXPECT_EQ(order, "ba");
    }

    TEST(ScopeGuardTest, ItAllocatesNothing) {
        if (!AllocTracker::kEnabled) {
            GTEST_SKIP() << "the allocation counters are on only in a build with ENGINE_PROFILER_ALLOC";
        }
        int runs = 0;
        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        {
            ScopeGuard guard([&runs] { ++runs; });
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
        EXPECT_EQ(runs, 1);
        EXPECT_EQ(after.count, before.count);
    }

}  // namespace BECore::Tests
