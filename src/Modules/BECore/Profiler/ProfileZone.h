#pragma once

#include <BECore/Profiler/Profiler.h>

namespace BECore {

    /**
     * @brief One zone of the BE_PROFILE_SCOPE macros: begins in the constructor, ends in the destructor.
     *
     * It keeps the backend it began on (a backend is never destroyed), so a zone that spans a change of the backend stays balanced.
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
        uint64_t _allocCountAtBegin = 0;
        uint64_t _allocBytesAtBegin = 0;
    };

}  // namespace BECore
