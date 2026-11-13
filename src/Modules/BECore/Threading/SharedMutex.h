#pragma once

#include <BECore/Threading/LockHooks.h>
#include <BECore/Threading/LockSite.h>
#include <shared_mutex>

namespace BECore {

    /**
     * @brief The engine's reader-writer mutex: std::shared_mutex with a name (LockSite). Same contract as Mutex (Mutex.h).
     *
     * SharedLockable as well, so std::shared_lock works over it, and std::lock_guard / std::unique_lock take the writer side.
     */
    class SharedMutex {
    public:
        constexpr explicit SharedMutex(const LockSite& site) noexcept
#if BE_LOCK_SITES
            : _site(&site)
#endif
        {
            static_cast<void>(site);
        }

        SharedMutex(const SharedMutex&) = delete;
        SharedMutex& operator=(const SharedMutex&) = delete;

        void lock() {
            if (!_native.try_lock()) {
                LockContended();
            }
        }

        [[nodiscard]] bool try_lock() {
            return _native.try_lock();
        }

        void unlock() {
            _native.unlock();
        }

        void lock_shared() {
            if (!_native.try_lock_shared()) {
                LockContendedShared();
            }
        }

        [[nodiscard]] bool try_lock_shared() {
            return _native.try_lock_shared();
        }

        void unlock_shared() {
            _native.unlock_shared();
        }

    private:
#if BE_LOCK_SITES
        /// The blocking locks after a failed try, timed for the profiler (SharedMutex.cpp)
        void LockContended();
        void LockContendedShared();

        const LockSite* _site;
#else
        void LockContended() {
            _native.lock();
        }

        void LockContendedShared() {
            _native.lock_shared();
        }
#endif
        std::shared_mutex _native;
    };

#if !BE_LOCK_SITES
    static_assert(sizeof(SharedMutex) == sizeof(std::shared_mutex), "a lock without sites is the std lock");
#endif

}  // namespace BECore
