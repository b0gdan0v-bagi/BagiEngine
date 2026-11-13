#include "LockHooks.h"

#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerClock.h>

#if BE_LOCK_SITES

namespace BECore::LockHooks {

#if BE_PROFILER
    namespace {

        constexpr double kNanosecondsPerMicrosecond = 1000.0;

    }  // namespace
#endif

    uint64_t WaitBegin() noexcept {
#if BE_PROFILER
        // No backend, no clock read: a lock waited on in a run that does not profile costs the blocking lock and one atomic load
        if (Profiler::GetBackend() != nullptr) {
            return ProfilerClock::Now();
        }
#endif
        return 0;
    }

    void WaitEnd(const LockSite& site, uint64_t beginTicks) noexcept {
#if BE_PROFILER
        if (beginTicks == 0) {
            return;
        }
        IProfilerBackend* backend = Profiler::GetBackend();
        if (backend == nullptr) {
            return;
        }
        const uint64_t endTicks = ProfilerClock::Now();
        const double waitedUs = ProfilerClock::ToNanoseconds(endTicks - beginTicks) / kNanosecondsPerMicrosecond;
        if (waitedUs >= static_cast<double>(Profiler::GetLockWaitThresholdUs())) {
            backend->RecordZone(site.wait, beginTicks, endTicks);
        }
#else
        static_cast<void>(site);
        static_cast<void>(beginTicks);
#endif
    }

}  // namespace BECore::LockHooks

#endif
