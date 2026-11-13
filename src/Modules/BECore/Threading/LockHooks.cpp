#include "LockHooks.h"

#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerClock.h>

#if BE_LOCK_SITES

namespace BECore::LockHooks {

#if BE_PROFILER
    namespace {

        constexpr double kNanosecondsPerMicrosecond = 1000.0;

    }  // namespace
#endif

    uint64_t WaitBegin() noexcept {
#if BE_PROFILER
        // No backend, no clock read: a lock waited on in a run that does not profile costs the blocking lock and one atomic load
        if (Profiler::GetBackend() != nullptr) {
            return ProfilerClock::Now();
        }
#endif
        return 0;
    }

    void WaitEnd(const LockSite& site, uint64_t beginTicks) noexcept {
#if BE_PROFILER
        if (beginTicks == 0) {
            return;
        }
        IProfilerBackend* backend = Profiler::GetBackend();
        if (backend == nullptr) {
            return;
        }
        const uint64_t endTicks = ProfilerClock::Now();
        const double waitedUs = ProfilerClock::ToNanoseconds(endTicks - beginTicks) / kNanosecondsPerMicrosecond;
        if (waitedUs >= static_cast<double>(Profiler::GetLockWaitThresholdUs())) {
            backend->RecordZone(site.wait, beginTicks, endTicks);
        }
#else
        static_cast<void>(site);
        static_cast<void>(beginTicks);
#endif
    }

}  // namespace BECore::LockHooks

#endif

#if BE_PROFILER

namespace BECore {

    namespace {

        /// The backend that made the contexts still alive (see LockHooks::SetEventBackend); a backend is never destroyed
        constinit std::atomic<IProfilerBackend*> g_contextOwner{nullptr};

    }  // namespace

    namespace LockHooks {

        void SetEventBackend(IProfilerBackend* backend) noexcept {
            if (backend != nullptr) {
                g_contextOwner.store(backend, std::memory_order_release);
            }
            g_eventBackend.store(backend, std::memory_order_release);
        }

    }  // namespace LockHooks

    void* LockEvents::EnsureContext(IProfilerBackend& backend, const LockSite& site) {
        void* context = _context.load(std::memory_order_acquire);
        if (context != nullptr) {
            return context;
        }
        void* fresh = backend.AnnounceLock(site.wait, _announceKind);
        if (fresh == nullptr) {
            return nullptr;
        }
        if (_context.compare_exchange_strong(context, fresh, std::memory_order_acq_rel)) {
            return fresh;
        }
        backend.TerminateLock(fresh, _announceKind);  // another thread announced first: use its context
        return context;
    }

    bool LockEvents::BeforeLockSlow(const LockSite& site, LockKind kind) {
        IProfilerBackend* backend = LockHooks::g_eventBackend.load(std::memory_order_acquire);
        if (backend == nullptr) {
            return false;
        }
        void* context = EnsureContext(*backend, site);
        return context != nullptr && backend->BeforeLock(context, kind);
    }

    void LockEvents::AfterLockSlow(LockKind kind) {
        IProfilerBackend* backend = LockHooks::g_eventBackend.load(std::memory_order_acquire);
        void* context = _context.load(std::memory_order_acquire);
        if (backend != nullptr && context != nullptr) {
            backend->AfterLock(context, kind);
        }
    }

    void LockEvents::BeforeUnlockSlow(LockKind kind) {
        IProfilerBackend* backend = LockHooks::g_eventBackend.load(std::memory_order_acquire);
        void* context = _context.load(std::memory_order_acquire);
        if (backend != nullptr && context != nullptr) {
            backend->BeforeUnlock(context, kind);
        }
    }

    void LockEvents::AfterTryLockSlow(const LockSite& site, LockKind kind, bool acquired) {
        IProfilerBackend* backend = LockHooks::g_eventBackend.load(std::memory_order_acquire);
        if (backend == nullptr) {
            return;
        }
        if (void* context = EnsureContext(*backend, site)) {
            backend->AfterTryLock(context, kind, acquired);
        }
    }

    void LockEvents::Terminate() {
        void* context = _context.exchange(nullptr, std::memory_order_acq_rel);
        IProfilerBackend* owner = g_contextOwner.load(std::memory_order_acquire);
        if (context != nullptr && owner != nullptr) {
            owner->TerminateLock(context, _announceKind);
        }
    }

}  // namespace BECore

#endif
