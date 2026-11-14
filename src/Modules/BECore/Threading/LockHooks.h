#pragma once

#include <BECore/Profiler/IProfilerBackend.h>
#include <BECore/Threading/LockSite.h>

/**
 * @file LockHooks.h
 * @brief The out-of-line side of the locks: what the contended path and the per-lock events of a Mutex / SharedMutex call.
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

#if BE_PROFILER

namespace BECore::LockHooks {

    /// The backend that gets an event for every lock operation (IProfilerBackend::WantsLockEvents), nullptr when none: Chrome, the ring
    /// and no backend leave it null, so their locks pay this one load and no virtual call. Set by Profiler when it installs a backend
    inline constinit std::atomic<IProfilerBackend*> g_eventBackend{nullptr};

    [[nodiscard]] inline bool EventsActive() noexcept {
        return g_eventBackend.load(std::memory_order_relaxed) != nullptr;
    }

    /// Profiler: @p backend is the effective backend when it wants lock events, else nullptr. The last non-null one is kept to end the
    /// contexts of locks destroyed later (a backend is never destroyed)
    void SetEventBackend(IProfilerBackend* backend) noexcept;

}  // namespace BECore::LockHooks

namespace BECore {

    /**
     * @brief The per-lock state of the lock events (Tracy's lockables): the opaque context the backend made for this lock.
     *
     * The context is made on the first event while a backend wants them (so never before Tracy runs), by compare-exchange: a thread
     * that loses the race ends its own context and uses the winner's. The destructor ends it, so a lock that outlives the backend is
     * safe (the backend ignores every call once it has stopped). A lock operation that began while no backend wanted events is not
     * seen by it: switching the backend while locks are held can skew Tracy's own per-lock count, never the program.
     */
    class LockEvents {
    public:
        explicit constexpr LockEvents(LockKind announceKind) noexcept : _announceKind(announceKind) {}
        ~LockEvents() {
            if (_context.load(std::memory_order_relaxed) != nullptr) {
                Terminate();
            }
        }

        LockEvents(const LockEvents&) = delete;
        LockEvents& operator=(const LockEvents&) = delete;

        /// Before the lock call; true: call AfterLock(true) when the lock was obtained
        [[nodiscard]] bool BeforeLock(const LockSite& site, LockKind kind) {
            return LockHooks::EventsActive() && BeforeLockSlow(site, kind);
        }

        void AfterLock(bool tracked, LockKind kind) {
            if (tracked) {
                AfterLockSlow(kind);
            }
        }

        void BeforeUnlock(LockKind kind) {
            if (LockHooks::EventsActive()) {
                BeforeUnlockSlow(kind);
            }
        }

        void AfterTryLock(const LockSite& site, LockKind kind, bool acquired) {
            if (LockHooks::EventsActive()) {
                AfterTryLockSlow(site, kind, acquired);
            }
        }

    private:
        bool BeforeLockSlow(const LockSite& site, LockKind kind);
        void AfterLockSlow(LockKind kind);
        void BeforeUnlockSlow(LockKind kind);
        void AfterTryLockSlow(const LockSite& site, LockKind kind, bool acquired);
        void Terminate();
        /// The context of this lock, made now when there is none yet; nullptr when the backend makes none
        void* EnsureContext(IProfilerBackend& backend, const LockSite& site);

        std::atomic<void*> _context{nullptr};
        LockKind _announceKind;
    };

}  // namespace BECore

#endif
