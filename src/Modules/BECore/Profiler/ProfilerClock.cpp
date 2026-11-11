#include "ProfilerClock.h"

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <chrono>
#endif

namespace BECore {

#if defined(__APPLE__)

    namespace {

        /// mach_absolute_time counts in units of numer / denom nanoseconds (1 / 1 on Intel, 125 / 3 on Apple silicon)
        double NanosecondsPerTick() {
            static const double ratio = [] {
                mach_timebase_info_data_t info{};
                mach_timebase_info(&info);
                return static_cast<double>(info.numer) / static_cast<double>(info.denom);
            }();
            return ratio;
        }

    }  // namespace

    uint64_t ProfilerClock::Now() {
        // A register read without a system call: cheaper than steady_clock, which goes through clock_gettime
        return mach_absolute_time();
    }

    double ProfilerClock::ToNanoseconds(uint64_t ticks) {
        return static_cast<double>(ticks) * NanosecondsPerTick();
    }

#else

    uint64_t ProfilerClock::Now() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    double ProfilerClock::ToNanoseconds(uint64_t ticks) {
        return static_cast<double>(ticks);
    }

#endif

}  // namespace BECore
