#pragma once

#include <BECore/Profiler/Profiler.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief One zone of the BE_PROFILE_SCOPE macros: begins in the constructor, ends in the destructor.
     *
     * It keeps the backend it began on (a backend is never destroyed), so a zone that spans a change of the backend stays balanced.
     * With ENGINE_PROFILER_ALLOC it also counts the allocations of its thread between the two (inclusive of inner zones) into the
     * statistics of its site, with or without a backend, and is the thread's innermost zone while it lives (the zone that allocations are
     * charged to by name, AllocTracker::GetNameStats).
     */
    class ProfileZone {
    public:
        explicit ProfileZone(const ProfileSite& site) noexcept;
        ~ProfileZone();

        ProfileZone(const ProfileZone&) = delete;
        ProfileZone& operator=(const ProfileZone&) = delete;

    private:
        const ProfileSite& _site;
        IProfilerBackend* _backend;
        uint64_t _token = 0;
#if BE_PROFILER_ALLOC
        uint64_t _allocCountAtBegin = 0;
        uint64_t _allocBytesAtBegin = 0;
        const ProfileSite* _parentZone = nullptr;  // the zone of this thread that was innermost before this one
#endif
    };

}  // namespace BECore
