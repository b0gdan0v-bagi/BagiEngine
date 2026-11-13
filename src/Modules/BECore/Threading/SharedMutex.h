#pragma once

#include <BECore/Threading/LockHooks.h>
#include <BECore/Threading/LockOrder.h>
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
            OrderBefore();
            if (!_native.try_lock()) {
                LockContended();
            }
            OrderAcquired();
        }

        [[nodiscard]] bool try_lock() {
            const bool locked = _native.try_lock();
            if (locked) {
                OrderAcquired();
            }
            return locked;
        }

        void unlock() {
            OrderReleased();
            _native.unlock();
        }

        void lock_shared() {
            OrderBefore();
            if (!_native.try_lock_shared()) {
                LockContendedShared();
            }
            OrderAcquired();
        }

        [[nodiscard]] bool try_lock_shared() {
            const bool locked = _native.try_lock_shared();
            if (locked) {
                OrderAcquired();
            }
            return locked;
        }

        void unlock_shared() {
            OrderReleased();
            _native.unlock_shared();
        }

    private:
        /// The lock order check (LockOrder.h): nothing at all without BE_LOCK_ORDER_CHECK
        void OrderBefore() const {
#if BE_LOCK_ORDER_CHECK
            LockOrder::BeforeAcquire(*_site);
#endif
        }

        void OrderAcquired() const {
#if BE_LOCK_ORDER_CHECK
            LockOrder::Acquired(*_site);
#endif
        }

        void OrderReleased() const {
#if BE_LOCK_ORDER_CHECK
            LockOrder::Released(*_site);
#endif
        }

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
