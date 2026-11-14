#pragma once

#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <shared_mutex>

namespace BECore {

    /// The explicit spellings of the std guards (they work over Mutex and SharedMutex as they are, with CTAD: `LockGuard lock(_mutex);`)
    template <typename TMutex>
    using LockGuard = std::lock_guard<TMutex>;

    template <typename TMutex>
    using UniqueLock = std::unique_lock<TMutex>;

    template <typename TMutex>
    using SharedLock = std::shared_lock<TMutex>;

}  // namespace BECore
