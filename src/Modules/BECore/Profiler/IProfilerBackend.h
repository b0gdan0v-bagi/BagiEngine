#pragma once

#include <BECore/Profiler/ProfileSite.h>
#include <cstddef>

namespace BECore {

    /// Which side of which lock an event is about (a lock's Tracy context type follows from the kind it was announced with)
    enum class LockKind : uint8_t {
        Mutex,         // Mutex
        SharedWriter,  // the exclusive side of a SharedMutex (also the kind it is announced with)
        SharedReader,  // the shared side of a SharedMutex
    };

    /**
     * @brief Where the BE_PROFILE_* macros send what they see (Profiler::SetBackend).
     *
     * Every call except Start and Stop can come from any thread at any time, also while another thread changes the backend: a zone
     * keeps the backend it began with and ends on it, so a backend that was ever installed is never destroyed while the process runs.
     * OnAlloc / OnFree are called from inside the global operator new / delete (ENGINE_PROFILER_ALLOC): they must not allocate,
     * lock a mutex that an allocating path holds, or log.
     */
    class IProfilerBackend {
    public:
        virtual ~IProfilerBackend() = default;

        /// Called by Profiler::SetBackend before the backend gets its first event. False: the backend is not installed
        virtual bool Start() {
            return true;
        }
        /// Called by Profiler::SetBackend when another backend replaces this one (events may still arrive from zones that began here)
        virtual void Stop() {}

        /// A zone begins on the calling thread. The result is handed back to EndZone of the same zone
        virtual uint64_t BeginZone(const ProfileSite& site) = 0;
        /// The zone that BeginZone returned @p token for ends; @p allocs are the allocations it made
        virtual void EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) = 0;
        /// A finished zone of the calling thread whose begin and end the caller timed (ProfilerClock ticks): a long wait on a lock, which
        /// is known only after it ended. It nests inside the zones the thread has open, as the thread was blocked in the call. Never a
        /// BeginZone / EndZone pair, so no token. Default: nothing
        virtual void RecordZone(const ProfileSite& /*site*/, uint64_t /*beginTicks*/, uint64_t /*endTicks*/) {}
        /// The end of a frame (main thread)
        virtual void FrameMark() = 0;
        /// A named value at this moment; @p name has static storage (a literal)
        virtual void Counter(const char* name, double value) = 0;
        /// The calling thread is called @p name (a copy is kept when the backend needs it)
        virtual void SetThreadName(const char* name) = 0;

        /// True for a backend that wants an event for every lock operation (Tracy's lockables), not only the long waits of RecordZone.
        /// Profiler asks when it installs the effective backend; the lock events below are called only for such a backend
        [[nodiscard]] virtual bool WantsLockEvents() const {
            return false;
        }
        /// A lock is used for the first time: its context (an opaque pointer kept in the lock), nullptr when there is none. May be called
        /// from several threads for the same lock: the loser's context goes to TerminateLock. @p site is the lock's `wait` site (the name
        /// of the lock is its `function`)
        virtual void* AnnounceLock(const ProfileSite& /*site*/, LockKind /*kind*/) {
            return nullptr;
        }
        /// The lock is destroyed (or lost the announce race): its context goes away
        virtual void TerminateLock(void* /*context*/, LockKind /*kind*/) {}
        /// The thread is about to lock. True: call AfterLock when the lock was obtained
        virtual bool BeforeLock(void* /*context*/, LockKind /*kind*/) {
            return false;
        }
        virtual void AfterLock(void* /*context*/, LockKind /*kind*/) {}
        virtual void BeforeUnlock(void* /*context*/, LockKind /*kind*/) {}
        /// A try_lock returned (@p acquired: it got the lock)
        virtual void AfterTryLock(void* /*context*/, LockKind /*kind*/, bool /*acquired*/) {}

        virtual void OnAlloc(void* /*ptr*/, size_t /*size*/) {}
        virtual void OnFree(void* /*ptr*/) {}
    };

}  // namespace BECore
