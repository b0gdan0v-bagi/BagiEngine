#pragma once

#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <cstddef>

namespace BECore::Tests {

    /// Leaves the process with no backend and the ring off, whatever the test did. It stops the
    /// primary backend, so declare it after every backend the test installs (locals die in reverse)
    class ProfilerStateGuard {
    public:
        ProfilerStateGuard() = default;
        ~ProfilerStateGuard() {
            Profiler::SetRingEnabled(false);
            Profiler::SetBackend(nullptr);
        }
        ProfilerStateGuard(const ProfilerStateGuard&) = delete;
        ProfilerStateGuard& operator=(const ProfilerStateGuard&) = delete;
    };

    /// The zones of a snapshot of @p ring, of every thread, for which @p match(thread, zone) is true; the snapshot is taken once
    template <typename TMatch>
    size_t CountRingZones(const FrameRingBackend& ring, TMatch&& match) {
        size_t count = 0;
        for (const RingThread& thread : ring.Snapshot().threads) {
            for (const ZoneRecord& zone : thread.zones) {
                count += match(thread, zone) ? 1 : 0;
            }
        }
        return count;
    }

}  // namespace BECore::Tests
