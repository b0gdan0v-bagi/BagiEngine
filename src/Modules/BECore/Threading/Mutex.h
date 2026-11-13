#pragma once

#include <BECore/Threading/LockHooks.h>
#include <BECore/Threading/LockSite.h>
#include <mutex>

namespace BECore {

    class ConditionVariable;

    /**
     * @brief The engine's mutex: std::mutex with a name (LockSite), so a long wait on it shows in the profiler.
     *
     * Lockable, so std::lock_guard, std::unique_lock (try_to_lock included) and std::scoped_lock work over it; the aliases of
     * LockGuards.h are the explicit spellings. There is no default constructor: every lock has a static name,
     * `Mutex _mutex{BE_LOCK_SITE("Task queue")};`.
     *
     * lock() is try_lock() first; only a failed try takes the out-of-line contended path (clock read, blocking lock, record), so an
     * uncontended lock reads no clock. Without BE_LOCK_SITES (a shipped build) the site is dropped and the class is std::mutex in
     * size and cost.
     */
    class Mutex {
    public:
        constexpr explicit Mutex(const LockSite& site) noexcept
#if BE_LOCK_SITES
            : _site(&site)
#endif
        {
            static_cast<void>(site);
        }

        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

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

    private:
        friend class ConditionVariable;

#if BE_LOCK_SITES
        /// The blocking lock after a failed try_lock, timed for the profiler (Mutex.cpp)
        void LockContended();

        const LockSite* _site;
#else
        void LockContended() {
            _native.lock();
        }
#endif
        std::mutex _native;
    };

#if !BE_LOCK_SITES
    static_assert(sizeof(Mutex) == sizeof(std::mutex), "a lock without sites is the std lock");
#endif

}  // namespace BECore
