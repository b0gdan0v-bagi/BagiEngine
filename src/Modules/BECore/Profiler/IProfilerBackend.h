#pragma once

#include <BECore/Profiler/ProfileSite.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

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
        /// The end of a frame (main thread)
        virtual void FrameMark() = 0;
        /// A named value at this moment; @p name has static storage (a literal)
        virtual void Counter(const char* name, double value) = 0;
        /// The calling thread is called @p name (a copy is kept when the backend needs it)
        virtual void SetThreadName(const char* name) = 0;

        virtual void OnAlloc(void* /*ptr*/, size_t /*size*/) {}
        virtual void OnFree(void* /*ptr*/) {}
    };

}  // namespace BECore
