#pragma once

namespace BECore {

    /// The time stamps of the profiler: raw ticks of the cheapest monotonic clock of the platform, converted only when written out
    class ProfilerClock {
    public:
        ProfilerClock() = delete;

        /// The current tick (any thread)
        [[nodiscard]] static uint64_t Now();
        /// @p ticks as nanoseconds
        [[nodiscard]] static double ToNanoseconds(uint64_t ticks);
    };

}  // namespace BECore
