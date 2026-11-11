#pragma once

#include <BECore/Profiler/ProfileSite.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief BE_PROFILE_NO_ALLOC: a scope in which the calling thread is not expected to allocate.
     *
     * An allocation inside it is counted (AllocTotals::noAllocViolations) and, when the scope ends, reported with one LOG_WARNING
     * naming the scope, its file and line. Never an abort: a report is enough to find the allocation with the zone statistics or a
     * debugger. Only with ENGINE_PROFILER_ALLOC; otherwise nothing is counted.
     */
    class NoAllocScope {
    public:
        explicit NoAllocScope(const ProfileSite& site);
        ~NoAllocScope();

        NoAllocScope(const NoAllocScope&) = delete;
        NoAllocScope& operator=(const NoAllocScope&) = delete;

    private:
        const ProfileSite& _site;
        uint64_t _violationsAtBegin;
    };

}  // namespace BECore
