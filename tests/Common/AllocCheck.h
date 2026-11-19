#pragma once

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// Why an allocation check is skipped: without the tracker the counters stay zero, so the check could not fail
    inline constexpr const char* kNoAllocTracker = "allocations are counted only with BE_PROFILER_ALLOC (the macos-profile preset); the other checks of the case ran";

    /**
     * Runs @p body and fails the case when it allocated on the calling thread. Without the tracker the body still runs and
     * the case is marked skipped with kNoAllocTracker, so a build that cannot count does not report "allocates nothing" as
     * a pass. The skip returns from here only: the checks after the call still run.
     */
    template <typename TBody>
    void ExpectNoAllocations(TBody&& body) {
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        body();
        const uint64_t after = AllocTracker::GetThreadCounters().count;
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after, before) << "allocations on this thread";
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
