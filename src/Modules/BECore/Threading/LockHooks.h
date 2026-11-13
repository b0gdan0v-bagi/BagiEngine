#pragma once

#include <BECore/Threading/LockSite.h>
#include <cstdint>

/**
 * @file LockHooks.h
 * @brief The out-of-line side of the locks: what the contended path of a lock calls.
 *
 * Mutex.h and SharedMutex.h are inline for the uncontended path and include only this file besides the std headers, so they can be
 * used before the PCH is complete; everything that needs the profiler lives in LockHooks.cpp. Only built with BE_LOCK_SITES.
 */

#if BE_LOCK_SITES

namespace BECore::LockHooks {

    /// Called when try_lock failed, just before the blocking lock: the time stamp (ProfilerClock ticks) of the wait, 0 when nobody records it
    [[nodiscard]] uint64_t WaitBegin() noexcept;
    /// Called when the blocking lock returned: records the wait as a zone of site.wait when it was long enough (@p beginTicks from WaitBegin)
    void WaitEnd(const LockSite& site, uint64_t beginTicks) noexcept;

}  // namespace BECore::LockHooks

#endif
