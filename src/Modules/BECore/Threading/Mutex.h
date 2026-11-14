#pragma once

#include <BECore/Threading/LockHooks.h>
#include <BECore/Threading/LockOrder.h>
#include <BECore/Threading/LockSite.h>

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
#if BE_PROFILER
            : _site(&site), _events(LockKind::Mutex)
#elif BE_LOCK_SITES
            : _site(&site)
#endif
        {
            static_cast<void>(site);
        }

        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        void lock() {
            OrderBefore();
            const bool tracked = EventsBeforeLock();
            if (!_native.try_lock()) {
                LockContended();
            }
            EventsAfterLock(tracked);
            OrderAcquired();
        }

        [[nodiscard]] bool try_lock() {
            const bool locked = _native.try_lock();
            EventsAfterTryLock(locked);
            if (locked) {
                OrderAcquired();
            }
            return locked;
        }

        void unlock() {
            OrderReleased();
            EventsBeforeUnlock();
            _native.unlock();
        }

    private:
        friend class ConditionVariable;

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

        /// The per-lock events of a backend that wants them (Tracy): with BE_PROFILER one atomic load each, otherwise nothing at all
        [[nodiscard]] bool EventsBeforeLock() {
#if BE_PROFILER
            return _events.BeforeLock(*_site, LockKind::Mutex);
#else
            return false;
#endif
        }

        void EventsAfterLock([[maybe_unused]] bool tracked) {
#if BE_PROFILER
            _events.AfterLock(tracked, LockKind::Mutex);
#endif
        }

        void EventsAfterTryLock([[maybe_unused]] bool acquired) {
#if BE_PROFILER
            _events.AfterTryLock(*_site, LockKind::Mutex, acquired);
#endif
        }

        void EventsBeforeUnlock() {
#if BE_PROFILER
            _events.BeforeUnlock(LockKind::Mutex);
#endif
        }

#if BE_LOCK_SITES
        /// The blocking lock after a failed try_lock, timed for the profiler (Mutex.cpp)
        void LockContended();

        const LockSite* _site;
#if BE_PROFILER
        LockEvents _events;
#endif
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
